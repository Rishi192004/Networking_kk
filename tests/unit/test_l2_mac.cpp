#include <gtest/gtest.h>
#include "minirouter/dataplane/port.hpp"
#include "minirouter/dataplane/port_manager.hpp"
#include "minirouter/dataplane/mac_table.hpp"

using namespace minirouter;

class MacTableTest : public ::testing::Test {
protected:
    void SetUp() override {
        clock = std::make_shared<SimulatedClock>(1000);
        mac_table = std::make_unique<MacTable>(5000); // 5 seconds aging
    }

    std::shared_ptr<SimulatedClock> clock;
    std::unique_ptr<MacTable> mac_table;
};

TEST_F(MacTableTest, LearnAndLookup) {
    auto mac1 = MacAddress::from_string("00:11:22:33:44:01").value();
    auto mac2 = MacAddress::from_string("00:11:22:33:44:02").value();

    EXPECT_EQ(mac_table->learn(mac1, 1, clock->now_ms()), ResultCode::SUCCESS);
    EXPECT_EQ(mac_table->size(), 1u);

    auto port_lookup = mac_table->lookup(mac1);
    ASSERT_TRUE(port_lookup.has_value());
    EXPECT_EQ(*port_lookup, 1u);

    // Lookup non-existent MAC
    EXPECT_FALSE(mac_table->lookup(mac2).has_value());

    auto stats = mac_table->get_stats();
    EXPECT_EQ(stats.total_learns, 1u);
    EXPECT_EQ(stats.total_lookups, 2u);
    EXPECT_EQ(stats.lookup_hits, 1u);
    EXPECT_EQ(stats.lookup_misses, 1u);
}

TEST_F(MacTableTest, PortMigration) {
    auto mac = MacAddress::from_string("00:11:22:33:44:55").value();

    // Learned on Port 1
    EXPECT_EQ(mac_table->learn(mac, 1, clock->now_ms()), ResultCode::SUCCESS);
    EXPECT_EQ(mac_table->lookup(mac), 1u);

    // Migrated to Port 2
    clock->advance_ms(500);
    EXPECT_EQ(mac_table->learn(mac, 2, clock->now_ms()), ResultCode::SUCCESS);
    EXPECT_EQ(mac_table->lookup(mac), 2u);
}

TEST_F(MacTableTest, InvalidateBroadcastAndMulticastLearn) {
    auto bcast = MacAddress::broadcast();
    auto mcast = MacAddress(0x01, 0x00, 0x5e, 0x00, 0x00, 0x01);
    auto zero = MacAddress(0x00, 0x00, 0x00, 0x00, 0x00, 0x00);

    EXPECT_EQ(mac_table->learn(bcast, 1, clock->now_ms()), ResultCode::ERROR_INVALID_PARAM);
    EXPECT_EQ(mac_table->learn(mcast, 1, clock->now_ms()), ResultCode::ERROR_INVALID_PARAM);
    EXPECT_EQ(mac_table->learn(zero, 1, clock->now_ms()), ResultCode::ERROR_INVALID_PARAM);
    EXPECT_EQ(mac_table->size(), 0u);
}

TEST_F(MacTableTest, StaticEntryDoesNotAge) {
    auto mac = MacAddress::from_string("aa:bb:cc:dd:ee:ff").value();
    EXPECT_EQ(mac_table->add_static(mac, 3), ResultCode::SUCCESS);

    // Advance time beyond aging threshold
    clock->advance_ms(10000);
    size_t aged = mac_table->age_entries(clock->now_ms());
    EXPECT_EQ(aged, 0u);
    EXPECT_EQ(mac_table->lookup(mac), 3u);
}

TEST_F(MacTableTest, DynamicEntryAging) {
    auto mac1 = MacAddress::from_string("00:00:00:00:00:01").value();
    auto mac2 = MacAddress::from_string("00:00:00:00:00:02").value();

    mac_table->learn(mac1, 1, clock->now_ms()); // t = 1000
    clock->advance_ms(3000);                     // t = 4000
    mac_table->learn(mac2, 2, clock->now_ms()); // t = 4000

    clock->advance_ms(3000); // t = 7000 (mac1 is 6000ms old > 5000ms threshold; mac2 is 3000ms old)
    size_t aged = mac_table->age_entries(clock->now_ms());
    EXPECT_EQ(aged, 1u);
    EXPECT_FALSE(mac_table->lookup(mac1).has_value());
    EXPECT_TRUE(mac_table->lookup(mac2).has_value());

    // Advance further to age mac2
    clock->advance_ms(3000); // t = 10000
    aged = mac_table->age_entries(clock->now_ms());
    EXPECT_EQ(aged, 1u);
    EXPECT_FALSE(mac_table->lookup(mac2).has_value());
    EXPECT_EQ(mac_table->size(), 0u);
}

TEST(PortManagerTest, PortOperationsAndQueues) {
    PortManager pm;
    auto mac1 = MacAddress::from_string("00:aa:bb:cc:dd:01").value();
    auto mac2 = MacAddress::from_string("00:aa:bb:cc:dd:02").value();

    auto p1 = std::make_shared<VirtualPort>(1, "eth0", mac1);
    auto p2 = std::make_shared<VirtualPort>(2, "eth1", mac2);

    EXPECT_EQ(pm.add_port(p1), ResultCode::SUCCESS);
    EXPECT_EQ(pm.add_port(p2), ResultCode::SUCCESS);
    EXPECT_EQ(pm.add_port(p1), ResultCode::ERROR_ALREADY_EXISTS);
    EXPECT_EQ(pm.port_count(), 2u);

    // Queue test
    PacketBuffer pkt({0x01, 0x02, 0x03}, 3);
    p1->enqueue_rx(pkt);
    EXPECT_EQ(p1->rx_queue_size(), 1u);

    auto deq = p1->dequeue_rx();
    ASSERT_TRUE(deq.has_value());
    EXPECT_EQ(deq->ingress_port(), 1u);
    EXPECT_EQ(deq->size(), 3u);
    EXPECT_EQ(p1->rx_queue_size(), 0u);

    // Stats test
    p1->record_rx(100);
    p1->record_tx(200);
    p1->record_drop(50);
    p1->record_flood();

    auto stats = p1->get_stats();
    EXPECT_EQ(stats.rx_packets, 1u);
    EXPECT_EQ(stats.rx_bytes, 100u);
    EXPECT_EQ(stats.tx_packets, 1u);
    EXPECT_EQ(stats.tx_bytes, 200u);
    EXPECT_EQ(stats.drop_packets, 1u);
    EXPECT_EQ(stats.drop_bytes, 50u);
    EXPECT_EQ(stats.flood_packets, 1u);
}
