#pragma once

#include <cstdint>
#include <unordered_map>
#include <mutex>
#include <vector>
#include <optional>
#include "minirouter/common/types.hpp"
#include "minirouter/common/clock.hpp"
#include "minirouter/packet/ethernet.hpp"

namespace minirouter {

// Hash for MacAddress to use in std::unordered_map
struct MacAddressHash {
    size_t operator()(const MacAddress& mac) const {
        size_t h = 0;
        for (auto b : mac.bytes) {
            h = (h * 31) + b;
        }
        return h;
    }
};

struct MacTableEntry {
    MacAddress mac;
    PortId port_id{INVALID_PORT_ID};
    uint64_t last_seen_ms{0};
    bool is_static{false};
};

struct MacTableStats {
    uint64_t total_learns{0};
    uint64_t total_lookups{0};
    uint64_t lookup_hits{0};
    uint64_t lookup_misses{0};
    uint64_t total_aged{0};
};

class MacTable {
public:
    explicit MacTable(uint64_t default_ageing_time_ms = 300000) // Default 300 seconds
        : ageing_time_ms_(default_ageing_time_ms) {}

    // Learn or refresh an entry
    ResultCode learn(const MacAddress& mac, PortId ingress_port, uint64_t current_time_ms) {
        if (mac.is_broadcast() || mac.is_multicast() || mac.is_zero()) {
            return ResultCode::ERROR_INVALID_PARAM;
        }

        std::lock_guard<std::mutex> lock(mutex_);
        auto it = table_.find(mac);
        if (it != table_.end()) {
            if (!it->second.is_static) {
                it->second.port_id = ingress_port;
                it->second.last_seen_ms = current_time_ms;
            }
        } else {
            table_[mac] = MacTableEntry{mac, ingress_port, current_time_ms, false};
            stats_.total_learns++;
        }
        return ResultCode::SUCCESS;
    }

    // Add static entry (never aged out)
    ResultCode add_static(const MacAddress& mac, PortId port_id) {
        if (mac.is_broadcast() || mac.is_multicast() || mac.is_zero()) {
            return ResultCode::ERROR_INVALID_PARAM;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        table_[mac] = MacTableEntry{mac, port_id, 0, true};
        return ResultCode::SUCCESS;
    }

    // Lookup destination MAC
    std::optional<PortId> lookup(const MacAddress& mac) {
        std::lock_guard<std::mutex> lock(mutex_);
        stats_.total_lookups++;
        auto it = table_.find(mac);
        if (it != table_.end()) {
            stats_.lookup_hits++;
            return it->second.port_id;
        }
        stats_.lookup_misses++;
        return std::nullopt;
    }

    // Remove specific entry
    ResultCode remove(const MacAddress& mac) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = table_.find(mac);
        if (it != table_.end()) {
            table_.erase(it);
            return ResultCode::SUCCESS;
        }
        return ResultCode::ERROR_NOT_FOUND;
    }

    // Age out old entries
    size_t age_entries(uint64_t current_time_ms, std::optional<uint64_t> max_age_override_ms = std::nullopt) {
        uint64_t max_age = max_age_override_ms.value_or(ageing_time_ms_);
        std::lock_guard<std::mutex> lock(mutex_);
        size_t aged_count = 0;

        for (auto it = table_.begin(); it != table_.end();) {
            if (!it->second.is_static && (current_time_ms >= it->second.last_seen_ms + max_age)) {
                it = table_.erase(it);
                aged_count++;
            } else {
                ++it;
            }
        }
        stats_.total_aged += aged_count;
        return aged_count;
    }

    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return table_.size();
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        table_.clear();
    }

    MacTableStats get_stats() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return stats_;
    }

    std::vector<MacTableEntry> get_all_entries() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<MacTableEntry> res;
        res.reserve(table_.size());
        for (const auto& [_, entry] : table_) {
            res.push_back(entry);
        }
        return res;
    }

private:
    uint64_t ageing_time_ms_;
    mutable std::mutex mutex_;
    std::unordered_map<MacAddress, MacTableEntry, MacAddressHash> table_;
    MacTableStats stats_;
};

} // namespace minirouter
