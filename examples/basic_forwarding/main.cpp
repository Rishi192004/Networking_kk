#include <iostream>
#include <iomanip>
#include <memory>
#include "minirouter/common/logger.hpp"
#include "minirouter/common/clock.hpp"
#include "minirouter/packet/ethernet.hpp"
#include "minirouter/dataplane/port.hpp"
#include "minirouter/dataplane/port_manager.hpp"
#include "minirouter/dataplane/mac_table.hpp"

using namespace minirouter;

int main() {
    std::cout << "========================================\n";
    std::cout << " MiniRouter Milestone B: L2 Demo\n";
    std::cout << "========================================\n\n";

    auto clock = std::make_shared<SimulatedClock>(1000);
    PortManager port_mgr;
    MacTable mac_table(30000); // 30s aging

    // Create 3 virtual ports
    auto port1 = std::make_shared<VirtualPort>(1, "eth0", MacAddress(0x02, 0x00, 0x00, 0x00, 0x00, 0x01));
    auto port2 = std::make_shared<VirtualPort>(2, "eth1", MacAddress(0x02, 0x00, 0x00, 0x00, 0x00, 0x02));
    auto port3 = std::make_shared<VirtualPort>(3, "eth2", MacAddress(0x02, 0x00, 0x00, 0x00, 0x00, 0x03));

    port_mgr.add_port(port1);
    port_mgr.add_port(port2);
    port_mgr.add_port(port3);

    std::cout << "Configured " << port_mgr.port_count() << " virtual ports:\n";
    for (const auto& p : port_mgr.get_all_ports()) {
        std::cout << " - Port " << p->id() << " (" << p->name() << "): MAC " << p->mac().to_string() << "\n";
    }
    std::cout << "\n";

    MacAddress hostA = MacAddress::from_string("00:50:56:c0:00:01").value();
    MacAddress hostB = MacAddress::from_string("00:50:56:c0:00:02").value();

    std::cout << "[Step 1] Ingress frame from Host A (" << hostA.to_string() 
              << ") on Port 1 destined to unknown Host B (" << hostB.to_string() << ")...\n";

    // Learn source MAC on ingress port
    mac_table.learn(hostA, port1->id(), clock->now_ms());
    port1->record_rx(64);

    // Forwarding decision for Host B
    auto target_port = mac_table.lookup(hostB);
    if (!target_port.has_value()) {
        std::cout << " -> MAC " << hostB.to_string() << " unknown! Flooding to all eligible ports (eth1, eth2)...\n";
        port1->record_flood();
        port2->record_tx(64);
        port3->record_tx(64);
    } else {
        std::cout << " -> Forwarding to Port " << *target_port << "\n";
    }

    std::cout << "\n[Step 2] Host B replies on Port 2 to Host A...\n";
    // Learn Host B on Port 2
    mac_table.learn(hostB, port2->id(), clock->now_ms());
    port2->record_rx(64);

    // Forwarding decision for Host A
    target_port = mac_table.lookup(hostA);
    if (target_port.has_value()) {
        std::cout << " -> MAC " << hostA.to_string() << " learned on Port " << *target_port 
                  << " (eth0). Unicast forwarding without flooding!\n";
        auto egress = port_mgr.get_port(*target_port);
        if (egress) egress->record_tx(64);
    }

    std::cout << "\n[Step 3] Port statistics:\n";
    for (const auto& p : port_mgr.get_all_ports()) {
        auto st = p->get_stats();
        std::cout << " Port " << p->id() << " (" << p->name() << "):"
                  << " RX=" << st.rx_packets << " pkts (" << st.rx_bytes << " B)"
                  << ", TX=" << st.tx_packets << " pkts (" << st.tx_bytes << " B)"
                  << ", Flood=" << st.flood_packets << "\n";
    }

    std::cout << "\n[Step 4] Learned MAC Table Entries (" << mac_table.size() << " total):\n";
    for (const auto& entry : mac_table.get_all_entries()) {
        std::cout << " - MAC: " << entry.mac.to_string() << " -> Port " << entry.port_id
                  << " (last seen: " << entry.last_seen_ms << " ms)\n";
    }

    std::cout << "\nL2 Forwarding demo completed successfully!\n";
    return 0;
}
