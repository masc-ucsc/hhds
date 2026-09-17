// Correctness tests for the CACHED forward emission order.
//
// ForwardClassIterator used to recompute the whole topological order on every
// walk: each iterator copied Graph::forward_remaining_in_cache_, zeroed its own
// emitted bitset, and then decremented sink counts across every out-edge of
// every node it emitted -- O(E) per traversal, even when the graph had not
// changed since the last one. Graph::ensure_forward_caches() had ALREADY run
// exactly that algorithm to fill forward_pass2_cache_, so the answer was
// computed and thrown away once per walk.
//
// It now records that dry run (forward_pass1_bits_ / forward_pass2_cache_ /
// forward_emitted_bits_) and the iterator REPLAYS it. That is only sound while
// the recorded order is still a topological order of the live graph, so the
// incremental edge patcher gained an O(1) guard: adding driver -> sink keeps
// the cached order valid iff the driver already precedes the sink in it
// (forward_order_pos_); otherwise the cache is dropped and rebuilt. Deleting an
// edge only removes a constraint, so it never invalidates.
//
// The tests below pin down both halves:
//   * the replay reproduces a real topological order, on fresh graphs, after
//     order-preserving edge adds (the fast path that must NOT rebuild) and
//     after BACKWARD edge adds (the path that MUST rebuild),
//   * every alive node is still emitted exactly once -- including on cyclic
//     graphs, where the Tail phase carries the survivors,
//   * repeated and NESTED walks agree, which the old per-iterator scratch gave
//     for free and the shared cache has to earn,
//   * loop_break placement (cuts first/last/both/omit) is unchanged.
//
// A backward-add test is the one that fails loudly if the guard is dropped:
// two independent chains, then an edge from the LAST node of the second back
// to the FIRST node of the first. No cycle, but every cached position is now
// wrong.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "hhds/graph.hpp"

namespace {

#define TEST_CHECK(condition)                                                            \
  do {                                                                                   \
    if (!(condition)) {                                                                  \
      std::fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition); \
      std::abort();                                                                      \
    }                                                                                    \
  } while (false)

using hhds::Cut_placement;
using hhds::Graph;
using hhds::GraphLibrary;
using hhds::Node;
using hhds::Node_order;

std::vector<hhds::Nid> forward_order(Graph& g, Cut_placement cuts = Cut_placement::first) {
  std::vector<hhds::Nid> out;
  for (auto n : g.body().nodes(Node_order::forward, cuts)) {
    out.push_back(n.get_debug_nid());
  }
  return out;
}

std::vector<hhds::Nid> storage_order(Graph& g) {
  std::vector<hhds::Nid> out;
  for (auto n : g.body().nodes()) {
    out.push_back(n.get_debug_nid());
  }
  return out;
}

// Every alive body node appears exactly once, and for every edge whose two
// ends are both ordinary (non-loop_break) nodes the driver precedes the sink.
// loop_break nodes are the traversal's cut points: an edge in or out of one
// carries no ordering obligation.
void check_topological(Graph& g, const char* what) {
  const auto order   = forward_order(g);
  const auto storage = storage_order(g);

  if (order.size() != storage.size()) {
    std::fprintf(stderr, "%s: emitted %zu nodes, body holds %zu\n", what, order.size(), storage.size());
    std::abort();
  }
  ankerl::unordered_dense::map<hhds::Nid, size_t> pos;
  for (size_t i = 0; i < order.size(); ++i) {
    const bool inserted = pos.emplace(order[i], i).second;
    if (!inserted) {
      std::fprintf(stderr, "%s: node %llu emitted twice\n", what, static_cast<unsigned long long>(order[i]));
      std::abort();
    }
  }
  for (auto nid : storage) {
    TEST_CHECK(pos.count(nid) == 1);
  }

  for (auto driver : g.body().nodes()) {
    if (driver.is_loop_break()) {
      continue;
    }
    for (const auto e : driver.out_edges()) {
      const auto sink = e.sink.get_master_node();
      if (sink.is_loop_break()) {
        continue;
      }
      const auto dn = driver.get_debug_nid();
      const auto sn = sink.get_debug_nid();
      if (dn == sn) {
        continue;  // self edge: no obligation the order could satisfy
      }
      if (pos.count(dn) == 0 || pos.count(sn) == 0) {
        continue;  // an IO pseudo node, not a body node
      }
      if (pos[dn] >= pos[sn]) {
        std::fprintf(stderr,
                     "%s: NOT topological -- driver nid %llu at %zu, sink nid %llu at %zu\n",
                     what,
                     static_cast<unsigned long long>(dn),
                     pos[dn],
                     static_cast<unsigned long long>(sn),
                     pos[sn]);
        std::abort();
      }
    }
  }
}

void connect(Node a, Node b) { a.create_driver_pin().connect_sink(b.create_sink_pin()); }

// ---------------------------------------------------------------------------

// A random DAG with edges only from lower to higher storage index is the shape
// Pass 1 handles alone; the reverse-built chain below exercises Pass 2.
void test_random_dag() {
  GraphLibrary lib;
  auto         gio = lib.create_io("dag");
  gio->add_input("in", 0);
  gio->add_output("out", 0);
  auto graph = gio->create_graph();

  constexpr int     kN   = 200;
  std::vector<Node> n;
  n.reserve(kN);
  for (int i = 0; i < kN; ++i) {
    auto node = graph->create_node();
    node.set_type(hhds::Type{2});
    n.push_back(node);
  }
  uint64_t seed = 0x9e3779b97f4a7c15ULL;
  auto     rnd  = [&]() {
    seed ^= seed << 13;
    seed ^= seed >> 7;
    seed ^= seed << 17;
    return seed;
  };
  for (int i = 1; i < kN; ++i) {
    const int fan = 1 + static_cast<int>(rnd() % 3);
    for (int k = 0; k < fan; ++k) {
      const int j = static_cast<int>(rnd() % static_cast<uint64_t>(i));
      connect(n[j], n[i]);
    }
  }
  check_topological(*graph, "random dag");

  // Same graph, walked again: the second walk is the one that reads the cache
  // instead of building it, and it must agree with the first.
  const auto first  = forward_order(*graph);
  const auto second = forward_order(*graph);
  TEST_CHECK(first == second);
}

// Storage order is the exact REVERSE of the dependency order, so Pass 1 can
// only emit the last node and everything else is replayed from Pass 2.
void test_reverse_chain() {
  GraphLibrary lib;
  auto         gio = lib.create_io("rev");
  gio->add_input("in", 0);
  gio->add_output("out", 0);
  auto graph = gio->create_graph();

  constexpr int     kN = 64;
  std::vector<Node> n;
  n.reserve(kN);
  for (int i = 0; i < kN; ++i) {
    auto node = graph->create_node();
    node.set_type(hhds::Type{2});
    n.push_back(node);
  }
  for (int i = kN - 1; i > 0; --i) {
    connect(n[i], n[i - 1]);
  }
  check_topological(*graph, "reverse chain");

  const auto order = forward_order(*graph);
  TEST_CHECK(order.size() == static_cast<size_t>(kN));
  TEST_CHECK(order.front() == n[kN - 1].get_debug_nid());
  TEST_CHECK(order.back() == n[0].get_debug_nid());
}

// The fast path: an added edge that already points FORWARD in the cached order
// leaves it valid, so no rebuild happens -- and the walk must still be right.
void test_forward_add_keeps_order() {
  GraphLibrary lib;
  auto         gio = lib.create_io("fwdadd");
  gio->add_input("in", 0);
  gio->add_output("out", 0);
  auto graph = gio->create_graph();

  std::vector<Node> n;
  for (int i = 0; i < 16; ++i) {
    auto node = graph->create_node();
    node.set_type(hhds::Type{2});
    n.push_back(node);
  }
  for (int i = 0; i + 1 < 16; ++i) {
    connect(n[i], n[i + 1]);
  }
  const auto before = forward_order(*graph);  // builds the cache

  connect(n[2], n[11]);  // already 2 before 11 in the cached order
  check_topological(*graph, "forward add");
  const auto after = forward_order(*graph);
  TEST_CHECK(before == after);  // an order-preserving add must not reshuffle
}

// The guard: two INDEPENDENT chains, then an edge from the tail of the second
// back to the head of the first. Acyclic, but it inverts the cached order, so
// the cache must be dropped. Without the forward_order_pos_ check the replay
// hands back the stale order and check_topological aborts.
void test_backward_add_invalidates() {
  GraphLibrary lib;
  auto         gio = lib.create_io("bwdadd");
  gio->add_input("in", 0);
  gio->add_output("out", 0);
  auto graph = gio->create_graph();

  std::vector<Node> a;
  std::vector<Node> b;
  for (int i = 0; i < 8; ++i) {
    auto node = graph->create_node();
    node.set_type(hhds::Type{2});
    a.push_back(node);
  }
  for (int i = 0; i < 8; ++i) {
    auto node = graph->create_node();
    node.set_type(hhds::Type{2});
    b.push_back(node);
  }
  for (int i = 0; i + 1 < 8; ++i) {
    connect(a[i], a[i + 1]);
    connect(b[i], b[i + 1]);
  }

  const auto before = forward_order(*graph);
  TEST_CHECK(before.front() == a[0].get_debug_nid());  // chain A leads

  connect(b[7], a[0]);  // strictly backwards in `before`
  check_topological(*graph, "backward add");

  const auto after = forward_order(*graph);
  TEST_CHECK(after != before);
  TEST_CHECK(after.front() == b[0].get_debug_nid());
  TEST_CHECK(after.back() == a[7].get_debug_nid());
}

// Deleting an edge only removes a constraint, so the cached order stays valid;
// the walk must still cover every node.
void test_edge_delete() {
  GraphLibrary lib;
  auto         gio = lib.create_io("del");
  gio->add_input("in", 0);
  gio->add_output("out", 0);
  auto graph = gio->create_graph();

  std::vector<Node> n;
  for (int i = 0; i < 12; ++i) {
    auto node = graph->create_node();
    node.set_type(hhds::Type{2});
    n.push_back(node);
  }
  for (int i = 0; i + 1 < 12; ++i) {
    connect(n[i], n[i + 1]);
  }
  check_topological(*graph, "pre delete");

  {
    // out_edges() is a view over live storage, so snapshot before deleting.
    std::vector<hhds::Edge_class> snap;
    for (const auto e : n[5].out_edges()) {
      snap.push_back(e);
    }
    for (const auto& e : snap) {
      e.del_edge();
    }
  }
  check_topological(*graph, "post delete");
  TEST_CHECK(forward_order(*graph).size() == 12);
}

// A cycle has no topological order; the Tail phase must still emit each
// survivor exactly once and never loop forever.
void test_cycle_survivors() {
  GraphLibrary lib;
  auto         gio = lib.create_io("cyc");
  gio->add_input("in", 0);
  gio->add_output("out", 0);
  auto graph = gio->create_graph();

  std::vector<Node> n;
  for (int i = 0; i < 10; ++i) {
    auto node = graph->create_node();
    node.set_type(hhds::Type{2});
    n.push_back(node);
  }
  for (int i = 0; i + 1 < 10; ++i) {
    connect(n[i], n[i + 1]);
  }
  connect(n[9], n[3]);  // closes a cycle over 3..9

  const auto order = forward_order(*graph);
  TEST_CHECK(order.size() == 10);
  ankerl::unordered_dense::set<hhds::Nid> seen;
  for (auto nid : order) {
    TEST_CHECK(seen.insert(nid).second);
  }
}

// Tail nodes were emitted in storage order, not topological order. Removing
// a cycle edge must rebuild that order once the remaining graph is a DAG.
void test_break_cycle_reorders_tail() {
  GraphLibrary lib;
  auto         graph = lib.create_io("break_cycle")->create_graph();
  auto         a     = graph->create_node();
  auto         b     = graph->create_node();
  auto         c     = graph->create_node();
  for (auto node : {a, b, c}) {
    node.set_type(hhds::Type{2});
  }
  connect(a, b);
  connect(b, c);
  connect(c, a);
  TEST_CHECK(forward_order(*graph) == storage_order(*graph));

  a.out_edges().front().del_edge();
  check_topological(*graph, "broken cycle");
  const std::vector<hhds::Nid> expected{b.get_debug_nid(), c.get_debug_nid(), a.get_debug_nid()};
  TEST_CHECK(forward_order(*graph) == expected);
}

// The iterator no longer owns any scratch, so two walks in flight over one
// graph share the cache. They must not interfere.
void test_nested_walks() {
  GraphLibrary lib;
  auto         gio = lib.create_io("nested");
  gio->add_input("in", 0);
  gio->add_output("out", 0);
  auto graph = gio->create_graph();

  std::vector<Node> n;
  for (int i = 0; i < 24; ++i) {
    auto node = graph->create_node();
    node.set_type(hhds::Type{2});
    n.push_back(node);
  }
  for (int i = 0; i + 1 < 24; ++i) {
    connect(n[i], n[i + 1]);
  }
  const auto reference = forward_order(*graph);

  std::vector<hhds::Nid> outer;
  for (auto a : graph->body().nodes(Node_order::forward)) {
    outer.push_back(a.get_debug_nid());
    std::vector<hhds::Nid> inner;
    for (auto b : graph->body().nodes(Node_order::forward)) {
      inner.push_back(b.get_debug_nid());
    }
    TEST_CHECK(inner == reference);
  }
  TEST_CHECK(outer == reference);
}

// A walk pins an immutable snapshot, so a NESTED walk that forces a rebuild
// (because the outer body mutated in between) must not disturb the outer
// sequence. The old iterator got this for Pass 1 only -- it owned a private
// count copy but read the Pass-2 list live.
void test_rebuild_under_walk() {
  GraphLibrary lib;
  auto         gio = lib.create_io("under");
  gio->add_input("in", 0);
  gio->add_output("out", 0);
  auto graph = gio->create_graph();

  std::vector<Node> n;
  for (int i = 0; i < 20; ++i) {
    auto node = graph->create_node();
    node.set_type(hhds::Type{2});
    n.push_back(node);
  }
  for (int i = 19; i > 0; --i) {
    connect(n[i], n[i - 1]);  // reverse-built, so most of it replays from Pass 2
  }
  const auto reference = forward_order(*graph);

  std::vector<hhds::Nid> seen;
  bool                   mutated = false;
  for (auto node : graph->body().nodes(Node_order::forward)) {
    seen.push_back(node.get_debug_nid());
    if (!mutated && seen.size() == 3) {
      mutated = true;
      connect(n[0], n[19]);  // backwards in the pinned order: forces a rebuild
      // ...and a nested walk to actually perform that rebuild mid-iteration.
      const auto nested = forward_order(*graph);
      TEST_CHECK(nested.size() == 20);
      TEST_CHECK(nested != reference);
    }
  }
  TEST_CHECK(seen == reference);
}

// loop_break nodes are the only user-range sources. Their placement is a
// property of the cut policy, not of the cache.
void test_loop_break_placement() {
  GraphLibrary lib;
  auto         gio = lib.create_io("lb");
  gio->add_input("in", 0);
  gio->add_output("out", 0);
  auto graph = gio->create_graph();

  std::vector<Node> n;
  for (int i = 0; i < 6; ++i) {
    auto node = graph->create_node();
    node.set_type(hhds::Type{2});
    n.push_back(node);
  }
  auto flop = graph->create_node();
  flop.set_type(hhds::Type{1});  // bit 0 set == loop_break
  for (int i = 0; i + 1 < 6; ++i) {
    connect(n[i], n[i + 1]);
  }
  connect(n[5], flop);
  connect(flop, n[0]);  // the cut that makes the graph walkable

  const auto first = forward_order(*graph, Cut_placement::first);
  const auto last  = forward_order(*graph, Cut_placement::last);
  const auto both  = forward_order(*graph, Cut_placement::both);
  const auto omit  = forward_order(*graph, Cut_placement::omit);

  // `first` yields the cut IN Pass 1, at its storage position -- it does not
  // hoist it to the front; `last` replays it after everything else.
  auto count_flop = [&](const std::vector<hhds::Nid>& v) {
    size_t c = 0;
    for (auto nid : v) {
      c += (nid == flop.get_debug_nid()) ? 1 : 0;
    }
    return c;
  };
  TEST_CHECK(first.size() == 7);
  TEST_CHECK(count_flop(first) == 1);
  TEST_CHECK(last.size() == 7);
  TEST_CHECK(count_flop(last) == 1);
  TEST_CHECK(last.back() == flop.get_debug_nid());
  TEST_CHECK(both.size() == 8);
  TEST_CHECK(count_flop(both) == 2);
  TEST_CHECK(both.back() == flop.get_debug_nid());
  TEST_CHECK(omit.size() == 6);
  for (auto nid : omit) {
    TEST_CHECK(nid != flop.get_debug_nid());
  }
}

// Mutate and re-walk many times over. Every walk after the first reads a cache
// that the previous mutation either kept (and must have been right to keep) or
// dropped, so this is the end-to-end check on the patcher's guard.
void test_mutate_walk_loop() {
  GraphLibrary lib;
  auto         gio = lib.create_io("churn");
  gio->add_input("in", 0);
  gio->add_output("out", 0);
  auto graph = gio->create_graph();

  constexpr int     kN = 60;
  std::vector<Node> n;
  n.reserve(kN);
  for (int i = 0; i < kN; ++i) {
    auto node = graph->create_node();
    node.set_type(hhds::Type{2});
    n.push_back(node);
  }
  uint64_t seed = 12345;
  auto     rnd  = [&]() {
    seed ^= seed << 13;
    seed ^= seed >> 7;
    seed ^= seed << 17;
    return seed;
  };
  for (int round = 0; round < 40; ++round) {
    // Only i -> j with i < j, so the graph stays acyclic however the cached
    // order happens to be shaped; whether the add is forward or backward
    // RELATIVE TO THAT ORDER is exactly what the guard has to decide.
    const int i = static_cast<int>(rnd() % (kN - 1));
    const int j = i + 1 + static_cast<int>(rnd() % static_cast<uint64_t>(kN - 1 - i));
    connect(n[i], n[j]);
    check_topological(*graph, "churn");
  }
}

}  // namespace

int main() {
  test_random_dag();
  test_reverse_chain();
  test_forward_add_keeps_order();
  test_backward_add_invalidates();
  test_edge_delete();
  test_cycle_survivors();
  test_break_cycle_reorders_tail();
  test_nested_walks();
  test_rebuild_under_walk();
  test_loop_break_placement();
  test_mutate_walk_loop();
  std::printf("forward_order_cache_test: all checks passed\n");
  return 0;
}
