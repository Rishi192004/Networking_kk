#pragma once

#include <cstdint>
#include <string>
#include "minirouter/common/types.hpp"
#include "minirouter/routing/router_id.hpp"

namespace minirouter {

enum class OspfNeighborState {
    DOWN,
    INIT,
    TWO_WAY,
    FULL
};

inline const char* neighbor_state_to_string(OspfNeighborState s) {
    switch (s) {
        case OspfNeighborState::DOWN: return "DOWN";
        case OspfNeighborState::INIT: return "INIT";
        case OspfNeighborState::TWO_WAY: return "2-WAY";
        case OspfNeighborState::FULL: return "FULL";
    }
    return "UNKNOWN";
}

struct OspfNeighbor {
    RouterId router_id;
    PortId local_port_id{INVALID_PORT_ID};
    IPv4Address ip_address;
    OspfNeighborState state{OspfNeighborState::DOWN};
    uint64_t last_hello_seen_ms{0};
    uint16_t dead_interval_sec{40};

    bool is_adjacency_full() const {
        return state == OspfNeighborState::FULL || state == OspfNeighborState::TWO_WAY;
    }
};

} // namespace minirouter
