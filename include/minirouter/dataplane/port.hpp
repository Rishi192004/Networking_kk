#pragma once

#include <cstdint>
#include <string>
#include <queue>
#include <mutex>
#include <vector>
#include <optional>
#include "minirouter/common/types.hpp"
#include "minirouter/packet/ethernet.hpp"
#include "minirouter/packet/ipv4.hpp"
#include "minirouter/packet/packet_buffer.hpp"

namespace minirouter {

struct PortStats {
    uint64_t rx_packets{0};
    uint64_t rx_bytes{0};
    uint64_t tx_packets{0};
    uint64_t tx_bytes{0};
    uint64_t drop_packets{0};
    uint64_t drop_bytes{0};
    uint64_t flood_packets{0};
};

class VirtualPort {
public:
    VirtualPort(PortId id, std::string name, MacAddress mac, IPv4Address ip = IPv4Address::any(), uint8_t prefix_len = 0)
        : id_(id), name_(std::move(name)), mac_(mac), ip_(ip), prefix_len_(prefix_len), is_up_(true) {}

    PortId id() const { return id_; }
    const std::string& name() const { return name_; }
    MacAddress mac() const { return mac_; }
    void set_mac(MacAddress mac) { mac_ = mac; }

    IPv4Address ip() const { return ip_; }
    uint8_t prefix_len() const { return prefix_len_; }
    void set_ip(IPv4Address ip, uint8_t prefix_len) {
        ip_ = ip;
        prefix_len_ = prefix_len;
    }

    bool is_up() const { return is_up_; }
    void set_up(bool up) { is_up_ = up; }

    // Statistics
    PortStats get_stats() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return stats_;
    }

    void record_rx(size_t bytes) {
        std::lock_guard<std::mutex> lock(mutex_);
        stats_.rx_packets++;
        stats_.rx_bytes += bytes;
    }

    void record_tx(size_t bytes) {
        std::lock_guard<std::mutex> lock(mutex_);
        stats_.tx_packets++;
        stats_.tx_bytes += bytes;
    }

    void record_drop(size_t bytes) {
        std::lock_guard<std::mutex> lock(mutex_);
        stats_.drop_packets++;
        stats_.drop_bytes += bytes;
    }

    void record_flood() {
        std::lock_guard<std::mutex> lock(mutex_);
        stats_.flood_packets++;
    }

    // Packet queue for deterministic in-memory simulation
    void enqueue_rx(PacketBuffer pkt) {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        pkt.set_ingress_port(id_);
        rx_queue_.push(std::move(pkt));
    }

    std::optional<PacketBuffer> dequeue_rx() {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        if (rx_queue_.empty()) return std::nullopt;
        PacketBuffer pkt = std::move(rx_queue_.front());
        rx_queue_.pop();
        return pkt;
    }

    void enqueue_tx(PacketBuffer pkt) {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        pkt.set_egress_port(id_);
        tx_queue_.push(std::move(pkt));
    }

    std::optional<PacketBuffer> dequeue_tx() {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        if (tx_queue_.empty()) return std::nullopt;
        PacketBuffer pkt = std::move(tx_queue_.front());
        tx_queue_.pop();
        return pkt;
    }

    size_t rx_queue_size() const {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        return rx_queue_.size();
    }

    size_t tx_queue_size() const {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        return tx_queue_.size();
    }

    void clear_queues() {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        while (!rx_queue_.empty()) rx_queue_.pop();
        while (!tx_queue_.empty()) tx_queue_.pop();
    }

private:
    PortId id_;
    std::string name_;
    MacAddress mac_;
    IPv4Address ip_;
    uint8_t prefix_len_{0};
    bool is_up_{true};

    mutable std::mutex mutex_;
    PortStats stats_;

    mutable std::mutex queue_mutex_;
    std::queue<PacketBuffer> rx_queue_;
    std::queue<PacketBuffer> tx_queue_;
};

} // namespace minirouter
