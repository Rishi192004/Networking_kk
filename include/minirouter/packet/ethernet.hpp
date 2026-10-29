#pragma once

#include <cstdint>
#include <array>
#include <string>
#include <string_view>
#include <optional>
#include <algorithm>
#include <iomanip>
#include <sstream>
#include "minirouter/common/endian.hpp"

namespace minirouter {

constexpr uint16_t ETHERTYPE_IPV4 = 0x0800;
constexpr uint16_t ETHERTYPE_ARP  = 0x0806;
constexpr uint16_t ETHERTYPE_VLAN = 0x8100;
constexpr size_t ETHERNET_HEADER_LEN = 14;

struct MacAddress {
    std::array<uint8_t, 6> bytes{0, 0, 0, 0, 0, 0};

    constexpr MacAddress() = default;
    constexpr explicit MacAddress(const std::array<uint8_t, 6>& b) : bytes(b) {}
    constexpr MacAddress(uint8_t b0, uint8_t b1, uint8_t b2, uint8_t b3, uint8_t b4, uint8_t b5)
        : bytes{b0, b1, b2, b3, b4, b5} {}

    bool operator==(const MacAddress& other) const {
        return bytes == other.bytes;
    }

    bool operator!=(const MacAddress& other) const {
        return bytes != other.bytes;
    }

    bool operator<(const MacAddress& other) const {
        return bytes < other.bytes;
    }

    bool is_broadcast() const {
        for (auto b : bytes) {
            if (b != 0xFF) return false;
        }
        return true;
    }

    bool is_multicast() const {
        return (bytes[0] & 0x01) != 0;
    }

    bool is_zero() const {
        for (auto b : bytes) {
            if (b != 0) return false;
        }
        return true;
    }

    std::string to_string() const {
        std::ostringstream oss;
        oss << std::hex << std::setfill('0');
        for (size_t i = 0; i < 6; ++i) {
            if (i > 0) oss << ":";
            oss << std::setw(2) << static_cast<int>(bytes[i]);
        }
        return oss.str();
    }

    static std::optional<MacAddress> from_string(std::string_view str) {
        if (str.size() != 17) return std::nullopt;
        std::array<uint8_t, 6> b{};
        for (size_t i = 0; i < 6; ++i) {
            size_t idx = i * 3;
            if (i < 5 && str[idx + 2] != ':' && str[idx + 2] != '-') {
                return std::nullopt;
            }
            char c1 = str[idx];
            char c2 = str[idx + 1];
            auto hex_val = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return -1;
            };
            int h1 = hex_val(c1);
            int h2 = hex_val(c2);
            if (h1 < 0 || h2 < 0) return std::nullopt;
            b[i] = static_cast<uint8_t>((h1 << 4) | h2);
        }
        return MacAddress(b);
    }

    static MacAddress broadcast() {
        return MacAddress(0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF);
    }
};

struct EthernetHeader {
    MacAddress dst_mac;
    MacAddress src_mac;
    uint16_t ethertype{0}; // Host byte order

    static std::optional<EthernetHeader> parse(const uint8_t* data, size_t len) {
        if (len < ETHERNET_HEADER_LEN) return std::nullopt;
        EthernetHeader hdr;
        std::memcpy(hdr.dst_mac.bytes.data(), data, 6);
        std::memcpy(hdr.src_mac.bytes.data(), data + 6, 6);
        hdr.ethertype = net_to_host16(read_unaligned<uint16_t>(data + 12));
        return hdr;
    }

    void serialize(uint8_t* data) const {
        std::memcpy(data, dst_mac.bytes.data(), 6);
        std::memcpy(data + 6, src_mac.bytes.data(), 6);
        write_unaligned<uint16_t>(data + 12, host_to_net16(ethertype));
    }
};

} // namespace minirouter
