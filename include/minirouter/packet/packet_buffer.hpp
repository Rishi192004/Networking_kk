#pragma once

#include <cstdint>
#include <vector>
#include <span>
#include <memory>
#include "minirouter/common/types.hpp"
#include "minirouter/packet/ethernet.hpp"
#include "minirouter/packet/ipv4.hpp"

namespace minirouter {

class PacketBuffer {
public:
    PacketBuffer() = default;
    explicit PacketBuffer(size_t reserve_size) {
        data_.reserve(reserve_size);
    }
    PacketBuffer(const uint8_t* bytes, size_t len, PortId ingress_port = INVALID_PORT_ID)
        : data_(bytes, bytes + len), ingress_port_(ingress_port) {}
    explicit PacketBuffer(std::vector<uint8_t> data, PortId ingress_port = INVALID_PORT_ID)
        : data_(std::move(data)), ingress_port_(ingress_port) {}

    const uint8_t* data() const { return data_.data(); }
    uint8_t* data() { return data_.data(); }
    size_t size() const { return data_.size(); }
    bool empty() const { return data_.empty(); }

    void resize(size_t new_size) { data_.resize(new_size); }
    void clear() {
        data_.clear();
        ingress_port_ = INVALID_PORT_ID;
        egress_port_ = INVALID_PORT_ID;
    }

    PortId ingress_port() const { return ingress_port_; }
    void set_ingress_port(PortId port) { ingress_port_ = port; }

    PortId egress_port() const { return egress_port_; }
    void set_egress_port(PortId port) { egress_port_ = port; }

    const std::vector<uint8_t>& raw_bytes() const { return data_; }
    std::vector<uint8_t>& raw_bytes() { return data_; }

    void append(const uint8_t* bytes, size_t len) {
        data_.insert(data_.end(), bytes, bytes + len);
    }

private:
    std::vector<uint8_t> data_;
    PortId ingress_port_{INVALID_PORT_ID};
    PortId egress_port_{INVALID_PORT_ID};
};

} // namespace minirouter
