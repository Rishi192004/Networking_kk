#include "minirouter/dataplane/forwarding_engine.hpp"

namespace minirouter {

ResultCode ForwardingEngine::process_packet(PacketBuffer& pkt) {
    if (pkt.empty() || pkt.ingress_port() == INVALID_PORT_ID) {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.drop_malformed++;
        return ResultCode::ERROR_PACKET_MALFORMED;
    }

    auto ingress_port = port_mgr_->get_port(pkt.ingress_port());
    if (!ingress_port || !ingress_port->is_up()) {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.drop_interface_down++;
        return ResultCode::ERROR_INTERFACE_DOWN;
    }

    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.total_rx_packets++;
    }
    ingress_port->record_rx(pkt.size());

    // 1. Parse Ethernet Header
    auto eth_hdr = EthernetHeader::parse(pkt.data(), pkt.size());
    if (!eth_hdr.has_value()) {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.drop_malformed++;
        ingress_port->record_drop(pkt.size());
        return ResultCode::ERROR_PACKET_MALFORMED;
    }

    // 2. L2 Learning (learn source MAC on ingress port)
    mac_table_->learn(eth_hdr->src_mac, ingress_port->id(), clock_->now_ms());

    // 3. Check if packet is destined for the router's interface MAC
    bool is_for_router = (eth_hdr->dst_mac == ingress_port->mac());

    // If packet is NOT destined for router and NOT broadcast, try pure L2 forwarding
    if (!is_for_router && !eth_hdr->dst_mac.is_broadcast() && !eth_hdr->dst_mac.is_multicast()) {
        auto target_port_id = mac_table_->lookup(eth_hdr->dst_mac);
        if (target_port_id.has_value()) {
            if (*target_port_id == ingress_port->id()) {
                // Same port drop (hairpin filtering)
                ingress_port->record_drop(pkt.size());
                return ResultCode::SUCCESS;
            }
            auto egress_port = port_mgr_->get_port(*target_port_id);
            if (egress_port && egress_port->is_up()) {
                pkt.set_egress_port(egress_port->id());
                egress_port->enqueue_tx(pkt);
                egress_port->record_tx(pkt.size());
                std::lock_guard<std::mutex> lock(stats_mutex_);
                stats_.total_l2_forwarded++;
                stats_.total_tx_packets++;
                return ResultCode::SUCCESS;
            }
        }
        // Unknown unicast -> flood
        flood_frame(pkt, ingress_port->id());
        return ResultCode::SUCCESS;
    }

    if (eth_hdr->dst_mac.is_broadcast()) {
        // If broadcast, and not IPv4 (or even if IPv4 ARP/etc), also flood L2
        if (eth_hdr->ethertype != ETHERTYPE_IPV4) {
            flood_frame(pkt, ingress_port->id());
            return ResultCode::SUCCESS;
        }
    }

    // 4. L3 IPv4 Forwarding
    if (eth_hdr->ethertype != ETHERTYPE_IPV4) {
        // Non-IPv4 packet addressed to router
        return ResultCode::SUCCESS;
    }

    const uint8_t* ip_data = pkt.data() + ETHERNET_HEADER_LEN;
    size_t ip_len = pkt.size() - ETHERNET_HEADER_LEN;

    auto ip_hdr = IPv4Header::parse(ip_data, ip_len);
    if (!ip_hdr.has_value()) {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.drop_checksum_invalid++;
        ingress_port->record_drop(pkt.size());
        return ResultCode::ERROR_CHECKSUM_INVALID;
    }

    // Check if packet destination is the router's own IP on this interface
    if (ip_hdr->dst_ip == ingress_port->ip()) {
        // Packet terminated at router (e.g. control plane protocol message)
        return ResultCode::SUCCESS;
    }

    // Check TTL
    if (ip_hdr->ttl <= 1) {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.drop_ttl_expired++;
        ingress_port->record_drop(pkt.size());
        return ResultCode::ERROR_TTL_EXPIRED;
    }

    // Extract 5-tuple flow for ECMP
    size_t ip_hdr_len = ip_hdr->header_bytes();
    const uint8_t* l4_data = ip_data + ip_hdr_len;
    size_t l4_len = (ip_len > ip_hdr_len) ? (ip_len - ip_hdr_len) : 0;
    Flow5Tuple flow = Flow5Tuple::extract(*ip_hdr, l4_data, l4_len);

    // FIB Route Lookup
    auto nh_opt = fib_->resolve_forwarding(ip_hdr->dst_ip, flow);
    if (!nh_opt.has_value()) {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.drop_no_route++;
        ingress_port->record_drop(pkt.size());
        return ResultCode::ERROR_NO_ROUTE;
    }

    const NextHop& nh = *nh_opt;
    auto egress_port = port_mgr_->get_port(nh.port_id);
    if (!egress_port || !egress_port->is_up()) {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.drop_interface_down++;
        ingress_port->record_drop(pkt.size());
        return ResultCode::ERROR_INTERFACE_DOWN;
    }

    // 5. Decrement TTL and update IPv4 checksum
    uint8_t old_ttl = ip_hdr->ttl;
    uint8_t new_ttl = old_ttl - 1;
    uint16_t old_csum = host_to_net16(ip_hdr->checksum);
    uint16_t new_csum = update_checksum_ttl_decrement(old_csum, old_ttl, ip_hdr->protocol);

    uint8_t* mutable_ip_data = pkt.data() + ETHERNET_HEADER_LEN;
    mutable_ip_data[8] = new_ttl;
    write_unaligned<uint16_t>(mutable_ip_data + 10, new_csum);

    // 6. Rewrite Ethernet Header
    EthernetHeader new_eth;
    new_eth.src_mac = egress_port->mac();
    new_eth.dst_mac = nh.dst_mac;
    new_eth.ethertype = ETHERTYPE_IPV4;
    new_eth.serialize(pkt.data());

    // 7. Transmit to egress port
    pkt.set_egress_port(egress_port->id());
    egress_port->enqueue_tx(pkt);
    egress_port->record_tx(pkt.size());

    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.total_l3_forwarded++;
        stats_.total_tx_packets++;
    }

    return ResultCode::SUCCESS;
}

void ForwardingEngine::flood_frame(const PacketBuffer& pkt, PortId ingress_port) {
    auto ports = port_mgr_->get_all_ports();
    for (const auto& p : ports) {
        if (p->id() != ingress_port && p->is_up()) {
            PacketBuffer copy = pkt;
            copy.set_egress_port(p->id());
            p->enqueue_tx(copy);
            p->record_tx(copy.size());
        }
    }
    auto in_port = port_mgr_->get_port(ingress_port);
    if (in_port) in_port->record_flood();

    std::lock_guard<std::mutex> lock(stats_mutex_);
    stats_.total_l2_flooded++;
    stats_.total_tx_packets++;
}

} // namespace minirouter
