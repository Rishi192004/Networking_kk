#pragma once

#include <vector>
#include <unordered_map>
#include <queue>
#include <limits>
#include "minirouter/common/types.hpp"
#include "minirouter/packet/ipv4.hpp"
#include "minirouter/routing/router_id.hpp"
#include "minirouter/routing/lsdb.hpp"

namespace minirouter {

struct ComputedRoute {
    IPv4Prefix subnet;
    PortId egress_port{INVALID_PORT_ID};
    IPv4Address next_hop_ip;
    uint32_t cost{0};
};

struct DirectNeighborLink {
    RouterId neighbor_id;
    PortId port_id{INVALID_PORT_ID};
    IPv4Address neighbor_ip;
    uint16_t cost{0};

    DirectNeighborLink() = default;
    DirectNeighborLink(RouterId nid, PortId pid, IPv4Address nip, uint16_t c)
        : neighbor_id(nid), port_id(pid), neighbor_ip(nip), cost(c) {}
};

class Dijkstra {
public:
    static std::vector<ComputedRoute> calculate_spf(
        RouterId local_id,
        const std::vector<OspfLsa>& lsdb,
        const std::vector<DirectNeighborLink>& direct_neighbors
    ) {
        // Map from RouterId to LSA
        std::unordered_map<RouterId, const OspfLsa*, RouterIdHash> lsa_map;
        for (const auto& lsa : lsdb) {
            lsa_map[lsa.adv_router] = &lsa;
        }

        // Direct neighbor map for fast lookup of first-hop egress info
        std::unordered_map<RouterId, DirectNeighborLink, RouterIdHash> direct_map;
        for (const auto& dn : direct_neighbors) {
            direct_map[dn.neighbor_id] = dn;
        }

        struct VertexState {
            uint32_t dist{std::numeric_limits<uint32_t>::max()};
            PortId egress_port{INVALID_PORT_ID};
            IPv4Address next_hop_ip;
            bool visited{false};
        };

        std::unordered_map<RouterId, VertexState, RouterIdHash> state;

        // Initialize root (local router)
        state[local_id] = VertexState{0, INVALID_PORT_ID, IPv4Address::any(), false};

        // Priority queue: (dist, RouterId)
        using PQItem = std::pair<uint32_t, RouterId>;
        auto cmp = [](const PQItem& a, const PQItem& b) { return a.first > b.first; };
        std::priority_queue<PQItem, std::vector<PQItem>, decltype(cmp)> pq(cmp);

        pq.push({0, local_id});

        while (!pq.empty()) {
            auto [d, u] = pq.top();
            pq.pop();

            auto& u_state = state[u];
            if (u_state.visited) continue;
            u_state.visited = true;

            auto it = lsa_map.find(u);
            if (it == lsa_map.end()) continue;
            const OspfLsa* u_lsa = it->second;

            for (const auto& link : u_lsa->links) {
                RouterId v = link.neighbor_id;
                if (v == RouterId(0)) continue; // Stub link, processed later

                uint32_t new_dist = d + link.metric;
                auto& v_state = state[v];

                if (new_dist < v_state.dist) {
                    v_state.dist = new_dist;

                    // Derive first-hop egress port and next-hop IP
                    if (u == local_id) {
                        // First hop directly connected to us
                        auto dn_it = direct_map.find(v);
                        if (dn_it != direct_map.end()) {
                            v_state.egress_port = dn_it->second.port_id;
                            v_state.next_hop_ip = dn_it->second.neighbor_ip;
                        }
                    } else {
                        // Inherit next-hop from parent u
                        v_state.egress_port = u_state.egress_port;
                        v_state.next_hop_ip = u_state.next_hop_ip;
                    }

                    pq.push({new_dist, v});
                }
            }
        }

        // Derive IP routes from all advertised subnets
        std::vector<ComputedRoute> routes;
        std::unordered_map<std::string, uint32_t> best_subnet_cost;

        for (const auto& lsa : lsdb) {
            auto st_it = state.find(lsa.adv_router);
            if (st_it == state.end() || !st_it->second.visited) continue;

            const VertexState& r_state = st_it->second;

            for (const auto& link : lsa.links) {
                // If it's a stub network or transit network
                uint32_t total_cost = r_state.dist + (link.neighbor_id == RouterId(0) ? link.metric : 0);
                std::string pfx_str = link.subnet.to_string();

                if (lsa.adv_router == local_id) {
                    // Local subnet, already directly connected
                    continue;
                }

                auto cost_it = best_subnet_cost.find(pfx_str);
                if (cost_it == best_subnet_cost.end() || total_cost < cost_it->second) {
                    best_subnet_cost[pfx_str] = total_cost;

                    ComputedRoute cr;
                    cr.subnet = link.subnet;
                    cr.egress_port = r_state.egress_port;
                    cr.next_hop_ip = r_state.next_hop_ip;
                    cr.cost = total_cost;

                    // Replace or add
                    bool replaced = false;
                    for (auto& r : routes) {
                        if (r.subnet == link.subnet) {
                            r = cr;
                            replaced = true;
                            break;
                        }
                    }
                    if (!replaced) {
                        routes.push_back(cr);
                    }
                }
            }
        }

        return routes;
    }
};

} // namespace minirouter
