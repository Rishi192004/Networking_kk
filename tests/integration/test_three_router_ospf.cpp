#include <gtest/gtest.h>
#include <map>
#include "minirouter/routing/ospf_router.hpp"

using namespace minirouter;

class ThreeRouterOspfSimulation : public ::testing::Test {
protected:
    void SetUp() override {
        sim_clock = std::make_shared<SimulatedClock>(1000);

        // Build 3 routers: R1, R2, R3
        r1_id = IPv4Address::from_string("1.1.1.1").value();
        r2_id = IPv4Address::from_string("2.2.2.2").value();
        r3_id = IPv4Address::from_string("3.3.3.3").value();

        engine1 = std::make_shared<ForwardingEngine>(sim_clock);
        engine2 = std::make_shared<ForwardingEngine>(sim_clock);
        engine3 = std::make_shared<ForwardingEngine>(sim_clock);

        r1 = std::make_unique<OspfRouter>(r1_id, engine1, sim_clock);
        r2 = std::make_unique<OspfRouter>(r2_id, engine2, sim_clock);
        r3 = std::make_unique<OspfRouter>(r3_id, engine3, sim_clock);

        // Add virtual ports
        // R1: Port 1 (to R2), Port 2 (to R3)
        // R2: Port 1 (to R1), Port 2 (to R3)
        // R3: Port 1 (to R1), Port 2 (to R2), Port 3 (Local LAN: 192.168.100.0/24)
        auto r1_p1 = std::make_shared<VirtualPort>(1, "eth0", MacAddress(1,1,1,1,1,1), IPv4Address::from_string("10.0.12.1").value(), 24);
        auto r1_p2 = std::make_shared<VirtualPort>(2, "eth1", MacAddress(1,1,1,1,1,2), IPv4Address::from_string("10.0.13.1").value(), 24);
        engine1->ports()->add_port(r1_p1);
        engine1->ports()->add_port(r1_p2);

        auto r2_p1 = std::make_shared<VirtualPort>(1, "eth0", MacAddress(2,2,2,2,2,1), IPv4Address::from_string("10.0.12.2").value(), 24);
        auto r2_p2 = std::make_shared<VirtualPort>(2, "eth1", MacAddress(2,2,2,2,2,2), IPv4Address::from_string("10.0.23.2").value(), 24);
        engine2->ports()->add_port(r2_p1);
        engine2->ports()->add_port(r2_p2);

        auto r3_p1 = std::make_shared<VirtualPort>(1, "eth0", MacAddress(3,3,3,3,3,1), IPv4Address::from_string("10.0.13.3").value(), 24);
        auto r3_p2 = std::make_shared<VirtualPort>(2, "eth1", MacAddress(3,3,3,3,3,2), IPv4Address::from_string("10.0.23.3").value(), 24);
        auto r3_lan = std::make_shared<VirtualPort>(3, "eth2", MacAddress(3,3,3,3,3,3), IPv4Address::from_string("192.168.100.1").value(), 24);
        engine3->ports()->add_port(r3_p1);
        engine3->ports()->add_port(r3_p2);
        engine3->ports()->add_port(r3_lan);

        // OSPF interfaces
        // R1: Link to R2 (cost 10), Link to R3 (cost 100 - expensive backup link)
        r1->add_interface({1, IPv4Prefix::from_string("10.0.12.0/24").value(), 10, 1, 3, true});
        r1->add_interface({2, IPv4Prefix::from_string("10.0.13.0/24").value(), 100, 1, 3, true});

        // R2: Link to R1 (cost 10), Link to R3 (cost 10)
        r2->add_interface({1, IPv4Prefix::from_string("10.0.12.0/24").value(), 10, 1, 3, true});
        r2->add_interface({2, IPv4Prefix::from_string("10.0.23.0/24").value(), 10, 1, 3, true});

        // R3: Link to R1 (cost 100), Link to R2 (cost 10), LAN (cost 1)
        r3->add_interface({1, IPv4Prefix::from_string("10.0.13.0/24").value(), 100, 1, 3, true});
        r3->add_interface({2, IPv4Prefix::from_string("10.0.23.0/24").value(), 10, 1, 3, true});
        r3->add_interface({3, IPv4Prefix::from_string("192.168.100.0/24").value(), 1, 1, 3, true});

        // Simulated inter-router wire transport
        r1->set_message_sender([this](PortId port, const std::vector<uint8_t>& msg) {
            if (port == 1 && r1_r2_link_up) r2->process_message(1, msg.data(), msg.size());
            if (port == 2 && r1_r3_link_up) r3->process_message(1, msg.data(), msg.size());
        });

        r2->set_message_sender([this](PortId port, const std::vector<uint8_t>& msg) {
            if (port == 1 && r1_r2_link_up) r1->process_message(1, msg.data(), msg.size());
            if (port == 2 && r2_r3_link_up) r3->process_message(2, msg.data(), msg.size());
        });

        r3->set_message_sender([this](PortId port, const std::vector<uint8_t>& msg) {
            if (port == 1 && r1_r3_link_up) r1->process_message(2, msg.data(), msg.size());
            if (port == 2 && r2_r3_link_up) r2->process_message(2, msg.data(), msg.size());
        });
    }

    void step_simulation(int rounds = 5) {
        for (int i = 0; i < rounds; ++i) {
            sim_clock->advance_ms(1000);
            r1->tick();
            r2->tick();
            r3->tick();
        }
    }

    std::shared_ptr<SimulatedClock> sim_clock;
    RouterId r1_id, r2_id, r3_id;
    std::shared_ptr<ForwardingEngine> engine1, engine2, engine3;
    std::unique_ptr<OspfRouter> r1, r2, r3;
    bool r1_r2_link_up{true};
    bool r2_r3_link_up{true};
    bool r1_r3_link_up{true};
};

TEST_F(ThreeRouterOspfSimulation, ConvergenceAndShortestPath) {
    // Run simulation to form adjacencies and exchange LSAs
    step_simulation(10);

    // Verify neighbors
    auto r1_nbrs = r1->get_neighbors();
    EXPECT_GE(r1_nbrs.size(), 2u);

    // Check R1's FIB for R3's LAN (192.168.100.0/24)
    auto target_ip = IPv4Address::from_string("192.168.100.50").value();
    Flow5Tuple dummy_flow;
    auto nh_opt = engine1->fib()->resolve_forwarding(target_ip, dummy_flow);

    ASSERT_TRUE(nh_opt.has_value());
    // Since R1-R2-R3 cost is 10 + 10 = 20, whereas R1-R3 direct is 100,
    // R1 should forward to Port 1 (towards R2)!
    EXPECT_EQ(nh_opt->port_id, 1u);

    // Dynamic Link Failure: Disable R1-R2 link
    r1_r2_link_up = false;
    r1->set_interface_enabled(1, false);
    r2->set_interface_enabled(1, false);

    // Advance clock so dead timers trigger and topology reconverges
    step_simulation(5);

    // R1 should failover to Port 2 (direct R1-R3 link with cost 100)
    nh_opt = engine1->fib()->resolve_forwarding(target_ip, dummy_flow);
    ASSERT_TRUE(nh_opt.has_value());
    EXPECT_EQ(nh_opt->port_id, 2u); // Failover succeeded!
}
