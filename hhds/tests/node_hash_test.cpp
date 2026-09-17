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

// The LIVE combiner. These two property tests used to exercise the old
// commutative_combine, which had no caller outside this file and is deleted;
// the properties themselves still matter, so they now cover field_combine.
uint64_t combine(std::vector<uint64_t> v) { return hhds::field_combine(v); }

// Permutation invariance: every ordering of the same multiset hashes alike.
void test_field_combine_is_permutation_invariant() {
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
void test_field_combine_separates_near_multisets() {
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

// canonical_node_hash under ONE DRIVER PER SINK PIN: every operand owns its own
// sink pid, so the hash must be commutative WITHIN an operand ROLE, positional
// ACROSS roles, and keyed on the node type. The caller supplies the pid -> role
// mapping (LiveHD passes Ntype::sink_bank); here kSum/kSub use the same parity
// convention: EVEN pid = the "added" role, ODD = the "subtracted" one.
void test_canonical_node_hash() {
  constexpr hhds::Type kSum = 1;
  constexpr hhds::Type kSub = 2;

  const auto role = [](hhds::Type t, hhds::Port_id pid) -> hhds::Port_id {
    return (t == kSum || t == kSub) ? static_cast<hhds::Port_id>(pid & 1U) : pid;
  };

  hhds::GraphLibrary lib;
  auto               gio = lib.create_io("top");
  auto               g   = gio->create_graph();

  auto a = g->create_node();
  auto b = g->create_node();

  // Two distinct leaf hashes, keyed off the driver node's own id.
  const auto child = [&](const hhds::Pin_class& p) -> uint64_t {
    return p.get_master_node() == a ? 0x1111ULL : 0x2222ULL;
  };

  // Sum(add: a, b) vs Sum(add: b, a). Two operands of ONE role, each on its own
  // sink pin (pids 0 and 2 -- consecutive slots of the even/"added" bank):
  // EQUAL, because the role, not the slot, is what is mixed in.
  auto sum_ab = g->create_node();
  sum_ab.set_type(kSum);
  sum_ab.create_sink_pin(0).connect_driver(a.create_driver_pin());
  sum_ab.create_sink_pin(2).connect_driver(b.create_driver_pin());

  auto sum_ba = g->create_node();
  sum_ba.set_type(kSum);
  sum_ba.create_sink_pin(0).connect_driver(b.create_driver_pin());
  sum_ba.create_sink_pin(2).connect_driver(a.create_driver_pin());

  TEST_CHECK(hhds::canonical_node_hash(sum_ab, child, role) == hhds::canonical_node_hash(sum_ba, child, role));

  // Keying on the RAW pid instead would say these are different nodes -- this
  // is exactly the regression the role mapping exists to prevent.
  TEST_CHECK(hhds::canonical_node_hash(sum_ab, child) != hhds::canonical_node_hash(sum_ba, child));

  // Sub(add: a, sub: b) vs Sub(add: b, sub: a) -- operands swapped ACROSS roles
  // (even pid vs odd pid): DIFFERENT.
  auto sub_ab = g->create_node();
  sub_ab.set_type(kSub);
  sub_ab.create_sink_pin(0).connect_driver(a.create_driver_pin());
  sub_ab.create_sink_pin(1).connect_driver(b.create_driver_pin());

  auto sub_ba = g->create_node();
  sub_ba.set_type(kSub);
  sub_ba.create_sink_pin(0).connect_driver(b.create_driver_pin());
  sub_ba.create_sink_pin(1).connect_driver(a.create_driver_pin());

  TEST_CHECK(hhds::canonical_node_hash(sub_ab, child, role) != hhds::canonical_node_hash(sub_ba, child, role));

  // A DUPLICATED operand is meaningful: Sum(a, a) is 2a, not a and not nothing.
  // A pure-xor combiner would collapse it to the empty fold; the multiset one
  // must keep all three apart.
  auto sum_a = g->create_node();
  sum_a.set_type(kSum);
  sum_a.create_sink_pin(0).connect_driver(a.create_driver_pin());

  auto sum_aa = g->create_node();
  sum_aa.set_type(kSum);
  sum_aa.create_sink_pin(0).connect_driver(a.create_driver_pin());
  sum_aa.create_sink_pin(2).connect_driver(a.create_driver_pin());

  auto sum_empty = g->create_node();
  sum_empty.set_type(kSum);

  const auto h_a     = hhds::canonical_node_hash(sum_a, child, role);
  const auto h_aa    = hhds::canonical_node_hash(sum_aa, child, role);
  const auto h_empty = hhds::canonical_node_hash(sum_empty, child, role);
  TEST_CHECK(h_a != h_aa);
  TEST_CHECK(h_aa != h_empty);
  TEST_CHECK(h_a != h_empty);

  // Same shape, different TYPE: different hash.
  auto sum_as_sub = g->create_node();
  sum_as_sub.set_type(kSum);
  sum_as_sub.create_sink_pin(0).connect_driver(a.create_driver_pin());
  sum_as_sub.create_sink_pin(1).connect_driver(b.create_driver_pin());
  TEST_CHECK(hhds::canonical_node_hash(sum_as_sub, child, role) != hhds::canonical_node_hash(sub_ab, child, role));
}

}  // namespace

int main() {
  test_field_combine_is_permutation_invariant();
  test_field_combine_separates_near_multisets();
  test_canonical_node_hash();
  std::printf("node_hash_test passed\n");
  return 0;
}
