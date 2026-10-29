#include <gtest/gtest.h>
#include "minirouter/packet/ipv4.hpp"
#include "minirouter/packet/ethernet.hpp"
#include "minirouter/packet/checksum.hpp"
#include "minirouter/packet/packet_buffer.hpp"

using namespace minirouter;

TEST(IPv4AddressTest, ValidAddresses) {
    auto addr1 = IPv4Address::from_string("192.168.1.1");
    ASSERT_TRUE(addr1.has_value());
    EXPECT_EQ(addr1->to_string(), "192.168.1.1");
    EXPECT_EQ(addr1->octet(0), 192);
    EXPECT_EQ(addr1->octet(1), 168);
    EXPECT_EQ(addr1->octet(2), 1);
    EXPECT_EQ(addr1->octet(3), 1);

    auto addr_zero = IPv4Address::from_string("0.0.0.0");
    ASSERT_TRUE(addr_zero.has_value());
    EXPECT_EQ(addr_zero->to_string(), "0.0.0.0");
    EXPECT_EQ(addr_zero->to_host_order(), 0u);

    auto addr_bcast = IPv4Address::from_string("255.255.255.255");
    ASSERT_TRUE(addr_bcast.has_value());
    EXPECT_EQ(addr_bcast->to_string(), "255.255.255.255");
    EXPECT_EQ(addr_bcast->to_host_order(), 0xFFFFFFFFu);
}

TEST(IPv4AddressTest, InvalidAddresses) {
    EXPECT_FALSE(IPv4Address::from_string("").has_value());
    EXPECT_FALSE(IPv4Address::from_string("192.168.1").has_value());
    EXPECT_FALSE(IPv4Address::from_string("192.168.1.1.1").has_value());
    EXPECT_FALSE(IPv4Address::from_string("192.168.1.256").has_value());
    EXPECT_FALSE(IPv4Address::from_string("192.168.1.-1").has_value());
    EXPECT_FALSE(IPv4Address::from_string("192.168.1.abc").has_value());
    EXPECT_FALSE(IPv4Address::from_string("...").has_value());
}

TEST(IPv4AddressTest, ByteOrder) {
    IPv4Address addr(10, 20, 30, 40);
    uint32_t host_val = addr.to_host_order();
    uint32_t net_val = addr.to_network_order();

    EXPECT_EQ(host_val, 0x0A141E28u);
    EXPECT_EQ(net_val, host_to_net32(host_val));
    EXPECT_EQ(IPv4Address::from_network_order(net_val), addr);
}

TEST(IPv4PrefixTest, CIDRParsing) {
    auto p1 = IPv4Prefix::from_string("10.0.0.0/8");
    ASSERT_TRUE(p1.has_value());
    EXPECT_EQ(p1->prefix_len(), 8);
    EXPECT_EQ(p1->network().to_string(), "10.0.0.0");
    EXPECT_EQ(p1->mask(), 0xFF000000u);

    // Address host bits masked out
    auto p2 = IPv4Prefix::from_string("192.168.1.155/24");
    ASSERT_TRUE(p2.has_value());
    EXPECT_EQ(p2->prefix_len(), 24);
    EXPECT_EQ(p2->network().to_string(), "192.168.1.0");

    // Default route /0
    auto p_def = IPv4Prefix::from_string("0.0.0.0/0");
    ASSERT_TRUE(p_def.has_value());
    EXPECT_EQ(p_def->prefix_len(), 0);
    EXPECT_EQ(p_def->mask(), 0u);

    // Host route /32
    auto p_host = IPv4Prefix::from_string("172.16.0.1/32");
    ASSERT_TRUE(p_host.has_value());
    EXPECT_EQ(p_host->prefix_len(), 32);
    EXPECT_EQ(p_host->mask(), 0xFFFFFFFFu);
}

TEST(IPv4PrefixTest, InvalidPrefixes) {
    EXPECT_FALSE(IPv4Prefix::from_string("10.0.0.0/33").has_value());
    EXPECT_FALSE(IPv4Prefix::from_string("10.0.0.0/-1").has_value());
    EXPECT_FALSE(IPv4Prefix::from_string("10.0.0.0/").has_value());
    EXPECT_FALSE(IPv4Prefix::from_string("not_an_ip/24").has_value());
}

TEST(IPv4PrefixTest, ContainsAddress) {
    auto prefix = IPv4Prefix::from_string("10.10.0.0/16").value();
    EXPECT_TRUE(prefix.contains(IPv4Address::from_string("10.10.0.1").value()));
    EXPECT_TRUE(prefix.contains(IPv4Address::from_string("10.10.255.254").value()));
    EXPECT_FALSE(prefix.contains(IPv4Address::from_string("10.11.0.1").value()));
    EXPECT_FALSE(prefix.contains(IPv4Address::from_string("192.168.1.1").value()));

    auto def_route = IPv4Prefix::default_route();
    EXPECT_TRUE(def_route.contains(IPv4Address::from_string("1.2.3.4").value()));
    EXPECT_TRUE(def_route.contains(IPv4Address::from_string("224.0.0.5").value()));
}

TEST(EthernetTest, MacAddressParsingAndFormatting) {
    auto mac = MacAddress::from_string("00:1A:2B:3C:4D:5E");
    ASSERT_TRUE(mac.has_value());
    EXPECT_EQ(mac->to_string(), "00:1a:2b:3c:4d:5e");
    EXPECT_FALSE(mac->is_broadcast());
    EXPECT_FALSE(mac->is_multicast());

    auto bcast = MacAddress::broadcast();
    EXPECT_TRUE(bcast.is_broadcast());
    EXPECT_TRUE(bcast.is_multicast());

    auto mcast = MacAddress(0x01, 0x00, 0x5E, 0x00, 0x00, 0x01);
    EXPECT_TRUE(mcast.is_multicast());
    EXPECT_FALSE(mcast.is_broadcast());

    EXPECT_FALSE(MacAddress::from_string("00:1A:2B:3C:4D").has_value());
    EXPECT_FALSE(MacAddress::from_string("00:1A:2B:3C:4D:5E:6F").has_value());
    EXPECT_FALSE(MacAddress::from_string("00:1Z:2B:3C:4D:5E").has_value());
}

TEST(EthernetTest, HeaderSerializationAndParsing) {
    EthernetHeader original;
    original.src_mac = MacAddress::from_string("aa:bb:cc:dd:ee:01").value();
    original.dst_mac = MacAddress::from_string("aa:bb:cc:dd:ee:02").value();
    original.ethertype = ETHERTYPE_IPV4;

    uint8_t buffer[ETHERNET_HEADER_LEN];
    original.serialize(buffer);

    auto parsed = EthernetHeader::parse(buffer, sizeof(buffer));
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->src_mac, original.src_mac);
    EXPECT_EQ(parsed->dst_mac, original.dst_mac);
    EXPECT_EQ(parsed->ethertype, ETHERTYPE_IPV4);

    // Truncated buffer check
    EXPECT_FALSE(EthernetHeader::parse(buffer, ETHERNET_HEADER_LEN - 1).has_value());
}

TEST(ChecksumTest, BasicCalculationAndValidation) {
    // RFC 1071 sample data
    uint8_t data[] = {0x00, 0x01, 0xf2, 0x03, 0xf4, 0xf5, 0xf6, 0xf7};
    uint16_t csum = calculate_checksum(data, sizeof(data));

    // Append checksum to data block
    uint8_t verified_block[10];
    std::memcpy(verified_block, data, sizeof(data));
    write_unaligned<uint16_t>(verified_block + 8, csum);

    EXPECT_TRUE(validate_checksum(verified_block, sizeof(verified_block)));
}

TEST(ChecksumTest, IncrementalUpdate) {
    IPv4Header hdr;
    hdr.ttl = 64;
    hdr.protocol = IP_PROTO_TCP;
    hdr.src_ip = IPv4Address::from_string("192.168.1.1").value();
    hdr.dst_ip = IPv4Address::from_string("10.0.0.1").value();
    hdr.total_length = 20;

    uint8_t buffer[20];
    hdr.serialize(buffer);

    auto parsed1 = IPv4Header::parse(buffer, sizeof(buffer));
    ASSERT_TRUE(parsed1.has_value());
    uint16_t original_csum = parsed1->checksum;

    // Incremental update for TTL decrement
    uint16_t new_csum = update_checksum_ttl_decrement(host_to_net16(original_csum), 64, IP_PROTO_TCP);
    buffer[8] = 63; // Decrement TTL
    write_unaligned<uint16_t>(buffer + 10, new_csum);

    // Check that validate_checksum passes with the incrementally updated checksum
    EXPECT_TRUE(validate_checksum(buffer, sizeof(buffer)));

    // Re-verify against full calculation
    auto parsed2 = IPv4Header::parse(buffer, sizeof(buffer));
    ASSERT_TRUE(parsed2.has_value());
    EXPECT_EQ(parsed2->ttl, 63);
}

TEST(IPv4HeaderTest, SerializationAndParsing) {
    IPv4Header original;
    original.version = 4;
    original.ihl = 5;
    original.dscp = 0;
    original.ecn = 0;
    original.total_length = 60;
    original.identification = 12345;
    original.flags = 2; // Don't Fragment
    original.fragment_offset = 0;
    original.ttl = 64;
    original.protocol = IP_PROTO_UDP;
    original.src_ip = IPv4Address::from_string("192.168.10.1").value();
    original.dst_ip = IPv4Address::from_string("172.16.1.1").value();

    uint8_t packet[60];
    std::memset(packet, 0, sizeof(packet));
    original.serialize(packet);

    auto parsed = IPv4Header::parse(packet, sizeof(packet));
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->version, 4);
    EXPECT_EQ(parsed->ihl, 5);
    EXPECT_EQ(parsed->total_length, 60);
    EXPECT_EQ(parsed->identification, 12345);
    EXPECT_EQ(parsed->flags, 2);
    EXPECT_EQ(parsed->ttl, 64);
    EXPECT_EQ(parsed->protocol, IP_PROTO_UDP);
    EXPECT_EQ(parsed->src_ip, original.src_ip);
    EXPECT_EQ(parsed->dst_ip, original.dst_ip);
}

TEST(IPv4HeaderTest, MalformedAndTruncatedPackets) {
    uint8_t valid_packet[20];
    IPv4Header hdr;
    hdr.serialize(valid_packet);

    // Truncated length
    EXPECT_FALSE(IPv4Header::parse(valid_packet, 19).has_value());

    // Corrupted checksum
    uint8_t corrupted[20];
    std::memcpy(corrupted, valid_packet, 20);
    corrupted[10] ^= 0xFF; // Invert checksum byte
    EXPECT_FALSE(IPv4Header::parse(corrupted, 20).has_value());

    // Invalid version (e.g. IPv6)
    uint8_t wrong_ver[20];
    std::memcpy(wrong_ver, valid_packet, 20);
    wrong_ver[0] = 0x65; // Version 6
    EXPECT_FALSE(IPv4Header::parse(wrong_ver, 20).has_value());

    // Total length larger than buffer length
    uint8_t len_mismatch[20];
    std::memcpy(len_mismatch, valid_packet, 20);
    write_unaligned<uint16_t>(len_mismatch + 2, host_to_net16(100)); // claims 100 bytes
    // Recompute checksum so only length mismatch triggers
    write_unaligned<uint16_t>(len_mismatch + 10, 0);
    uint16_t csum = calculate_checksum(len_mismatch, 20);
    write_unaligned<uint16_t>(len_mismatch + 10, csum);
    EXPECT_FALSE(IPv4Header::parse(len_mismatch, 20).has_value());
}
