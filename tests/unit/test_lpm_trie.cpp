#include <gtest/gtest.h>
#include <random>
#include "minirouter/dataplane/lpm_trie.hpp"
#include "minirouter/dataplane/brute_force_lpm.hpp"

using namespace minirouter;

TEST(LpmTrieTest, BasicOperations) {
    LpmTrie<uint32_t> trie;

    auto p1 = IPv4Prefix::from_string("10.0.0.0/8").value();
    auto p2 = IPv4Prefix::from_string("10.1.0.0/16").value();
    auto p3 = IPv4Prefix::from_string("10.1.1.0/24").value();
    auto p4 = IPv4Prefix::from_string("10.1.1.1/32").value();
    auto def_route = IPv4Prefix::from_string("0.0.0.0/0").value();

    trie.insert(def_route, 100);
    trie.insert(p1, 10);
    trie.insert(p2, 20);
    trie.insert(p3, 30);
    trie.insert(p4, 40);

    EXPECT_EQ(trie.size(), 5u);

    // Exact matches
    EXPECT_EQ(trie.lookup_exact(p1), 10u);
    EXPECT_EQ(trie.lookup_exact(p2), 20u);

    // Longest prefix match lookups
    EXPECT_EQ(trie.lookup(IPv4Address::from_string("10.1.1.1").value()), 40u);
    EXPECT_EQ(trie.lookup(IPv4Address::from_string("10.1.1.2").value()), 30u);
    EXPECT_EQ(trie.lookup(IPv4Address::from_string("10.1.2.1").value()), 20u);
    EXPECT_EQ(trie.lookup(IPv4Address::from_string("10.2.1.1").value()), 10u);
    EXPECT_EQ(trie.lookup(IPv4Address::from_string("192.168.1.1").value()), 100u); // Default route

    // Overwrite / replacement
    trie.insert(p3, 33);
    EXPECT_EQ(trie.size(), 5u);
    EXPECT_EQ(trie.lookup(IPv4Address::from_string("10.1.1.2").value()), 33u);

    // Deletion
    EXPECT_EQ(trie.remove(p4), ResultCode::SUCCESS);
    EXPECT_EQ(trie.size(), 4u);
    // After removing /32, falls back to /24
    EXPECT_EQ(trie.lookup(IPv4Address::from_string("10.1.1.1").value()), 33u);

    // Non-existent deletion
    EXPECT_EQ(trie.remove(p4), ResultCode::ERROR_NOT_FOUND);
}

TEST(LpmTrieTest, RandomizedEquivalenceWithBruteForce) {
    LpmTrie<uint32_t> trie;
    BruteForceLpm<uint32_t> ref;

    std::mt19937 rng(42); // Deterministic seed
    std::uniform_int_distribution<uint32_t> addr_dist(1, 0xFFFFFFFE);
    std::uniform_int_distribution<uint32_t> len_dist(0, 32);

    // Insert 500 randomized prefixes into both trie and reference
    for (uint32_t i = 1; i <= 500; ++i) {
        IPv4Address addr(addr_dist(rng));
        uint8_t len = static_cast<uint8_t>(len_dist(rng));
        IPv4Prefix prefix(addr, len);

        trie.insert(prefix, i);
        ref.insert(prefix, i);
    }

    EXPECT_EQ(trie.size(), ref.size());

    // Perform 2,000 randomized lookups and verify 100% equivalence
    for (int i = 0; i < 2000; ++i) {
        IPv4Address test_addr(addr_dist(rng));

        auto trie_res = trie.lookup(test_addr);
        auto ref_res = ref.lookup(test_addr);

        ASSERT_EQ(trie_res.has_value(), ref_res.has_value())
            << "Mismatch for IP " << test_addr.to_string();
        if (trie_res.has_value()) {
            EXPECT_EQ(*trie_res, *ref_res)
                << "Value mismatch for IP " << test_addr.to_string();
        }
    }

    // Delete 100 entries and verify equivalence again
    for (size_t i = 0; i < 100 && !ref.entries().empty(); ++i) {
        auto pfx = ref.entries().front().prefix;
        EXPECT_EQ(trie.remove(pfx), ResultCode::SUCCESS);
        EXPECT_EQ(ref.remove(pfx), ResultCode::SUCCESS);
    }

    EXPECT_EQ(trie.size(), ref.size());

    for (int i = 0; i < 500; ++i) {
        IPv4Address test_addr(addr_dist(rng));
        auto trie_res = trie.lookup(test_addr);
        auto ref_res = ref.lookup(test_addr);

        ASSERT_EQ(trie_res.has_value(), ref_res.has_value());
        if (trie_res.has_value()) {
            EXPECT_EQ(*trie_res, *ref_res);
        }
    }
}
