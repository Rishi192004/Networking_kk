#pragma once

#include <cstdint>
#include <vector>
#include <memory>
#include <functional>
#include "minirouter/common/types.hpp"
#include "minirouter/common/endian.hpp"
#include "minirouter/dataplane/next_hop.hpp"

namespace minirouter {

struct Flow5Tuple {
    IPv4Address src_ip;
    IPv4Address dst_ip;
    uint8_t protocol{0};
    uint16_t src_port{0};
    uint16_t dst_port{0};

    // Deterministic flow hash (MurmurHash3-inspired 32-bit mix)
    uint32_t hash() const {
        uint32_t h = 0x811c9dc5u; // FNV offset basis
        auto mix = [&h](uint32_t k) {
            k *= 0xcc9e2d51u;
            k = (k << 15) | (k >> 17);
            k *= 0x1b873593u;

            h ^= k;
            h = (h << 13) | (h >> 19);
            h = h * 5 + 0xe6546b64u;
        };

        mix(src_ip.to_host_order());
        mix(dst_ip.to_host_order());
        mix((static_cast<uint32_t>(src_port) << 16) | dst_port);
        mix(static_cast<uint32_t>(protocol));

        // Final avalanche
        h ^= h >> 16;
        h *= 0x85ebca6bu;
        h ^= h >> 13;
        h *= 0xc2b2ae35u;
        h ^= h >> 16;
        return h;
    }

    static Flow5Tuple extract(const IPv4Header& ip_hdr, const uint8_t* payload, size_t payload_len) {
        Flow5Tuple flow;
        flow.src_ip = ip_hdr.src_ip;
        flow.dst_ip = ip_hdr.dst_ip;
        flow.protocol = ip_hdr.protocol;

        if ((ip_hdr.protocol == IP_PROTO_TCP || ip_hdr.protocol == IP_PROTO_UDP) && payload_len >= 4) {
            flow.src_port = net_to_host16(read_unaligned<uint16_t>(payload));
            flow.dst_port = net_to_host16(read_unaligned<uint16_t>(payload + 2));
        }
        return flow;
    }
};

struct NextHopGroup {
    NextHopGroupId id{INVALID_NEXT_HOP_GROUP_ID};
    std::vector<NextHopId> next_hop_ids;

    NextHopId select_next_hop(const Flow5Tuple& flow) const {
        if (next_hop_ids.empty()) {
            return INVALID_NEXT_HOP_ID;
        }
        if (next_hop_ids.size() == 1) {
            return next_hop_ids[0];
        }
        uint32_t hash_val = flow.hash();
        size_t index = hash_val % next_hop_ids.size();
        return next_hop_ids[index];
    }
};

} // namespace minirouter
