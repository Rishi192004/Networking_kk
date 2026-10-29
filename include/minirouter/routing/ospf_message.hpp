#pragma once

#include <cstdint>
#include <vector>
#include <optional>
#include "minirouter/common/endian.hpp"
#include "minirouter/packet/ipv4.hpp"
#include "minirouter/packet/checksum.hpp"
#include "minirouter/routing/router_id.hpp"

namespace minirouter {

constexpr uint8_t OSPF_LITE_VERSION = 1;
constexpr uint8_t OSPF_MSG_HELLO = 1;
constexpr uint8_t OSPF_MSG_LSU   = 2;

constexpr size_t OSPF_HEADER_LEN = 16;

struct OspfHeader {
    uint8_t version{OSPF_LITE_VERSION};
    uint8_t type{0};
    uint16_t length{0}; // total message length including header
    RouterId router_id;
    IPv4Address area_id{0}; // 0.0.0.0 (Backbone)
    uint16_t checksum{0};

    static std::optional<OspfHeader> parse(const uint8_t* data, size_t len) {
        if (len < OSPF_HEADER_LEN) return std::nullopt;
        if (data[0] != OSPF_LITE_VERSION) return std::nullopt;

        OspfHeader hdr;
        hdr.version = data[0];
        hdr.type = data[1];
        hdr.length = net_to_host16(read_unaligned<uint16_t>(data + 2));
        hdr.router_id = IPv4Address::from_network_order(read_unaligned<uint32_t>(data + 4));
        hdr.area_id = IPv4Address::from_network_order(read_unaligned<uint32_t>(data + 8));
        hdr.checksum = net_to_host16(read_unaligned<uint16_t>(data + 12));

        if (hdr.length < OSPF_HEADER_LEN || len < hdr.length) return std::nullopt;
        if (!validate_checksum(data, hdr.length)) return std::nullopt;

        return hdr;
    }

    void serialize(uint8_t* out) const {
        out[0] = version;
        out[1] = type;
        write_unaligned<uint16_t>(out + 2, host_to_net16(length));
        write_unaligned<uint32_t>(out + 4, router_id.to_network_order());
        write_unaligned<uint32_t>(out + 8, area_id.to_network_order());
        write_unaligned<uint16_t>(out + 12, 0); // zero for checksum calculation
        write_unaligned<uint16_t>(out + 14, 0); // reserved
    }
};

struct OspfHelloMessage {
    OspfHeader header;
    IPv4Address netmask{0xFFFFFF00u};
    uint16_t hello_interval_sec{10};
    uint16_t dead_interval_sec{40};
    std::vector<RouterId> neighbors;

    std::vector<uint8_t> serialize() const {
        size_t total_len = OSPF_HEADER_LEN + 8 + (neighbors.size() * 4);
        std::vector<uint8_t> buf(total_len, 0);

        OspfHeader hdr = header;
        hdr.version = OSPF_LITE_VERSION;
        hdr.type = OSPF_MSG_HELLO;
        hdr.length = static_cast<uint16_t>(total_len);
        hdr.serialize(buf.data());

        write_unaligned<uint32_t>(buf.data() + OSPF_HEADER_LEN, netmask.to_network_order());
        write_unaligned<uint16_t>(buf.data() + OSPF_HEADER_LEN + 4, host_to_net16(hello_interval_sec));
        write_unaligned<uint16_t>(buf.data() + OSPF_HEADER_LEN + 6, host_to_net16(dead_interval_sec));

        for (size_t i = 0; i < neighbors.size(); ++i) {
            write_unaligned<uint32_t>(buf.data() + OSPF_HEADER_LEN + 8 + (i * 4), neighbors[i].to_network_order());
        }

        uint16_t csum = calculate_checksum(buf.data(), total_len);
        write_unaligned<uint16_t>(buf.data() + 12, csum);
        return buf;
    }

    static std::optional<OspfHelloMessage> parse(const uint8_t* data, size_t len) {
        auto hdr_opt = OspfHeader::parse(data, len);
        if (!hdr_opt.has_value() || hdr_opt->type != OSPF_MSG_HELLO) return std::nullopt;
        if (hdr_opt->length < OSPF_HEADER_LEN + 8) return std::nullopt;

        OspfHelloMessage msg;
        msg.header = *hdr_opt;
        msg.netmask = IPv4Address::from_network_order(read_unaligned<uint32_t>(data + OSPF_HEADER_LEN));
        msg.hello_interval_sec = net_to_host16(read_unaligned<uint16_t>(data + OSPF_HEADER_LEN + 4));
        msg.dead_interval_sec = net_to_host16(read_unaligned<uint16_t>(data + OSPF_HEADER_LEN + 6));

        size_t rem = hdr_opt->length - (OSPF_HEADER_LEN + 8);
        size_t count = rem / 4;
        for (size_t i = 0; i < count; ++i) {
            msg.neighbors.push_back(IPv4Address::from_network_order(read_unaligned<uint32_t>(data + OSPF_HEADER_LEN + 8 + (i * 4))));
        }

        return msg;
    }
};

struct OspfLink {
    RouterId neighbor_id; // 0 for stub network
    IPv4Prefix subnet;
    uint16_t metric{10};

    bool operator==(const OspfLink& o) const {
        return neighbor_id == o.neighbor_id && subnet == o.subnet && metric == o.metric;
    }
};

struct OspfLsa {
    RouterId adv_router;
    uint32_t seq_num{1};
    uint16_t age_sec{0};
    std::vector<OspfLink> links;

    bool operator==(const OspfLsa& o) const {
        return adv_router == o.adv_router && seq_num == o.seq_num && links == o.links;
    }
};

struct OspfLsuMessage {
    OspfHeader header;
    std::vector<OspfLsa> lsas;

    std::vector<uint8_t> serialize() const {
        // Calculate size: header(16) + lsa_count(4) + each LSA
        size_t total_len = OSPF_HEADER_LEN + 4;
        for (const auto& lsa : lsas) {
            total_len += 4 + 4 + 2 + 2; // adv_router(4) + seq(4) + age(2) + link_count(2)
            total_len += lsa.links.size() * (4 + 4 + 1 + 1 + 2); // neighbor_id(4) + net(4) + len(1) + pad(1) + metric(2)
        }

        std::vector<uint8_t> buf(total_len, 0);
        OspfHeader hdr = header;
        hdr.version = OSPF_LITE_VERSION;
        hdr.type = OSPF_MSG_LSU;
        hdr.length = static_cast<uint16_t>(total_len);
        hdr.serialize(buf.data());

        write_unaligned<uint32_t>(buf.data() + OSPF_HEADER_LEN, host_to_net32(static_cast<uint32_t>(lsas.size())));
        size_t offset = OSPF_HEADER_LEN + 4;

        for (const auto& lsa : lsas) {
            write_unaligned<uint32_t>(buf.data() + offset, lsa.adv_router.to_network_order());
            write_unaligned<uint32_t>(buf.data() + offset + 4, host_to_net32(lsa.seq_num));
            write_unaligned<uint16_t>(buf.data() + offset + 8, host_to_net16(lsa.age_sec));
            write_unaligned<uint16_t>(buf.data() + offset + 10, host_to_net16(static_cast<uint16_t>(lsa.links.size())));
            offset += 12;

            for (const auto& link : lsa.links) {
                write_unaligned<uint32_t>(buf.data() + offset, link.neighbor_id.to_network_order());
                write_unaligned<uint32_t>(buf.data() + offset + 4, link.subnet.network().to_network_order());
                buf[offset + 8] = link.subnet.prefix_len();
                buf[offset + 9] = 0; // padding
                write_unaligned<uint16_t>(buf.data() + offset + 10, host_to_net16(link.metric));
                offset += 12;
            }
        }

        uint16_t csum = calculate_checksum(buf.data(), total_len);
        write_unaligned<uint16_t>(buf.data() + 12, csum);
        return buf;
    }

    static std::optional<OspfLsuMessage> parse(const uint8_t* data, size_t len) {
        auto hdr_opt = OspfHeader::parse(data, len);
        if (!hdr_opt.has_value() || hdr_opt->type != OSPF_MSG_LSU) return std::nullopt;
        if (hdr_opt->length < OSPF_HEADER_LEN + 4) return std::nullopt;

        OspfLsuMessage msg;
        msg.header = *hdr_opt;

        uint32_t lsa_count = net_to_host32(read_unaligned<uint32_t>(data + OSPF_HEADER_LEN));
        size_t offset = OSPF_HEADER_LEN + 4;

        for (uint32_t i = 0; i < lsa_count; ++i) {
            if (offset + 12 > hdr_opt->length) return std::nullopt;

            OspfLsa lsa;
            lsa.adv_router = IPv4Address::from_network_order(read_unaligned<uint32_t>(data + offset));
            lsa.seq_num = net_to_host32(read_unaligned<uint32_t>(data + offset + 4));
            lsa.age_sec = net_to_host16(read_unaligned<uint16_t>(data + offset + 8));
            uint16_t link_count = net_to_host16(read_unaligned<uint16_t>(data + offset + 10));
            offset += 12;

            for (uint16_t j = 0; j < link_count; ++j) {
                if (offset + 12 > hdr_opt->length) return std::nullopt;

                OspfLink link;
                link.neighbor_id = IPv4Address::from_network_order(read_unaligned<uint32_t>(data + offset));
                IPv4Address net_addr = IPv4Address::from_network_order(read_unaligned<uint32_t>(data + offset + 4));
                uint8_t prefix_len = data[offset + 8];
                link.subnet = IPv4Prefix(net_addr, prefix_len);
                link.metric = net_to_host16(read_unaligned<uint16_t>(data + offset + 10));

                lsa.links.push_back(link);
                offset += 12;
            }
            msg.lsas.push_back(lsa);
        }

        return msg;
    }
};

} // namespace minirouter
