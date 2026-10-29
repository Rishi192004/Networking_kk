#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>
#include <utility>
#include "minirouter/common/types.hpp"
#include "minirouter/packet/ipv4.hpp"

namespace minirouter {

template <typename T>
class LpmTrie {
public:
    struct Node {
        std::unique_ptr<Node> children[2]{nullptr, nullptr};
        std::optional<T> value{std::nullopt};
        bool has_value{false};

        bool is_leaf() const {
            return !children[0] && !children[1];
        }
    };

    LpmTrie() : root_(std::make_unique<Node>()) {}

    ResultCode insert(const IPv4Prefix& prefix, const T& val) {
        Node* curr = root_.get();
        uint32_t addr = prefix.network().to_host_order();
        uint8_t len = prefix.prefix_len();

        for (uint8_t i = 0; i < len; ++i) {
            uint32_t bit = (addr >> (31 - i)) & 1u;
            if (!curr->children[bit]) {
                curr->children[bit] = std::make_unique<Node>();
            }
            curr = curr->children[bit].get();
        }

        if (!curr->has_value) {
            size_++;
        }
        curr->value = val;
        curr->has_value = true;
        return ResultCode::SUCCESS;
    }

    std::optional<T> lookup(IPv4Address addr) const {
        const Node* curr = root_.get();
        const Node* best_match = nullptr;
        uint32_t host_addr = addr.to_host_order();

        if (curr->has_value) {
            best_match = curr;
        }

        for (uint8_t i = 0; i < 32; ++i) {
            uint32_t bit = (host_addr >> (31 - i)) & 1u;
            if (!curr->children[bit]) {
                break;
            }
            curr = curr->children[bit].get();
            if (curr->has_value) {
                best_match = curr;
            }
        }

        if (best_match && best_match->has_value) {
            return best_match->value;
        }
        return std::nullopt;
    }

    // Exact prefix lookup
    std::optional<T> lookup_exact(const IPv4Prefix& prefix) const {
        const Node* curr = root_.get();
        uint32_t addr = prefix.network().to_host_order();
        uint8_t len = prefix.prefix_len();

        for (uint8_t i = 0; i < len; ++i) {
            uint32_t bit = (addr >> (31 - i)) & 1u;
            if (!curr->children[bit]) {
                return std::nullopt;
            }
            curr = curr->children[bit].get();
        }

        if (curr->has_value) {
            return curr->value;
        }
        return std::nullopt;
    }

    ResultCode remove(const IPv4Prefix& prefix) {
        bool removed = false;
        root_ = remove_helper(std::move(root_), prefix.network().to_host_order(), prefix.prefix_len(), 0, removed);
        if (removed) {
            size_--;
            return ResultCode::SUCCESS;
        }
        return ResultCode::ERROR_NOT_FOUND;
    }

    size_t size() const { return size_; }

    void clear() {
        root_ = std::make_unique<Node>();
        size_ = 0;
    }

    // Dump all routes
    void dump_all(std::vector<std::pair<IPv4Prefix, T>>& out) const {
        dump_helper(root_.get(), 0, 0, out);
    }

private:
    std::unique_ptr<Node> root_;
    size_t size_{0};

    std::unique_ptr<Node> remove_helper(std::unique_ptr<Node> node, uint32_t addr, uint8_t target_len, uint8_t depth, bool& removed) {
        if (!node) return nullptr;

        if (depth == target_len) {
            if (node->has_value) {
                node->has_value = false;
                node->value = std::nullopt;
                removed = true;
            }
        } else {
            uint32_t bit = (addr >> (31 - depth)) & 1u;
            node->children[bit] = remove_helper(std::move(node->children[bit]), addr, target_len, static_cast<uint8_t>(depth + 1), removed);
        }

        if (!node->has_value && !node->children[0] && !node->children[1] && depth > 0) {
            return nullptr;
        }
        return node;
    }

    void dump_helper(const Node* node, uint32_t current_prefix, uint8_t depth, std::vector<std::pair<IPv4Prefix, T>>& out) const {
        if (!node) return;
        if (node->has_value) {
            IPv4Prefix pfx(IPv4Address(current_prefix), depth);
            out.emplace_back(pfx, *node->value);
        }
        if (node->children[0]) {
            dump_helper(node->children[0].get(), current_prefix, static_cast<uint8_t>(depth + 1), out);
        }
        if (node->children[1]) {
            uint32_t next_prefix = current_prefix | (1u << (31 - depth));
            dump_helper(node->children[1].get(), next_prefix, static_cast<uint8_t>(depth + 1), out);
        }
    }
};

} // namespace minirouter
