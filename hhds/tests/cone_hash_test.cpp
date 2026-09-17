// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// Contract test for cone_hash / subgraph_hash (hhds/node_hash.hpp).
//
// The property that matters for incremental is STABILITY: the digest must fold
// only port_id, cell type, width and connectivity -- never a node id or pin id,
// which are allocation order and are reshuffled every time a graph is loaded.
// The build-order test below is the one that would catch a regression there.

#include "hhds/node_hash.hpp"

#include <cstdio>
#include <cstdlib>
#include <set>
#include <vector>

namespace {

#define TEST_CHECK(condition)                                                            \
  do {                                                                                   \
    if (!(condition)) {                                                                  \
      std::fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition); \
      std::abort();                                                                      \
    }                                                                                    \
  } while (false)

constexpr hhds::Type kIn  = 7;
constexpr hhds::Type kSum = 1;
constexpr hhds::Type kAnd = 3;

// Sum is banked on pid parity (even = "as"), like LiveHD's Ntype::sink_bank.
hhds::Port_id role(hhds::Type t, hhds::Port_id pid) {
  return (t == kSum || t == kAnd) ? static_cast<hhds::Port_id>(0) : pid;
}
bool     is_leaf(const hhds::Pin_class& p) { return p.get_master_node().get_type() == kIn; }
uint64_t width_of(const hhds::Pin_class&) { return 8; }

// Build `out = (x OP y)` where the two leaves are created in the order given, so
// the SAME structure gets DIFFERENT node ids between the two calls.
uint64_t build_and_hash(bool leaves_reversed, hhds::Type op) {
  hhds::GraphLibrary lib;
  auto               g = lib.create_io("top")->create_graph();

  auto first  = g->create_node();
  auto second = g->create_node();
  first.set_type(kIn);
  second.set_type(kIn);
  auto x = leaves_reversed ? second : first;
  auto y = leaves_reversed ? first : second;

  auto n = g->create_node();
  n.set_type(op);
  n.create_sink_pin(0).connect_driver(x.create_driver_pin());
  n.create_sink_pin(2).connect_driver(y.create_driver_pin());
  return hhds::cone_hash(n.create_driver_pin(), is_leaf, width_of, role);
}

// THE incremental property: node ids change with build order, the digest must not.
void test_stable_under_id_reshuffle() {
  TEST_CHECK(build_and_hash(false, kSum) == build_and_hash(true, kSum));
}

// Different cell type over the same operands must not collide (spec invariant 7).
void test_op_discriminates() { TEST_CHECK(build_and_hash(false, kSum) != build_and_hash(false, kAnd)); }

// A cone that stops at a boundary folds the boundary's STABLE facts (type,
// port_id, width) and nothing about its identity.
void test_leaf_is_identity_free() {
  hhds::GraphLibrary lib;
  auto               g = lib.create_io("top")->create_graph();
  auto               a = g->create_node();
  auto               b = g->create_node();
  a.set_type(kIn);
  b.set_type(kIn);
  // Two DIFFERENT boundary nodes of the same type/port/width hash the same: the
  // cone's identity comes from its shape, not from which leaf instance it hit.
  TEST_CHECK(hhds::cone_leaf_hash(a.create_driver_pin(), width_of)
             == hhds::cone_leaf_hash(b.create_driver_pin(), width_of));
}

// subgraph_hash is the fold of the OUTPUT cones, ordered by the stable port_id.
void test_subgraph_is_fold_of_output_cones() {
  hhds::GraphLibrary lib;
  auto               g = lib.create_io("top")->create_graph();
  auto               a = g->create_node();
  a.set_type(kIn);
  auto n = g->create_node();
  n.set_type(kSum);
  n.create_sink_pin(0).connect_driver(a.create_driver_pin());

  std::vector<hhds::Pin_class> outs{n.create_driver_pin()};
  const auto                   h1 = hhds::subgraph_hash(outs, is_leaf, width_of, role);
  const auto                   h2 = hhds::subgraph_hash(outs, is_leaf, width_of, role);
  TEST_CHECK(h1 == h2);  // deterministic
  std::vector<hhds::Pin_class> none;
  TEST_CHECK(hhds::subgraph_hash(none, is_leaf, width_of, role) != h1);  // cardinality is folded
}

// A combinational cycle must terminate with a value, not overflow the stack. A
// cycle is a defect elsewhere; the hash must not be the thing that crashes on it.
void test_cycle_terminates() {
  hhds::GraphLibrary lib;
  auto               g = lib.create_io("top")->create_graph();
  auto               n = g->create_node();
  n.set_type(kSum);
  auto d = n.create_driver_pin();
  n.create_sink_pin(0).connect_driver(d);  // self edge
  (void)hhds::cone_hash(d, is_leaf, width_of, role);
}

void test_cycle_hash_ignores_allocation_ids() {
  const auto build = [](int padding) {
    hhds::GraphLibrary lib;
    auto               g = lib.create_io("top")->create_graph();
    for (int i = 0; i < padding; ++i) {
      (void)g->create_node().create_driver_pin(7);
    }
    auto n = g->create_node();
    n.set_type(kSum);
    auto driver = n.create_driver_pin(7);
    n.create_sink_pin(0).connect_driver(driver);
    return hhds::cone_hash(driver, is_leaf, width_of, role);
  };
  TEST_CHECK(build(0) == build(17));
}

// --- group_fold ------------------------------------------------------------
//
// The properties the migrated LiveHD call sites depend on. `group_fold` is the
// shared spelling of "bucket by operand role, fold each bucket
// order-independently, combine the buckets positionally".

using Groups = absl::flat_hash_map<int, std::vector<uint64_t>>;

// Order inside a bucket is not a contract: `a+b` must hash like `b+a`.
void test_group_fold_order_independent_inside_a_group() {
  Groups a, b;
  a[0] = {10, 30, 7};
  a[1] = {5};
  b[0] = {7, 10, 30};
  b[1] = {5};
  TEST_CHECK(hhds::group_fold(99, a) == hhds::group_fold(99, b));
}

// Order ACROSS buckets is: swapping Sum's added and subtracted operand is a
// different node.
void test_group_fold_separates_roles() {
  Groups a, b;
  a[0] = {10};
  a[1] = {30};
  b[0] = {30};
  b[1] = {10};
  TEST_CHECK(hhds::group_fold(99, a) != hhds::group_fold(99, b));
}

// `x + x` is 2x, not an empty operand list -- the failure mode of a bare xor.
void test_group_fold_keeps_duplicates() {
  Groups a, b;
  a[0] = {10, 10};
  b[0] = {10};
  TEST_CHECK(hhds::group_fold(99, a) != hhds::group_fold(99, b));
}

// THE regression guard for the cache sites: group digests are combined
// positionally, so the keys must be SORTED. flat_hash_map iteration order is not
// stable across runs, and sim_color_plan's digest keys a persisted codegen
// cache -- an unsorted fold would turn cache hits into silent misses.
void test_group_fold_stable_under_map_insertion_order() {
  Groups a, b;
  for (int k : {0, 1, 2, 3, 4, 5, 6, 7}) {
    a[k] = {static_cast<uint64_t>(k) * 7 + 1};
  }
  for (int k : {7, 3, 0, 5, 1, 6, 2, 4}) {
    b[k] = {static_cast<uint64_t>(k) * 7 + 1};
  }
  TEST_CHECK(hhds::group_fold(0xabc, a) == hhds::group_fold(0xabc, b));
}

// The caller's tag/kind prefix must not be discardable.
void test_group_fold_base_participates() {
  Groups a;
  a[0] = {10};
  TEST_CHECK(hhds::group_fold(1, a) != hhds::group_fold(2, a));
}

}  // namespace

int main() {
  test_stable_under_id_reshuffle();
  test_op_discriminates();
  test_leaf_is_identity_free();
  test_subgraph_is_fold_of_output_cones();
  test_cycle_terminates();
  test_cycle_hash_ignores_allocation_ids();
  test_group_fold_order_independent_inside_a_group();
  test_group_fold_separates_roles();
  test_group_fold_keeps_duplicates();
  test_group_fold_stable_under_map_insertion_order();
  test_group_fold_base_participates();
  std::printf("cone_hash_test passed\n");
  return 0;
}
