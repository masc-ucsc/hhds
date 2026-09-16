// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// Contract test for the structural hashing primitives (hhds/node_hash.hpp).

#include "hhds/node_hash.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <set>
#include <vector>

namespace {

#define TEST_CHECK(condition)                                                            \
  do {                                                                                   \
    if (!(condition)) {                                                                   \
      std::fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition); \
      std::abort();                                                                      \
    }                                                                                    \
  } while (false)

uint64_t combine(std::vector<uint64_t> v) { return hhds::commutative_combine(v); }

// Permutation invariance: every ordering of the same multiset hashes alike.
void test_commutative_combine_is_permutation_invariant() {
  std::vector<uint64_t> terms = {1, 2, 3, 0, 0xdeadbeefULL, ~uint64_t{0}, 7, 7};
  const uint64_t        want  = combine(terms);

  std::mt19937_64 rng(0xC0FFEE);
  for (int i = 0; i < 200; ++i) {
    std::shuffle(terms.begin(), terms.end(), rng);
    TEST_CHECK(combine(terms) == want);
  }

  TEST_CHECK(combine({}) == combine({}));
  TEST_CHECK(combine({5}) == combine({5}));
}

// Multisets that a weaker combiner would confuse must stay apart:
//   a plain XOR collapses {a, a};
//   a plain SUM collapses {a, b} and {a-1, b+1};
//   neither separates {a} from {a, 0}.
void test_commutative_combine_separates_near_multisets() {
  std::set<uint64_t> seen;
  const auto         distinct = [&](uint64_t h) {
    const bool inserted = seen.insert(h).second;
    TEST_CHECK(inserted);
  };

  distinct(combine({}));
  distinct(combine({0}));
  distinct(combine({0, 0}));
  distinct(combine({5}));
  distinct(combine({5, 0}));
  distinct(combine({5, 5}));
  distinct(combine({4, 6}));
  distinct(combine({3, 7}));
  distinct(combine({5, 5, 5}));

  // Collision-free over every 3-subset of a small universe.
  std::set<uint64_t> triples;
  int                count = 0;
  for (uint64_t a = 0; a < 20; ++a) {
    for (uint64_t b = a; b < 20; ++b) {
      for (uint64_t c = b; c < 20; ++c) {
        triples.insert(combine({a, b, c}));
        ++count;
      }
    }
  }
  TEST_CHECK(static_cast<int>(triples.size()) == count);
}

void test_commutative_combine128() {
  std::vector<hhds::Sig128> terms
      = {hhds::hash_mix128(1), hhds::hash_mix128(2), hhds::hash_mix128(3), hhds::hash_mix128(3)};
  const auto want = hhds::commutative_combine128(terms);

  std::mt19937_64 rng(0xBEEF);
  for (int i = 0; i < 100; ++i) {
    std::shuffle(terms.begin(), terms.end(), rng);
    TEST_CHECK(hhds::commutative_combine128(terms) == want);
  }

  std::vector<hhds::Sig128> other = {hhds::hash_mix128(1), hhds::hash_mix128(2), hhds::hash_mix128(3)};
  TEST_CHECK(!(hhds::commutative_combine128(other) == want));
}

// canonical_node_hash: commutative WITHIN a sink port, positional ACROSS ports,
// and keyed on the node type.
void test_canonical_node_hash() {
  constexpr hhds::Type kSum = 1;
  constexpr hhds::Type kSub = 2;

  hhds::GraphLibrary lib;
  auto               gio = lib.create_io("top");
  auto               g   = gio->create_graph();

  auto a = g->create_node();
  auto b = g->create_node();

  // Two distinct leaf hashes, keyed off the driver node's own id.
  const auto child = [&](const hhds::Pin_class& p) -> uint64_t {
    return p.get_master_node() == a ? 0x1111ULL : 0x2222ULL;
  };

  // Sum(as: a, b)  vs  Sum(as: b, a) -- one sink pin, two drivers: EQUAL.
  auto sum_ab = g->create_node();
  sum_ab.set_type(kSum);
  sum_ab.create_sink_pin(1).connect_driver(a.create_driver_pin());
  sum_ab.create_sink_pin(1).connect_driver(b.create_driver_pin());

  auto sum_ba = g->create_node();
  sum_ba.set_type(kSum);
  sum_ba.create_sink_pin(1).connect_driver(b.create_driver_pin());
  sum_ba.create_sink_pin(1).connect_driver(a.create_driver_pin());

  TEST_CHECK(hhds::canonical_node_hash(sum_ab, child) == hhds::canonical_node_hash(sum_ba, child));

  // Sub(as: a, bs: b)  vs  Sub(as: b, bs: a) -- operands swapped ACROSS pins:
  // DIFFERENT, because the sink pid is mixed into each term.
  auto sub_ab = g->create_node();
  sub_ab.set_type(kSub);
  sub_ab.create_sink_pin(1).connect_driver(a.create_driver_pin());
  sub_ab.create_sink_pin(2).connect_driver(b.create_driver_pin());

  auto sub_ba = g->create_node();
  sub_ba.set_type(kSub);
  sub_ba.create_sink_pin(1).connect_driver(b.create_driver_pin());
  sub_ba.create_sink_pin(2).connect_driver(a.create_driver_pin());

  TEST_CHECK(hhds::canonical_node_hash(sub_ab, child) != hhds::canonical_node_hash(sub_ba, child));

  // Same shape, different TYPE: different hash.
  auto sum_as_sub = g->create_node();
  sum_as_sub.set_type(kSum);
  sum_as_sub.create_sink_pin(1).connect_driver(a.create_driver_pin());
  sum_as_sub.create_sink_pin(2).connect_driver(b.create_driver_pin());
  TEST_CHECK(hhds::canonical_node_hash(sum_as_sub, child) != hhds::canonical_node_hash(sub_ab, child));

  // The 128-bit variant agrees on all three answers.
  const auto child128 = [&](const hhds::Pin_class& p) -> hhds::Sig128 { return hhds::hash_mix128(child(p)); };
  TEST_CHECK(hhds::canonical_node_hash128(sum_ab, child128) == hhds::canonical_node_hash128(sum_ba, child128));
  TEST_CHECK(!(hhds::canonical_node_hash128(sub_ab, child128) == hhds::canonical_node_hash128(sub_ba, child128)));
  TEST_CHECK(!(hhds::canonical_node_hash128(sum_as_sub, child128) == hhds::canonical_node_hash128(sub_ab, child128)));
}

}  // namespace

int main() {
  test_commutative_combine_is_permutation_invariant();
  test_commutative_combine_separates_near_multisets();
  test_commutative_combine128();
  test_canonical_node_hash();
  std::printf("node_hash_test passed\n");
  return 0;
}
