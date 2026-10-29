#pragma once

#include <vector>
#include <optional>
#include "minirouter/common/types.hpp"
#include "minirouter/packet/ipv4.hpp"

namespace minirouter {

template <typename T>
class BruteForceLpm {
public:
    struct Entry {
        IPv4Prefix prefix;
        T value;
    };

    ResultCode insert(const IPv4Prefix& prefix, const T& val) {
        for (auto& entry : entries_) {
            if (entry.prefix == prefix) {
                entry.value = val;
                return ResultCode::SUCCESS;
            }
        }
        entries_.push_back({prefix, val});
        return ResultCode::SUCCESS;
    }

    ResultCode remove(const IPv4Prefix& prefix) {
        for (auto it = entries_.begin(); it != entries_.end(); ++it) {
            if (it->prefix == prefix) {
                entries_.erase(it);
                return ResultCode::SUCCESS;
            }
        }
        return ResultCode::ERROR_NOT_FOUND;
    }

    std::optional<T> lookup(IPv4Address addr) const {
        const Entry* best_match = nullptr;
        int max_len = -1;

        for (const auto& entry : entries_) {
            if (entry.prefix.contains(addr)) {
                if (static_cast<int>(entry.prefix.prefix_len()) > max_len) {
                    max_len = static_cast<int>(entry.prefix.prefix_len());
                    best_match = &entry;
                }
            }
        }

        if (best_match) {
            return best_match->value;
        }
        return std::nullopt;
    }

    size_t size() const { return entries_.size(); }
    void clear() { entries_.clear(); }

    const std::vector<Entry>& entries() const { return entries_; }

private:
    std::vector<Entry> entries_;
};

} // namespace minirouter
