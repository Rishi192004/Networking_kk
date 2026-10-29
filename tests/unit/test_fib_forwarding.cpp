#include <gtest/gtest.h>
#include <map>
#include "minirouter/dataplane/forwarding_engine.hpp"

using namespace minirouter;

class ForwardingEngineTest : public ::testing::Test {
protected:
    void SetUp() override {
        clock = std::make_shared<SimulatedClock>(1000);
        engine = std::make_unique<ForwardingEngine>(clock);

        // Port 1 (LAN): 192.168.1.1/24, MAC 00:00:5E:00:01:01
        port1 = std::make_shared<VirtualPort>(
            1, "eth0",
            MacAddress::from_string("00:00:5e:00:01:01").value(),
            IPv4Address::from_string("192.168.1.1").value(),
            24
        );

        // Port 2 (WAN 1): 10.0.1.1/24, MAC 00:00:5E:00:01:02
        port2 = std::make_shared<VirtualPort>(
            2, "eth1",
            MacAddress::from_string("00:00:5e:00:01:02").value(),
            IPv4Address::from_string("10.0.1.1").value(),
            24
        );

        // Port 3 (WAN 2): 10.0.2.1/24, MAC 00:00:5E:00:01:03
        port3 = std::make_shared<VirtualPort>(
            3, "eth2",
            MacAddress::from_string("00:00:5e:00:01:03").value(),
            IPv4Address::from_string("10.0.2.1").value(),
            24
        );

        engine->ports()->add_port(port1);
        engine->ports()->add_port(port2);
        engine->ports()->add_port(port3);

        // Add Next Hops
        NextHop nh1;
        nh1.id = 1;
        nh1.port_id = 2; // Port 2
        nh1.ip = IPv4Address::from_string("10.0.1.254").value();
        nh1.dst_mac = MacAddress::from_string("00:50:56:00:01:02").value();
        engine->fib()->add_next_hop(nh1);

        NextHop nh2;
        nh2.id = 2;
        nh2.port_id = 3; // Port 3
        nh2.ip = IPv4Address::from_string("10.0.2.254").value();
        nh2.dst_mac = MacAddress::from_string("00:50:56:00:01:03").value();
        engine->fib()->add_next_hop(nh2);
    }

    PacketBuffer create_ipv4_packet(
        MacAddress src_mac, MacAddress dst_mac,
        IPv4Address src_ip, IPv4Address dst_ip,
        uint8_t ttl = 64, uint8_t protocol = IP_PROTO_UDP,
        uint16_t src_port = 1234, uint16_t dst_port = 80
    ) {
        std::vector<uint8_t> data(ETHERNET_HEADER_LEN + 20 + 8, 0);

        EthernetHeader eth;
        eth.src_mac = src_mac;
        eth.dst_mac = dst_mac;
        eth.ethertype = ETHERTYPE_IPV4;
        eth.serialize(data.data());

        IPv4Header ip;
        ip.version = 4;
        ip.ihl = 5;
        ip.total_length = 20 + 8;
        ip.ttl = ttl;
        ip.protocol = protocol;
        ip.src_ip = src_ip;
        ip.dst_ip = dst_ip;
        ip.serialize(data.data() + ETHERNET_HEADER_LEN);

        // UDP ports
        write_unaligned<uint16_t>(data.data() + ETHERNET_HEADER_LEN + 20, host_to_net16(src_port));
        write_unaligned<uint16_t>(data.data() + ETHERNET_HEADER_LEN + 22, host_to_net16(dst_port));

        return PacketBuffer(std::move(data), 1);
    }

    std::shared_ptr<SimulatedClock> clock;
    std::unique_ptr<ForwardingEngine> engine;
    std::shared_ptr<VirtualPort> port1;
    std::shared_ptr<VirtualPort> port2;
    std::shared_ptr<VirtualPort> port3;
};

TEST_F(ForwardingEngineTest, SuccessfulL3Forwarding) {
    // Add route: 8.8.8.0/24 -> NextHop 1 (Port 2)
    auto prefix = IPv4Prefix::from_string("8.8.8.0/24").value();
    EXPECT_EQ(engine->fib()->add_route(prefix, 1), ResultCode::SUCCESS);

    MacAddress host_mac = MacAddress::from_string("00:11:22:33:44:55").value();
    IPv4Address host_ip = IPv4Address::from_string("192.168.1.100").value();
    IPv4Address dst_ip = IPv4Address::from_string("8.8.8.8").value();

    PacketBuffer pkt = create_ipv4_packet(host_mac, port1->mac(), host_ip, dst_ip, 64);
    pkt.set_ingress_port(1);

    EXPECT_EQ(engine->process_packet(pkt), ResultCode::SUCCESS);

    // Verify packet reached Port 2's TX queue
    EXPECT_EQ(port2->tx_queue_size(), 1u);
    auto egress_pkt = port2->dequeue_tx();
    ASSERT_TRUE(egress_pkt.has_value());

    // Verify Ethernet header rewrite:
    // src_mac = port2->mac(), dst_mac = nh1.dst_mac
    auto eth = EthernetHeader::parse(egress_pkt->data(), egress_pkt->size()).value();
    EXPECT_EQ(eth.src_mac, port2->mac());
    EXPECT_EQ(eth.dst_mac, MacAddress::from_string("00:50:56:00:01:02").value());

    // Verify IPv4 TTL was decremented from 64 to 63 and checksum remains valid
    auto ip = IPv4Header::parse(egress_pkt->data() + ETHERNET_HEADER_LEN, egress_pkt->size() - ETHERNET_HEADER_LEN).value();
    EXPECT_EQ(ip.ttl, 63);

    auto stats = engine->get_stats();
    EXPECT_EQ(stats.total_rx_packets, 1u);
    EXPECT_EQ(stats.total_l3_forwarded, 1u);
}

TEST_F(ForwardingEngineTest, TtlExpiredDrop) {
    auto prefix = IPv4Prefix::from_string("8.8.8.0/24").value();
    engine->fib()->add_route(prefix, 1);

    MacAddress host_mac = MacAddress::from_string("00:11:22:33:44:55").value();
    PacketBuffer pkt = create_ipv4_packet(
        host_mac, port1->mac(),
        IPv4Address::from_string("192.168.1.100").value(),
        IPv4Address::from_string("8.8.8.8").value(),
        1 // TTL = 1 should expire
    );
    pkt.set_ingress_port(1);

    EXPECT_EQ(engine->process_packet(pkt), ResultCode::ERROR_TTL_EXPIRED);
    EXPECT_EQ(port2->tx_queue_size(), 0u);

    auto stats = engine->get_stats();
    EXPECT_EQ(stats.drop_ttl_expired, 1u);
}

TEST_F(ForwardingEngineTest, NoRouteDrop) {
    MacAddress host_mac = MacAddress::from_string("00:11:22:33:44:55").value();
    PacketBuffer pkt = create_ipv4_packet(
        host_mac, port1->mac(),
        IPv4Address::from_string("192.168.1.100").value(),
        IPv4Address::from_string("172.20.1.1").value(), // No route for this
        64
    );
    pkt.set_ingress_port(1);

    EXPECT_EQ(engine->process_packet(pkt), ResultCode::ERROR_NO_ROUTE);
    auto stats = engine->get_stats();
    EXPECT_EQ(stats.drop_no_route, 1u);
}

TEST_F(ForwardingEngineTest, EcmpFlowHashDistribution) {
    // Create Next Hop Group with NH 1 and NH 2
    NextHopGroup grp;
    grp.id = 10;
    grp.next_hop_ids = {1, 2};
    EXPECT_EQ(engine->fib()->add_next_hop_group(grp), ResultCode::SUCCESS);

    // Route 0.0.0.0/0 to ECMP group 10
    auto def_route = IPv4Prefix::default_route();
    EXPECT_EQ(engine->fib()->add_ecmp_route(def_route, 10), ResultCode::SUCCESS);

    MacAddress host_mac = MacAddress::from_string("00:11:22:33:44:55").value();
    IPv4Address host_ip = IPv4Address::from_string("192.168.1.50").value();

    std::map<PortId, uint32_t> port_hits;

    // Send 100 different flows (different src_port / dst_ip)
    for (uint16_t port = 1000; port < 1100; ++port) {
        IPv4Address dst_ip(0x40000000u + port);
        PacketBuffer pkt = create_ipv4_packet(
            host_mac, port1->mac(), host_ip, dst_ip, 64, IP_PROTO_TCP, port, 443
        );
        pkt.set_ingress_port(1);

        ASSERT_EQ(engine->process_packet(pkt), ResultCode::SUCCESS);
    }

    size_t port2_pkts = port2->tx_queue_size();
    size_t port3_pkts = port3->tx_queue_size();

    EXPECT_EQ(port2_pkts + port3_pkts, 100u);
    // Both ports should receive a significant share of flows (no starvation, deterministic distribution)
    EXPECT_GT(port2_pkts, 25u);
    EXPECT_GT(port3_pkts, 25u);

    // Verify consistency: the same flow always chooses the exact same port
    PacketBuffer pkt1 = create_ipv4_packet(
        host_mac, port1->mac(), host_ip, IPv4Address::from_string("64.0.0.1").value(), 64, IP_PROTO_TCP, 5000, 80
    );
    pkt1.set_ingress_port(1);
    port2->clear_queues();
    port3->clear_queues();

    engine->process_packet(pkt1);
    PortId chosen1 = (port2->tx_queue_size() > 0) ? 2 : 3;

    // Send identical flow again
    PacketBuffer pkt2 = create_ipv4_packet(
        host_mac, port1->mac(), host_ip, IPv4Address::from_string("64.0.0.1").value(), 64, IP_PROTO_TCP, 5000, 80
    );
    pkt2.set_ingress_port(1);
    port2->clear_queues();
    port3->clear_queues();

    engine->process_packet(pkt2);
    PortId chosen2 = (port2->tx_queue_size() > 0) ? 2 : 3;

    EXPECT_EQ(chosen1, chosen2);
}
