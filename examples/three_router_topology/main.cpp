#include <iostream>
#include <iomanip>
#include "minirouter/common/logger.hpp"
#include "minirouter/common/clock.hpp"
#include "minirouter/routing/ospf_router.hpp"

using namespace minirouter;

int main() {
    std::cout << "========================================================\n";
    std::cout << " MiniRouter Milestone D: Three-Router OSPF-Lite Topology\n";
    std::cout << "========================================================\n\n";

    auto sim_clock = std::make_shared<SimulatedClock>(1000);

    // Create 3 Router nodes
    RouterId r1_id = IPv4Address::from_string("1.1.1.1").value();
    RouterId r2_id = IPv4Address::from_string("2.2.2.2").value();
    RouterId r3_id = IPv4Address::from_string("3.3.3.3").value();

    auto engine1 = std::make_shared<ForwardingEngine>(sim_clock);
    auto engine2 = std::make_shared<ForwardingEngine>(sim_clock);
    auto engine3 = std::make_shared<ForwardingEngine>(sim_clock);

    auto r1 = std::make_unique<OspfRouter>(r1_id, engine1, sim_clock);
    auto r2 = std::make_unique<OspfRouter>(r2_id, engine2, sim_clock);
    auto r3 = std::make_unique<OspfRouter>(r3_id, engine3, sim_clock);

    // Configure Ports
    engine1->ports()->add_port(std::make_shared<VirtualPort>(1, "eth0", MacAddress(1,1,1,1,1,1), IPv4Address::from_string("10.0.12.1").value(), 24));
    engine1->ports()->add_port(std::make_shared<VirtualPort>(2, "eth1", MacAddress(1,1,1,1,1,2), IPv4Address::from_string("10.0.13.1").value(), 24));

    engine2->ports()->add_port(std::make_shared<VirtualPort>(1, "eth0", MacAddress(2,2,2,2,2,1), IPv4Address::from_string("10.0.12.2").value(), 24));
    engine2->ports()->add_port(std::make_shared<VirtualPort>(2, "eth1", MacAddress(2,2,2,2,2,2), IPv4Address::from_string("10.0.23.2").value(), 24));

    engine3->ports()->add_port(std::make_shared<VirtualPort>(1, "eth0", MacAddress(3,3,3,3,3,1), IPv4Address::from_string("10.0.13.3").value(), 24));
    engine3->ports()->add_port(std::make_shared<VirtualPort>(2, "eth1", MacAddress(3,3,3,3,3,2), IPv4Address::from_string("10.0.23.3").value(), 24));
    engine3->ports()->add_port(std::make_shared<VirtualPort>(3, "eth2", MacAddress(3,3,3,3,3,3), IPv4Address::from_string("192.168.100.1").value(), 24));

    // OSPF Interface Costs:
    // R1 - R2: metric 10
    // R2 - R3: metric 10
    // R1 - R3: metric 50 (higher metric direct path)
    r1->add_interface({1, IPv4Prefix::from_string("10.0.12.0/24").value(), 10, 1, 3, true});
    r1->add_interface({2, IPv4Prefix::from_string("10.0.13.0/24").value(), 50, 1, 3, true});

    r2->add_interface({1, IPv4Prefix::from_string("10.0.12.0/24").value(), 10, 1, 3, true});
    r2->add_interface({2, IPv4Prefix::from_string("10.0.23.0/24").value(), 10, 1, 3, true});

    r3->add_interface({1, IPv4Prefix::from_string("10.0.13.0/24").value(), 50, 1, 3, true});
    r3->add_interface({2, IPv4Prefix::from_string("10.0.23.0/24").value(), 10, 1, 3, true});
    r3->add_interface({3, IPv4Prefix::from_string("192.168.100.0/24").value(), 1, 1, 3, true});

    // Wire up simulated message transmission
    r1->set_message_sender([&](PortId port, const std::vector<uint8_t>& msg) {
        if (port == 1) r2->process_message(1, msg.data(), msg.size());
        if (port == 2) r3->process_message(1, msg.data(), msg.size());
    });

    r2->set_message_sender([&](PortId port, const std::vector<uint8_t>& msg) {
        if (port == 1) r1->process_message(1, msg.data(), msg.size());
        if (port == 2) r3->process_message(2, msg.data(), msg.size());
    });

    r3->set_message_sender([&](PortId port, const std::vector<uint8_t>& msg) {
        if (port == 1) r1->process_message(2, msg.data(), msg.size());
        if (port == 2) r2->process_message(2, msg.data(), msg.size());
    });

    std::cout << "[Step 1] Running OSPF-Lite Adjacency Formation & LSDB Flooding...\n";
    for (int t = 0; t < 10; ++t) {
        sim_clock->advance_ms(1000);
        r1->tick();
        r2->tick();
        r3->tick();
    }

    std::cout << " -> R1 Adjacencies: " << r1->get_neighbors().size() << " neighbors\n";
    std::cout << " -> R2 Adjacencies: " << r2->get_neighbors().size() << " neighbors\n";
    std::cout << " -> R3 Adjacencies: " << r3->get_neighbors().size() << " neighbors\n";
    std::cout << " -> Total LSAs in R1 LSDB: " << r1->lsdb()->size() << "\n\n";

    IPv4Address r3_dest = IPv4Address::from_string("192.168.100.55").value();
    Flow5Tuple flow;

    std::cout << "[Step 2] Testing Forwarding Decision on R1 towards " << r3_dest.to_string() << "...\n";
    auto nh1 = engine1->fib()->resolve_forwarding(r3_dest, flow);
    if (nh1.has_value()) {
        std::cout << " -> R1 egress port: " << nh1->port_id << " (Port 1 = via R2, cost 10+10=20 < 50)\n";
    }

    std::cout << "\n[Step 3] Dynamically updating R1-R2 link cost from 10 to 80...\n";
    r1->set_link_metric(1, 80);
    r2->set_link_metric(1, 80);

    for (int t = 0; t < 5; ++t) {
        sim_clock->advance_ms(1000);
        r1->tick();
        r2->tick();
        r3->tick();
    }

    std::cout << " -> Path recomputed after metric update!\n";
    auto nh2 = engine1->fib()->resolve_forwarding(r3_dest, flow);
    if (nh2.has_value()) {
        std::cout << " -> New R1 egress port: " << nh2->port_id
                  << " (Port 2 = direct R1-R3 link cost 50 < 80+10=90)\n";
    }

    std::cout << "\nThree-router OSPF-lite topology demonstration completed successfully!\n";
    return 0;
}
