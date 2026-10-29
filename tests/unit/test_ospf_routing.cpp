#include <gtest/gtest.h>
#include "minirouter/routing/ospf_message.hpp"
#include "minirouter/routing/lsdb.hpp"
#include "minirouter/routing/dijkstra.hpp"
#include "minirouter/routing/ospf_router.hpp"

using namespace minirouter;

TEST(OspfMessageTest, HelloSerializationAndParsing) {
    OspfHelloMessage original;
    original.header.router_id = IPv4Address::from_string("1.1.1.1").value();
    original.header.area_id = IPv4Address::from_string("0.0.0.0").value();
    original.netmask = IPv4Address::from_string("255.255.255.0").value();
    original.hello_interval_sec = 10;
    original.dead_interval_sec = 40;
    original.neighbors = {
        IPv4Address::from_string("2.2.2.2").value(),
        IPv4Address::from_string("3.3.3.3").value()
    };

    auto bytes = original.serialize();
    EXPECT_GT(bytes.size(), OSPF_HEADER_LEN);

    auto parsed = OspfHelloMessage::parse(bytes.data(), bytes.size());
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->header.router_id, original.header.router_id);
    EXPECT_EQ(parsed->netmask, original.netmask);
    EXPECT_EQ(parsed->hello_interval_sec, 10);
    EXPECT_EQ(parsed->dead_interval_sec, 40);
    ASSERT_EQ(parsed->neighbors.size(), 2u);
    EXPECT_EQ(parsed->neighbors[0], IPv4Address::from_string("2.2.2.2").value());
    EXPECT_EQ(parsed->neighbors[1], IPv4Address::from_string("3.3.3.3").value());
}

TEST(OspfMessageTest, LsuSerializationAndParsing) {
    OspfLsuMessage original;
    original.header.router_id = IPv4Address::from_string("10.0.0.1").value();

    OspfLsa lsa1;
    lsa1.adv_router = IPv4Address::from_string("10.0.0.1").value();
    lsa1.seq_num = 100;
    lsa1.age_sec = 5;

    OspfLink link1;
    link1.neighbor_id = IPv4Address::from_string("10.0.0.2").value();
    link1.subnet = IPv4Prefix::from_string("192.168.10.0/24").value();
    link1.metric = 15;
    lsa1.links.push_back(link1);

    original.lsas.push_back(lsa1);

    auto bytes = original.serialize();
    auto parsed = OspfLsuMessage::parse(bytes.data(), bytes.size());
    ASSERT_TRUE(parsed.has_value());
    ASSERT_EQ(parsed->lsas.size(), 1u);
    EXPECT_EQ(parsed->lsas[0].adv_router, lsa1.adv_router);
    EXPECT_EQ(parsed->lsas[0].seq_num, 100u);
    ASSERT_EQ(parsed->lsas[0].links.size(), 1u);
    EXPECT_EQ(parsed->lsas[0].links[0].neighbor_id, link1.neighbor_id);
    EXPECT_EQ(parsed->lsas[0].links[0].subnet, link1.subnet);
    EXPECT_EQ(parsed->lsas[0].links[0].metric, 15u);
}

TEST(LsdbTest, SequenceNumberAndDuplicateSuppression) {
    Lsdb lsdb;
    RouterId r1 = IPv4Address::from_string("1.1.1.1").value();

    OspfLsa lsa_v1;
    lsa_v1.adv_router = r1;
    lsa_v1.seq_num = 1;

    EXPECT_TRUE(lsdb.install_lsa(lsa_v1));
    EXPECT_EQ(lsdb.size(), 1u);

    // Duplicate LSA (same sequence number) -> rejected
    EXPECT_FALSE(lsdb.install_lsa(lsa_v1));

    // Stale LSA (older sequence number) -> rejected
    OspfLsa lsa_stale = lsa_v1;
    lsa_stale.seq_num = 0;
    EXPECT_FALSE(lsdb.install_lsa(lsa_stale));

    // Newer LSA (higher sequence number) -> accepted
    OspfLsa lsa_v2 = lsa_v1;
    lsa_v2.seq_num = 2;
    EXPECT_TRUE(lsdb.install_lsa(lsa_v2));
    EXPECT_EQ(lsdb.get_lsa(r1)->seq_num, 2u);
}

TEST(DijkstraTest, ShortestPathTriangleTopology) {
    // Topology:
    // R1 (local) --- cost 50 --- R3
    //   \                         /
    // cost 10                 cost 10
    //     \                     /
    //      +------- R2 --------+
    // R3 advertises subnet 172.16.0.0/24.
    // Shortest path from R1 to R3 should go through R2 (10 + 10 = 20 < 50).

    RouterId r1 = IPv4Address::from_string("1.1.1.1").value();
    RouterId r2 = IPv4Address::from_string("2.2.2.2").value();
    RouterId r3 = IPv4Address::from_string("3.3.3.3").value();

    IPv4Prefix r3_subnet = IPv4Prefix::from_string("172.16.0.0/24").value();

    // Direct neighbors of R1
    std::vector<DirectNeighborLink> direct = {
        {r2, 1, IPv4Address::from_string("10.0.12.2").value(), 10}, // Port 1 to R2, metric 10
        {r3, 2, IPv4Address::from_string("10.0.13.3").value(), 50}  // Port 2 to R3, metric 50
    };

    // LSDB
    OspfLsa lsa_r1;
    lsa_r1.adv_router = r1;
    lsa_r1.links = {
        {r2, IPv4Prefix::from_string("10.0.12.0/24").value(), 10},
        {r3, IPv4Prefix::from_string("10.0.13.0/24").value(), 50}
    };

    OspfLsa lsa_r2;
    lsa_r2.adv_router = r2;
    lsa_r2.links = {
        {r1, IPv4Prefix::from_string("10.0.12.0/24").value(), 10},
        {r3, IPv4Prefix::from_string("10.0.23.0/24").value(), 10}
    };

    OspfLsa lsa_r3;
    lsa_r3.adv_router = r3;
    lsa_r3.links = {
        {r1, IPv4Prefix::from_string("10.0.13.0/24").value(), 50},
        {r2, IPv4Prefix::from_string("10.0.23.0/24").value(), 10},
        {RouterId(0), r3_subnet, 0} // Stub subnet
    };

    std::vector<OspfLsa> lsdb = {lsa_r1, lsa_r2, lsa_r3};

    auto routes = Dijkstra::calculate_spf(r1, lsdb, direct);

    bool found_r3_subnet = false;
    for (const auto& rt : routes) {
        if (rt.subnet == r3_subnet) {
            found_r3_subnet = true;
            EXPECT_EQ(rt.cost, 20u); // 10 + 10
            EXPECT_EQ(rt.egress_port, 1u); // Egress via Port 1 (towards R2)
            EXPECT_EQ(rt.next_hop_ip, IPv4Address::from_string("10.0.12.2").value());
        }
    }
    EXPECT_TRUE(found_r3_subnet);
}
