#pragma once

#include <cstdint>
#include <cstddef>
#include "minirouter/common/endian.hpp"

namespace minirouter {

/**
 * Calculates RFC 1071 Internet Checksum.
 * Returns 16-bit checksum in host byte order.
 */
inline uint16_t calculate_checksum(const void* data, size_t length) {
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    uint32_t sum = 0;

    while (length > 1) {
        uint16_t word = read_unaligned<uint16_t>(bytes);
        sum += word;
        bytes += 2;
        length -= 2;
    }

    if (length == 1) {
        uint16_t word = 0;
        std::memcpy(&word, bytes, 1);
        sum += word;
    }

    while (sum >> 16) {
        sum = (sum & 0xFFFFu) + (sum >> 16);
    }

    return static_cast<uint16_t>(~sum);
}

/**
 * Validates checksum over a block that already contains the checksum field.
 * Returns true if the one's complement sum is 0 (or 0xFFFF).
 */
inline bool validate_checksum(const void* data, size_t length) {
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    uint32_t sum = 0;

    while (length > 1) {
        uint16_t word = read_unaligned<uint16_t>(bytes);
        sum += word;
        bytes += 2;
        length -= 2;
    }

    if (length == 1) {
        uint16_t word = 0;
        std::memcpy(&word, bytes, 1);
        sum += word;
    }

    while (sum >> 16) {
        sum = (sum & 0xFFFFu) + (sum >> 16);
    }

    return static_cast<uint16_t>(~sum) == 0;
}

/**
 * Incrementally updates an IPv4 checksum when a 16-bit word changes (RFC 1624).
 * old_val and new_val are in host or network byte order (must match each other and checksum).
 */
inline uint16_t update_checksum16(uint16_t old_csum, uint16_t old_val, uint16_t new_val) {
    uint32_t sum = static_cast<uint32_t>((~old_csum) & 0xFFFFu) +
                   static_cast<uint32_t>((~old_val) & 0xFFFFu) +
                   static_cast<uint32_t>(new_val);
    while (sum >> 16) {
        sum = (sum & 0xFFFFu) + (sum >> 16);
    }
    return static_cast<uint16_t>(~sum);
}

/**
 * Incrementally updates an IPv4 checksum when TTL decreases by 1.
 * TTL is at byte offset 8 in IPv4 header (together with protocol at offset 9).
 */
inline uint16_t update_checksum_ttl_decrement(uint16_t old_csum, uint8_t old_ttl, uint8_t protocol) {
    uint16_t old_val = static_cast<uint16_t>((static_cast<uint16_t>(old_ttl) << 8) | protocol);
    uint16_t new_val = static_cast<uint16_t>((static_cast<uint16_t>(old_ttl - 1) << 8) | protocol);
    return update_checksum16(old_csum, host_to_net16(old_val), host_to_net16(new_val));
}

} // namespace minirouter
