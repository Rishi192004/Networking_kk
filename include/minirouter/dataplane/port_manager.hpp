#pragma once

#include <memory>
#include <unordered_map>
#include <mutex>
#include <vector>
#include "minirouter/dataplane/port.hpp"

namespace minirouter {

class PortManager {
public:
    PortManager() = default;

    ResultCode add_port(std::shared_ptr<VirtualPort> port) {
        if (!port) return ResultCode::ERROR_INVALID_PARAM;
        std::lock_guard<std::mutex> lock(mutex_);
        if (ports_.find(port->id()) != ports_.end()) {
            return ResultCode::ERROR_ALREADY_EXISTS;
        }
        ports_[port->id()] = port;
        return ResultCode::SUCCESS;
    }

    ResultCode remove_port(PortId id) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = ports_.find(id);
        if (it == ports_.end()) {
            return ResultCode::ERROR_NOT_FOUND;
        }
        ports_.erase(it);
        return ResultCode::SUCCESS;
    }

    std::shared_ptr<VirtualPort> get_port(PortId id) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = ports_.find(id);
        if (it == ports_.end()) return nullptr;
        return it->second;
    }

    std::vector<std::shared_ptr<VirtualPort>> get_all_ports() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<std::shared_ptr<VirtualPort>> res;
        res.reserve(ports_.size());
        for (const auto& [_, p] : ports_) {
            res.push_back(p);
        }
        return res;
    }

    size_t port_count() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return ports_.size();
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        ports_.clear();
    }

private:
    mutable std::mutex mutex_;
    std::unordered_map<PortId, std::shared_ptr<VirtualPort>> ports_;
};

} // namespace minirouter
