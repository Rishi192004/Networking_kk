#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <optional>
#include <vector>
#include <sstream>
#include "minirouter/common/endian.hpp"
#include "minirouter/packet/checksum.hpp"

namespace minirouter {

constexpr uint8_t IP_PROTO_ICMP = 1;
constexpr uint8_t IP_PROTO_TCP  = 6;
constexpr uint8_t IP_PROTO_UDP  = 17;
constexpr uint8_t IP_PROTO_OSPF = 89;

class IPv4Address {
public:
    constexpr IPv4Address() : addr_host_(0) {}
    constexpr explicit IPv4Address(uint32_t host_val) : addr_host_(host_val) {}
    constexpr IPv4Address(uint8_t b0, uint8_t b1, uint8_t b2, uint8_t b3)
        : addr_host_((static_cast<uint32_t>(b0) << 24) |
                     (static_cast<uint32_t>(b1) << 16) |
                     (static_cast<uint32_t>(b2) << 8)  |
                      static_cast<uint32_t>(b3)) {}

    static IPv4Address from_network_order(uint32_t net_val) {
        return IPv4Address(net_to_host32(net_val));
    }

    constexpr uint32_t to_host_order() const { return addr_host_; }
    uint32_t to_network_order() const { return host_to_net32(addr_host_); }

    uint8_t octet(size_t index) const {
        if (index > 3) return 0;
        return static_cast<uint8_t>((addr_host_ >> ((3 - index) * 8)) & 0xFFu);
    }

    constexpr bool operator==(const IPv4Address& other) const { return addr_host_ == other.addr_host_; }
    constexpr bool operator!=(const IPv4Address& other) const { return addr_host_ != other.addr_host_; }
    constexpr bool operator<(const IPv4Address& other) const { return addr_host_ < other.addr_host_; }

    std::string to_string() const {
        std::ostringstream oss;
        oss << static_cast<int>(octet(0)) << "."
            << static_cast<int>(octet(1)) << "."
            << static_cast<int>(octet(2)) << "."
            << static_cast<int>(octet(3));
        return oss.str();
    }

    static std::optional<IPv4Address> from_string(std::string_view str) {
        uint32_t octets[4] = {0, 0, 0, 0};
        size_t octet_idx = 0;
        uint32_t current_val = 0;
        bool has_digit = false;

        for (size_t i = 0; i < str.size(); ++i) {
            char c = str[i];
            if (c >= '0' && c <= '9') {
                current_val = current_val * 10 + static_cast<uint32_t>(c - '0');
                if (current_val > 255) return std::nullopt;
                has_digit = true;
            } else if (c == '.') {
                if (!has_digit || octet_idx >= 3) return std::nullopt;
                octets[octet_idx++] = current_val;
                current_val = 0;
                has_digit = false;
            } else {
                return std::nullopt;
            }
        }

        if (!has_digit || octet_idx != 3) return std::nullopt;
        octets[3] = current_val;

        return IPv4Address(static_cast<uint8_t>(octets[0]),
                           static_cast<uint8_t>(octets[1]),
                           static_cast<uint8_t>(octets[2]),
                           static_cast<uint8_t>(octets[3]));
    }

    static constexpr IPv4Address any() { return IPv4Address(0); }
    static constexpr IPv4Address broadcast() { return IPv4Address(0xFFFFFFFFu); }

private:
    uint32_t addr_host_{0};
};

class IPv4Prefix {
public:
    static constexpr uint32_t netmask_for_len(uint8_t len) {
        if (len == 0) return 0;
        if (len >= 32) return 0xFFFFFFFFu;
        return static_cast<uint32_t>(~((1ULL << (32 - len)) - 1ULL));
    }

    static constexpr IPv4Address mask_address(IPv4Address addr, uint8_t len) {
        uint32_t mask = netmask_for_len(len);
        return IPv4Address(addr.to_host_order() & mask);
    }

    constexpr IPv4Prefix() : network_(0), prefix_len_(0) {}
    constexpr IPv4Prefix(IPv4Address addr, uint8_t prefix_len)
        : network_(mask_address(addr, prefix_len)), prefix_len_(prefix_len > 32 ? 32 : prefix_len) {}

    constexpr IPv4Address network() const { return network_; }
    constexpr uint8_t prefix_len() const { return prefix_len_; }
    constexpr uint32_t mask() const { return netmask_for_len(prefix_len_); }

    constexpr bool contains(IPv4Address addr) const {
        return (addr.to_host_order() & mask()) == network_.to_host_order();
    }

    constexpr bool operator==(const IPv4Prefix& other) const {
        return network_ == other.network_ && prefix_len_ == other.prefix_len_;
    }

    constexpr bool operator!=(const IPv4Prefix& other) const {
        return !(*this == other);
    }

    constexpr bool operator<(const IPv4Prefix& other) const {
        if (network_ != other.network_) return network_ < other.network_;
        return prefix_len_ < other.prefix_len_;
    }

    std::string to_string() const {
        std::ostringstream oss;
        oss << network_.to_string() << "/" << static_cast<int>(prefix_len_);
        return oss.str();
    }

    static std::optional<IPv4Prefix> from_string(std::string_view str) {
        auto slash_pos = str.find('/');
        if (slash_pos == std::string_view::npos) {
            auto addr = IPv4Address::from_string(str);
            if (!addr) return std::nullopt;
            return IPv4Prefix(*addr, 32);
        }

        auto ip_str = str.substr(0, slash_pos);
        auto len_str = str.substr(slash_pos + 1);

        auto addr = IPv4Address::from_string(ip_str);
        if (!addr) return std::nullopt;

        if (len_str.empty()) return std::nullopt;
        uint32_t len = 0;
        for (char c : len_str) {
            if (c < '0' || c > '9') return std::nullopt;
            len = len * 10 + static_cast<uint32_t>(c - '0');
            if (len > 32) return std::nullopt;
        }

        return IPv4Prefix(*addr, static_cast<uint8_t>(len));
    }

    static constexpr IPv4Prefix default_route() {
        return IPv4Prefix(IPv4Address(0), 0);
    }

private:
    IPv4Address network_{0};
    uint8_t prefix_len_{0};
};

struct IPv4Header {
    uint8_t version{4};
    uint8_t ihl{5}; // in 32-bit words (5 = 20 bytes)
    uint8_t dscp{0};
    uint8_t ecn{0};
    uint16_t total_length{20};
    uint16_t identification{0};
    uint8_t flags{0}; // 3 bits
    uint16_t fragment_offset{0}; // 13 bits
    uint8_t ttl{64};
    uint8_t protocol{0};
    uint16_t checksum{0}; // in network byte order in raw header, we store host order
    IPv4Address src_ip;
    IPv4Address dst_ip;
    std::vector<uint8_t> options;

    size_t header_bytes() const {
        return static_cast<size_t>(ihl) * 4;
    }

    static std::optional<IPv4Header> parse(const uint8_t* data, size_t len) {
        if (len < 20) return std::nullopt;

        uint8_t ver_ihl = data[0];
        uint8_t version = ver_ihl >> 4;
        uint8_t ihl = ver_ihl & 0x0Fu;

        if (version != 4 || ihl < 5) return std::nullopt;
        size_t hdr_len = static_cast<size_t>(ihl) * 4;
        if (len < hdr_len) return std::nullopt;

        uint16_t total_len = net_to_host16(read_unaligned<uint16_t>(data + 2));
        if (total_len < hdr_len || len < total_len) return std::nullopt;

        // Verify IPv4 header checksum
        if (!validate_checksum(data, hdr_len)) {
            return std::nullopt;
        }

        IPv4Header hdr;
        hdr.version = version;
        hdr.ihl = ihl;

        uint8_t tos = data[1];
        hdr.dscp = static_cast<uint8_t>(tos >> 2);
        hdr.ecn = static_cast<uint8_t>(tos & 0x03u);

        hdr.total_length = total_len;
        hdr.identification = net_to_host16(read_unaligned<uint16_t>(data + 4));

        uint16_t flags_frag = net_to_host16(read_unaligned<uint16_t>(data + 6));
        hdr.flags = static_cast<uint8_t>(flags_frag >> 13);
        hdr.fragment_offset = flags_frag & 0x1FFFu;

        hdr.ttl = data[8];
        hdr.protocol = data[9];
        hdr.checksum = net_to_host16(read_unaligned<uint16_t>(data + 10));

        hdr.src_ip = IPv4Address::from_network_order(read_unaligned<uint32_t>(data + 12));
        hdr.dst_ip = IPv4Address::from_network_order(read_unaligned<uint32_t>(data + 16));

        if (hdr_len > 20) {
            hdr.options.assign(data + 20, data + hdr_len);
        }

        return hdr;
    }

    void serialize(uint8_t* out) const {
        uint8_t ver_ihl = static_cast<uint8_t>((version << 4) | (ihl & 0x0Fu));
        out[0] = ver_ihl;
        out[1] = static_cast<uint8_t>((dscp << 2) | (ecn & 0x03u));
        write_unaligned<uint16_t>(out + 2, host_to_net16(total_length));
        write_unaligned<uint16_t>(out + 4, host_to_net16(identification));

        uint16_t flags_frag = static_cast<uint16_t>((static_cast<uint16_t>(flags) << 13) | (fragment_offset & 0x1FFFu));
        write_unaligned<uint16_t>(out + 6, host_to_net16(flags_frag));

        out[8] = ttl;
        out[9] = protocol;
        write_unaligned<uint16_t>(out + 10, 0); // Checksum 0 before calculation

        write_unaligned<uint32_t>(out + 12, src_ip.to_network_order());
        write_unaligned<uint32_t>(out + 16, dst_ip.to_network_order());

        size_t total_hdr_len = static_cast<size_t>(ihl) * 4;
        if (!options.empty() && total_hdr_len > 20) {
            size_t max_opt = total_hdr_len - 20;
            size_t opt_bytes = std::min(options.size(), max_opt);
            std::memcpy(out + 20, options.data(), opt_bytes);
            if (opt_bytes < max_opt) {
                std::memset(out + 20 + opt_bytes, 0, max_opt - opt_bytes);
            }
        }

        uint16_t csum = calculate_checksum(out, total_hdr_len);
        write_unaligned<uint16_t>(out + 10, csum);
    }
};

} // namespace minirouter
