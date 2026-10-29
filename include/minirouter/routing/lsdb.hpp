#pragma once

#include <unordered_map>
#include <mutex>
#include <vector>
#include <optional>
#include "minirouter/routing/ospf_message.hpp"

namespace minirouter {

struct RouterIdHash {
    size_t operator()(const RouterId& id) const {
        return static_cast<size_t>(id.to_host_order());
    }
};

class Lsdb {
public:
    Lsdb() = default;

    // Returns true if LSA was new or had higher sequence number (needs flooding/SPF)
    bool install_lsa(const OspfLsa& lsa) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = database_.find(lsa.adv_router);
        if (it != database_.end()) {
            if (lsa.seq_num > it->second.seq_num) {
                it->second = lsa;
                return true;
            }
            return false; // Stale or duplicate LSA
        }
        database_[lsa.adv_router] = lsa;
        return true;
    }

    std::optional<OspfLsa> get_lsa(RouterId router_id) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = database_.find(router_id);
        if (it == database_.end()) return std::nullopt;
        return it->second;
    }

    std::vector<OspfLsa> get_all_lsas() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<OspfLsa> res;
        res.reserve(database_.size());
        for (const auto& [_, lsa] : database_) {
            res.push_back(lsa);
        }
        return res;
    }

    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return database_.size();
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        database_.clear();
    }

private:
    mutable std::mutex mutex_;
    std::unordered_map<RouterId, OspfLsa, RouterIdHash> database_;
};

} // namespace minirouter
