#pragma once

#include <memory>
#include <vector>
#include <mutex>
#include <optional>
#include "minirouter/common/types.hpp"
#include "minirouter/common/clock.hpp"
#include "minirouter/common/logger.hpp"
#include "minirouter/packet/ethernet.hpp"
#include "minirouter/packet/ipv4.hpp"
#include "minirouter/packet/checksum.hpp"
#include "minirouter/packet/packet_buffer.hpp"
#include "minirouter/dataplane/port.hpp"
#include "minirouter/dataplane/port_manager.hpp"
#include "minirouter/dataplane/mac_table.hpp"
#include "minirouter/dataplane/fib.hpp"

namespace minirouter {

struct ForwardingEngineStats {
    uint64_t total_rx_packets{0};
    uint64_t total_tx_packets{0};
    uint64_t total_l2_forwarded{0};
    uint64_t total_l2_flooded{0};
    uint64_t total_l3_forwarded{0};
    uint64_t drop_malformed{0};
    uint64_t drop_ttl_expired{0};
    uint64_t drop_checksum_invalid{0};
    uint64_t drop_no_route{0};
    uint64_t drop_interface_down{0};
};

class ForwardingEngine {
public:
    explicit ForwardingEngine(std::shared_ptr<IClock> clock = std::make_shared<SystemClock>())
        : clock_(std::move(clock)),
          port_mgr_(std::make_shared<PortManager>()),
          mac_table_(std::make_shared<MacTable>()),
          fib_(std::make_shared<Fib>()) {}

    std::shared_ptr<PortManager> ports() const { return port_mgr_; }
    std::shared_ptr<MacTable> mac_table() const { return mac_table_; }
    std::shared_ptr<Fib> fib() const { return fib_; }
    std::shared_ptr<IClock> clock() const { return clock_; }

    ResultCode process_packet(PacketBuffer& pkt);

    ForwardingEngineStats get_stats() const {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        return stats_;
    }

    void reset_stats() {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_ = ForwardingEngineStats{};
    }

private:
    std::shared_ptr<IClock> clock_;
    std::shared_ptr<PortManager> port_mgr_;
    std::shared_ptr<MacTable> mac_table_;
    std::shared_ptr<Fib> fib_;

    mutable std::mutex stats_mutex_;
    ForwardingEngineStats stats_;

    void flood_frame(const PacketBuffer& pkt, PortId ingress_port);
};

} // namespace minirouter
