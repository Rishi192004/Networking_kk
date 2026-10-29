#pragma once

#include <memory>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <functional>
#include "minirouter/common/types.hpp"
#include "minirouter/common/clock.hpp"
#include "minirouter/common/logger.hpp"
#include "minirouter/dataplane/forwarding_engine.hpp"
#include "minirouter/routing/router_id.hpp"
#include "minirouter/routing/ospf_message.hpp"
#include "minirouter/routing/ospf_neighbor.hpp"
#include "minirouter/routing/lsdb.hpp"
#include "minirouter/routing/dijkstra.hpp"

namespace minirouter {

struct OspfInterfaceConfig {
    PortId port_id{INVALID_PORT_ID};
    IPv4Prefix subnet;
    uint16_t metric{10};
    uint16_t hello_interval_sec{10};
    uint16_t dead_interval_sec{40};
    bool enabled{true};
};

class OspfRouter {
public:
    using MessageSender = std::function<void(PortId port_id, const std::vector<uint8_t>& message)>;

    OspfRouter(RouterId router_id,
               std::shared_ptr<ForwardingEngine> engine,
               std::shared_ptr<IClock> clock = std::make_shared<SystemClock>())
        : router_id_(router_id),
          engine_(std::move(engine)),
          clock_(std::move(clock)) {}

    RouterId router_id() const { return router_id_; }
    std::shared_ptr<Lsdb> lsdb() const { return lsdb_; }

    void set_message_sender(MessageSender sender) {
        msg_sender_ = std::move(sender);
    }

    ResultCode add_interface(const OspfInterfaceConfig& cfg) {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        if (interfaces_.find(cfg.port_id) != interfaces_.end()) {
            return ResultCode::ERROR_ALREADY_EXISTS;
        }
        interfaces_[cfg.port_id] = cfg;
        return ResultCode::SUCCESS;
    }

    ResultCode set_link_metric(PortId port_id, uint16_t new_metric) {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        auto it = interfaces_.find(port_id);
        if (it == interfaces_.end()) return ResultCode::ERROR_NOT_FOUND;
        it->second.metric = new_metric;
        originate_local_lsa_locked();
        recompute_routes_locked();
        return ResultCode::SUCCESS;
    }

    ResultCode set_interface_enabled(PortId port_id, bool enabled) {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        auto it = interfaces_.find(port_id);
        if (it == interfaces_.end()) return ResultCode::ERROR_NOT_FOUND;
        it->second.enabled = enabled;

        // If disabling, drop any neighbor on this port
        if (!enabled) {
            for (auto& [_, nbr] : neighbors_) {
                if (nbr.local_port_id == port_id) {
                    nbr.state = OspfNeighborState::DOWN;
                }
            }
        }

        originate_local_lsa_locked();
        recompute_routes_locked();
        return ResultCode::SUCCESS;
    }

    // Called periodically or by simulated clock tick
    void tick() {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        uint64_t now = clock_->now_ms();

        // 1. Send periodic Hellos on active interfaces
        for (const auto& [port_id, iface] : interfaces_) {
            if (!iface.enabled) continue;

            if (now >= last_hello_sent_ms_[port_id] + (iface.hello_interval_sec * 1000ULL)) {
                send_hello_locked(iface);
                last_hello_sent_ms_[port_id] = now;
            }
        }

        // 2. Check neighbor dead timers
        bool topology_changed = false;
        for (auto& [nbr_id, nbr] : neighbors_) {
            if (nbr.state != OspfNeighborState::DOWN) {
                if (now >= nbr.last_hello_seen_ms + (nbr.dead_interval_sec * 1000ULL)) {
                    LOG_WARN("Neighbor " << nbr_id.to_string() << " dead timer expired -> DOWN");
                    nbr.state = OspfNeighborState::DOWN;
                    topology_changed = true;
                }
            }
        }

        if (topology_changed) {
            originate_local_lsa_locked();
            recompute_routes_locked();
        }
    }

    // Process incoming OSPF message from port
    void process_message(PortId ingress_port, const uint8_t* data, size_t len) {
        if (len < OSPF_HEADER_LEN) return;
        uint8_t msg_type = data[1];

        if (msg_type == OSPF_MSG_HELLO) {
            auto hello = OspfHelloMessage::parse(data, len);
            if (hello.has_value()) {
                handle_hello(ingress_port, *hello);
            }
        } else if (msg_type == OSPF_MSG_LSU) {
            auto lsu = OspfLsuMessage::parse(data, len);
            if (lsu.has_value()) {
                handle_lsu(ingress_port, *lsu);
            }
        }
    }

    std::vector<OspfNeighbor> get_neighbors() const {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        std::vector<OspfNeighbor> res;
        for (const auto& [_, n] : neighbors_) {
            res.push_back(n);
        }
        return res;
    }

    void force_originate_lsa() {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        originate_local_lsa_locked();
    }

    void force_recompute_routes() {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        recompute_routes_locked();
    }

private:
    RouterId router_id_;
    std::shared_ptr<ForwardingEngine> engine_;
    std::shared_ptr<IClock> clock_;
    std::shared_ptr<Lsdb> lsdb_{std::make_shared<Lsdb>()};
    MessageSender msg_sender_;

    mutable std::recursive_mutex mutex_;
    std::unordered_map<PortId, OspfInterfaceConfig> interfaces_;
    std::unordered_map<PortId, uint64_t> last_hello_sent_ms_;
    std::unordered_map<RouterId, OspfNeighbor, RouterIdHash> neighbors_;
    uint32_t lsa_sequence_number_{1};

    void handle_hello(PortId ingress_port, const OspfHelloMessage& hello) {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        RouterId nbr_id = hello.header.router_id;
        if (nbr_id == router_id_) return; // Ignore self-hello

        auto& nbr = neighbors_[nbr_id];
        nbr.router_id = nbr_id;
        nbr.local_port_id = ingress_port;
        nbr.dead_interval_sec = hello.dead_interval_sec;
        nbr.last_hello_seen_ms = clock_->now_ms();

        // Check if our router_id is in neighbor's active neighbors list
        bool saw_ourselves = false;
        for (const auto& id : hello.neighbors) {
            if (id == router_id_) {
                saw_ourselves = true;
                break;
            }
        }

        OspfNeighborState old_state = nbr.state;
        if (saw_ourselves) {
            nbr.state = OspfNeighborState::FULL; // Direct 2-Way to Full in OSPF-lite
        } else {
            nbr.state = OspfNeighborState::INIT;
        }

        if (nbr.state != old_state) {
            LOG_INFO("OSPF Router " << router_id_.to_string() << ": neighbor "
                     << nbr_id.to_string() << " transitioned "
                     << neighbor_state_to_string(old_state) << " -> "
                     << neighbor_state_to_string(nbr.state));

            // Immediately send Hello back so neighbor sees our ID
            auto if_it = interfaces_.find(ingress_port);
            if (if_it != interfaces_.end()) {
                send_hello_locked(if_it->second);
            }

            if (nbr.state == OspfNeighborState::FULL) {
                // Originate new local LSA containing this neighbor
                originate_local_lsa_locked();
                recompute_routes_locked();

                // Send our entire LSDB to this neighbor
                send_lsdb_to_neighbor_locked(ingress_port);
            }
        }
    }

    void handle_lsu(PortId ingress_port, const OspfLsuMessage& lsu) {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        bool lsdb_updated = false;
        std::vector<OspfLsa> new_lsas;

        for (const auto& lsa : lsu.lsas) {
            if (lsa.adv_router == router_id_) {
                // Ignore self LSA
                continue;
            }

            if (lsdb_->install_lsa(lsa)) {
                lsdb_updated = true;
                new_lsas.push_back(lsa);
            }
        }

        if (lsdb_updated) {
            // Flood updated LSAs to all other active neighbors
            flood_lsas_locked(new_lsas, ingress_port);
            // Run SPF and install routes
            recompute_routes_locked();
        }
    }

    void send_hello_locked(const OspfInterfaceConfig& iface) {
        OspfHelloMessage msg;
        msg.header.router_id = router_id_;
        msg.header.area_id = IPv4Address(0);
        msg.netmask = IPv4Address(IPv4Prefix::netmask_for_len(iface.subnet.prefix_len()));
        msg.hello_interval_sec = iface.hello_interval_sec;
        msg.dead_interval_sec = iface.dead_interval_sec;

        for (const auto& [_, nbr] : neighbors_) {
            if (nbr.local_port_id == iface.port_id && nbr.state != OspfNeighborState::DOWN) {
                msg.neighbors.push_back(nbr.router_id);
            }
        }

        auto serialized = msg.serialize();
        if (msg_sender_) {
            msg_sender_(iface.port_id, serialized);
        }
    }

    void originate_local_lsa_locked() {
        OspfLsa local_lsa;
        local_lsa.adv_router = router_id_;
        local_lsa.seq_num = lsa_sequence_number_++;
        local_lsa.age_sec = 0;

        for (const auto& [port_id, iface] : interfaces_) {
            if (!iface.enabled) continue;

            bool has_full_nbr = false;
            for (const auto& [nbr_id, nbr] : neighbors_) {
                if (nbr.local_port_id == port_id && nbr.is_adjacency_full()) {
                    OspfLink link;
                    link.neighbor_id = nbr_id;
                    link.subnet = iface.subnet;
                    link.metric = iface.metric;
                    local_lsa.links.push_back(link);
                    has_full_nbr = true;
                }
            }

            // Also advertise stub link for this interface
            if (!has_full_nbr) {
                OspfLink stub_link;
                stub_link.neighbor_id = RouterId(0);
                stub_link.subnet = iface.subnet;
                stub_link.metric = iface.metric;
                local_lsa.links.push_back(stub_link);
            }
        }

        lsdb_->install_lsa(local_lsa);
        flood_lsas_locked({local_lsa}, INVALID_PORT_ID);
    }

    void flood_lsas_locked(const std::vector<OspfLsa>& lsas, PortId exclude_port) {
        if (lsas.empty() || !msg_sender_) return;

        OspfLsuMessage lsu;
        lsu.header.router_id = router_id_;
        lsu.header.area_id = IPv4Address(0);
        lsu.lsas = lsas;

        auto serialized = lsu.serialize();
        for (const auto& [port_id, iface] : interfaces_) {
            if (iface.enabled && port_id != exclude_port) {
                msg_sender_(port_id, serialized);
            }
        }
    }

    void send_lsdb_to_neighbor_locked(PortId port_id) {
        if (!msg_sender_) return;
        auto all_lsas = lsdb_->get_all_lsas();
        if (all_lsas.empty()) return;

        OspfLsuMessage lsu;
        lsu.header.router_id = router_id_;
        lsu.header.area_id = IPv4Address(0);
        lsu.lsas = std::move(all_lsas);

        msg_sender_(port_id, lsu.serialize());
    }

    void recompute_routes_locked() {
        if (!engine_) return;

        // Collect direct neighbors
        std::vector<DirectNeighborLink> direct_nbrs;
        for (const auto& [nbr_id, nbr] : neighbors_) {
            if (nbr.is_adjacency_full()) {
                auto if_it = interfaces_.find(nbr.local_port_id);
                if (if_it != interfaces_.end()) {
                    DirectNeighborLink dnl;
                    dnl.neighbor_id = nbr_id;
                    dnl.port_id = nbr.local_port_id;
                    dnl.neighbor_ip = nbr.ip_address;
                    dnl.cost = if_it->second.metric;
                    direct_nbrs.push_back(dnl);
                }
            }
        }

        auto computed = Dijkstra::calculate_spf(router_id_, lsdb_->get_all_lsas(), direct_nbrs);

        // Install newly computed routes into Forwarding Engine FIB
        NextHopId nh_alloc = 100;
        for (const auto& r : computed) {
            NextHop nh;
            nh.id = nh_alloc++;
            nh.port_id = r.egress_port;
            nh.ip = r.next_hop_ip;
            nh.type = NextHopType::INDIRECT;

            // Interface MAC
            auto port = engine_->ports()->get_port(r.egress_port);
            if (port) {
                nh.dst_mac = MacAddress::broadcast(); // Will be ARP-resolved or static
            }

            engine_->fib()->add_next_hop(nh);
            engine_->fib()->add_route(r.subnet, nh.id);
        }
    }
};

} // namespace minirouter
