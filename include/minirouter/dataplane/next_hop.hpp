#pragma once

#include <cstdint>
#include <string>
#include "minirouter/common/types.hpp"
#include "minirouter/packet/ethernet.hpp"
#include "minirouter/packet/ipv4.hpp"

namespace minirouter {

enum class NextHopType {
    DIRECT,      // Directly connected subnet (ARP resolved)
    INDIRECT     // Routed via a gateway IP
};

struct NextHop {
    NextHopId id{INVALID_NEXT_HOP_ID};
    PortId port_id{INVALID_PORT_ID};
    IPv4Address ip{IPv4Address::any()};
    MacAddress dst_mac{MacAddress::broadcast()};
    NextHopType type{NextHopType::DIRECT};
    uint64_t packet_count{0};
    uint64_t byte_count{0};

    bool is_valid() const {
        return id != INVALID_NEXT_HOP_ID && port_id != INVALID_PORT_ID;
    }
};

} // namespace minirouter
