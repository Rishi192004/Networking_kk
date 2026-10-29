#pragma once

#include <cstdint>
#include <unordered_map>
#include <mutex>
#include <optional>
#include <vector>
#include "minirouter/common/types.hpp"
#include "minirouter/dataplane/lpm_trie.hpp"
#include "minirouter/dataplane/next_hop.hpp"
#include "minirouter/dataplane/ecmp.hpp"

namespace minirouter {

struct FibRouteEntry {
    IPv4Prefix prefix;
    bool is_ecmp{false};
    NextHopId next_hop_id{INVALID_NEXT_HOP_ID};
    NextHopGroupId next_hop_group_id{INVALID_NEXT_HOP_GROUP_ID};
    uint64_t packet_count{0};
    uint64_t byte_count{0};
};

struct FibStats {
    uint64_t total_routes{0};
    uint64_t total_next_hops{0};
    uint64_t total_ecmp_groups{0};
    uint64_t lookups{0};
    uint64_t lookup_hits{0};
    uint64_t lookup_misses{0};
    uint64_t unresolved_drops{0};
};

class Fib {
public:
    Fib() = default;

    ResultCode add_next_hop(const NextHop& nh) {
        if (!nh.is_valid()) return ResultCode::ERROR_INVALID_PARAM;
        std::lock_guard<std::mutex> lock(mutex_);
        next_hops_[nh.id] = nh;
        return ResultCode::SUCCESS;
    }

    ResultCode remove_next_hop(NextHopId id) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = next_hops_.find(id);
        if (it == next_hops_.end()) return ResultCode::ERROR_NOT_FOUND;
        next_hops_.erase(it);
        return ResultCode::SUCCESS;
    }

    std::optional<NextHop> get_next_hop(NextHopId id) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = next_hops_.find(id);
        if (it == next_hops_.end()) return std::nullopt;
        return it->second;
    }

    ResultCode add_next_hop_group(const NextHopGroup& grp) {
        if (grp.id == INVALID_NEXT_HOP_GROUP_ID || grp.next_hop_ids.empty()) {
            return ResultCode::ERROR_INVALID_PARAM;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        next_hop_groups_[grp.id] = grp;
        return ResultCode::SUCCESS;
    }

    ResultCode remove_next_hop_group(NextHopGroupId id) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = next_hop_groups_.find(id);
        if (it == next_hop_groups_.end()) return ResultCode::ERROR_NOT_FOUND;
        next_hop_groups_.erase(it);
        return ResultCode::SUCCESS;
    }

    ResultCode add_route(const IPv4Prefix& prefix, NextHopId nh_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        FibRouteEntry entry;
        entry.prefix = prefix;
        entry.is_ecmp = false;
        entry.next_hop_id = nh_id;
        trie_.insert(prefix, entry);
        return ResultCode::SUCCESS;
    }

    ResultCode add_ecmp_route(const IPv4Prefix& prefix, NextHopGroupId grp_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        FibRouteEntry entry;
        entry.prefix = prefix;
        entry.is_ecmp = true;
        entry.next_hop_group_id = grp_id;
        trie_.insert(prefix, entry);
        return ResultCode::SUCCESS;
    }

    ResultCode remove_route(const IPv4Prefix& prefix) {
        std::lock_guard<std::mutex> lock(mutex_);
        return trie_.remove(prefix);
    }

    std::optional<FibRouteEntry> lookup_route(IPv4Address dst_ip) {
        std::lock_guard<std::mutex> lock(mutex_);
        stats_.lookups++;
        auto res = trie_.lookup(dst_ip);
        if (res.has_value()) {
            stats_.lookup_hits++;
        } else {
            stats_.lookup_misses++;
        }
        return res;
    }

    std::optional<NextHop> resolve_forwarding(IPv4Address dst_ip, const Flow5Tuple& flow) {
        std::lock_guard<std::mutex> lock(mutex_);
        stats_.lookups++;
        auto route = trie_.lookup(dst_ip);
        if (!route.has_value()) {
            stats_.lookup_misses++;
            return std::nullopt;
        }
        stats_.lookup_hits++;

        NextHopId target_nh_id = INVALID_NEXT_HOP_ID;
        if (route->is_ecmp) {
            auto grp_it = next_hop_groups_.find(route->next_hop_group_id);
            if (grp_it == next_hop_groups_.end()) {
                stats_.unresolved_drops++;
                return std::nullopt;
            }
            target_nh_id = grp_it->second.select_next_hop(flow);
        } else {
            target_nh_id = route->next_hop_id;
        }

        auto nh_it = next_hops_.find(target_nh_id);
        if (nh_it == next_hops_.end()) {
            stats_.unresolved_drops++;
            return std::nullopt;
        }

        return nh_it->second;
    }

    size_t route_count() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return trie_.size();
    }

    size_t next_hop_count() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return next_hops_.size();
    }

    FibStats get_stats() const {
        std::lock_guard<std::mutex> lock(mutex_);
        FibStats copy = stats_;
        copy.total_routes = trie_.size();
        copy.total_next_hops = next_hops_.size();
        copy.total_ecmp_groups = next_hop_groups_.size();
        return copy;
    }

    std::vector<FibRouteEntry> get_all_routes() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<std::pair<IPv4Prefix, FibRouteEntry>> pairs;
        trie_.dump_all(pairs);
        std::vector<FibRouteEntry> routes;
        routes.reserve(pairs.size());
        for (const auto& p : pairs) {
            routes.push_back(p.second);
        }
        return routes;
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        trie_.clear();
        next_hops_.clear();
        next_hop_groups_.clear();
    }

private:
    mutable std::mutex mutex_;
    LpmTrie<FibRouteEntry> trie_;
    std::unordered_map<NextHopId, NextHop> next_hops_;
    std::unordered_map<NextHopGroupId, NextHopGroup> next_hop_groups_;
    FibStats stats_;
};

} // namespace minirouter
