#include "hhds/graph.hpp"

#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cassert>
#include <csignal>
#include <filesystem>
#include <iostream>
#include <iterator>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "hhds/attrs/name.hpp"

namespace {

template <typename Range>
std::vector<hhds::Nid> collect_nids(Range&& range) {
  std::vector<hhds::Nid> order;
  for (auto node : range) {
    order.push_back(node.get_debug_nid());
  }
  return order;
}

template <typename Range>
std::vector<std::pair<hhds::Gid, hhds::Nid>> collect_gid_nids(Range&& range) {
  std::vector<std::pair<hhds::Gid, hhds::Nid>> order;
  for (auto node : range) {
    order.emplace_back(node.get_current_gid(), node.get_debug_nid());
  }
  return order;
}

constexpr hhds::Nid node_of(hhds::Nid nid) { return nid & ~static_cast<hhds::Nid>(3); }

// Resolve the hier-context Node_class for the user node (gid, nid) as visited by
// top->forward_hier(). The cross-boundary tests below use distinct leaf modules,
// so (gid, nid) identifies a node uniquely across the whole hierarchy walk.
hhds::Node_class find_hier_node(hhds::Graph* top, hhds::Gid gid, hhds::Nid nid) {
  hhds::Node_class found;
  bool             ok   = false;
  const hhds::Nid  want = node_of(nid);
  for (auto n : top->forward_hier()) {
    if (n.get_current_gid() == gid && node_of(n.get_debug_nid()) == want) {
      assert(!ok && "find_hier_node: node visited more than once");
      found = n;
      ok    = true;
    }
  }
  assert(ok && "find_hier_node: node not found in forward_hier walk");
  return found;
}

void test_declaration_api() {
  hhds::GraphLibrary lib;
  auto               gio = lib.create_io("alu");
  assert(gio);
  assert(gio->get_name() == "alu");
  assert(lib.find_io("alu") == gio);

  auto graph = gio->create_graph();
  assert(graph);
  assert(graph->get_io() == gio);
  assert(gio->get_graph() == graph);
  assert(gio->has_graph());
}

void test_wrapper_pin_connect_api() {
  hhds::GraphLibrary lib;

  auto and_gio = lib.create_io("and");
  and_gio->add_input("a", 0);
  and_gio->add_output("y", 0);

  auto top_gio = lib.create_io("top");
  top_gio->add_input("x", 1);
  top_gio->add_input("y", 2);
  top_gio->add_output("z", 0);

  auto graph = top_gio->create_graph();
  auto and1  = graph->create_node();
  and1.set_subnode(and_gio);

  auto and1_in  = and1.create_sink_pin("a");
  auto and1_out = and1.create_driver_pin("y");

  assert(and1.get_sink_pin("a") == and1_in);
  assert(and1.get_driver_pin("y") == and1_out);
  assert(and1_in.get_pin_name() == "a");
  assert(and1_out.get_pin_name() == "y");

  auto x = graph->get_input_pin("x");
  auto y = graph->get_input_pin("y");
  auto z = graph->get_output_pin("z");

  and1_in.connect_driver(x);
  and1_in.connect_driver(y);
  and1_out.connect_sink(z);

  auto input_edges = and1_in.inp_edges();
  assert(input_edges.size() == 2);
  for (const auto& edge : input_edges) {
    assert(edge.sink == and1_in);
  }

  auto output_edges = and1_out.out_edges();
  assert(output_edges.size() == 1);
  assert(output_edges.front().driver == and1_out);
  assert(output_edges.front().sink == z);
}

void test_subnode_accessors_round_trip_with_set_subnode() {
  hhds::GraphLibrary lib;

  auto leaf_gio  = lib.create_io("leaf");
  auto leaf      = leaf_gio->create_graph();
  auto other_gio = lib.create_io("other");
  auto other     = other_gio->create_graph();

  auto top_gio = lib.create_io("top");
  auto top     = top_gio->create_graph();

  // A plain node (no set_subnode) reports "no subnode" via every accessor.
  auto plain = top->create_node();
  assert(plain.get_subnode_gid() == hhds::Gid_invalid);
  assert(plain.get_subnode_io() == nullptr);
  assert(plain.get_subnode_graph() == nullptr);

  auto inst = top->create_node();
  inst.set_subnode(leaf_gio);
  assert(inst.get_subnode_gid() == leaf_gio->get_gid());
  assert(inst.get_subnode_io() == leaf_gio);
  assert(inst.get_subnode_graph() == leaf);

  // Retargeting must be observable through every accessor.
  inst.set_subnode(other_gio);
  assert(inst.get_subnode_gid() == other_gio->get_gid());
  assert(inst.get_subnode_io() == other_gio);
  assert(inst.get_subnode_graph() == other);
}

// Regression (LiveHD simple_hier_test via the slang reader): a sedge whose
// target is a node-as-sink (flag bits 00) at the SAME table index as the
// storing entry encoded to 0 — the empty-slot sentinel — so the driver-side
// half of the edge silently vanished while the sink-side half (flags 11,
// never zero) survived. pin_table and node_table indices are independent, so
// a pin at pin_table index N may legally drive port 0 of the node at
// node_table index N. In LiveHD the collision was sub.y (pin idx 6) ->
// get_mask.p0 (node idx 6); creating the big-pid const earlier merely
// shifted pin indices, which is why it appeared const-correlated.
void test_same_index_pin_to_node_port0_edge_survives() {
  hhds::GraphLibrary lib;
  auto               gio = lib.create_io("top");
  auto               g   = gio->create_graph();

  // node_table: 0=invalid, 1=INPUT, 2=OUTPUT, 3=CONST, then:
  auto filler   = g->create_node();  // node idx 4
  auto sub      = g->create_node();  // node idx 5 (the "instance")
  auto consumer = g->create_node();  // node idx 6 (sinks on port 0)

  // pin_table: 0=invalid; burn slots 1..5 so the driver pin lands at
  // pin_table idx 6 == consumer's node_table idx.
  for (hhds::Port_id p = 1; p <= 5; ++p) {
    (void)filler.create_sink_pin(p);
  }
  auto drv = sub.create_driver_pin(2);    // pin_table idx 6
  auto snk = consumer.create_sink_pin();  // node-as-pin (port 0, flags 00)

  snk.connect_driver(drv);

  // Sink-side half always survived.
  {
    auto in = consumer.inp_edges();
    assert(in.size() == 1);
    assert(in.front().driver == drv);
  }

  // Driver-side half: the regression. diff == 0 + flags 00 encoded to 0.
  {
    auto out = drv.out_edges();
    assert(out.size() == 1);
    assert(out.front().sink == snk);
  }
  assert(sub.out_edges().size() == 1);
  assert(sub.has_out_edges());

  // The edge must also be deletable and re-addable through the spill slot.
  drv.out_edges().front().del_edge();
  assert(drv.out_edges().empty());
  assert(consumer.inp_edges().empty());
  snk.connect_driver(drv);
  assert(drv.out_edges().size() == 1);
  assert(consumer.inp_edges().size() == 1);
}

// Same encoding hole on NodeEntry: a port0 -> port0 self-loop stores
// (diff == 0, flags 00) on the driver side.
void test_node_port0_self_loop_edge_survives() {
  hhds::GraphLibrary lib;
  auto               gio = lib.create_io("top");
  auto               g   = gio->create_graph();

  auto n = g->create_node();
  n.create_sink_pin().connect_driver(n.create_driver_pin());

  assert(n.inp_edges().size() == 1);
  assert(n.out_edges().size() == 1);
  assert(n.out_edges().front().sink == n.create_sink_pin());
  assert(n.has_out_edges());
  assert(n.has_inp_edges());

  n.out_edges().front().del_edge();
  assert(n.out_edges().empty());
  assert(n.inp_edges().empty());
}

// Pin_class::get_driver_pins(): the drivers feeding a sink pin. Covers the
// unconnected, single-driver, multi-driver, and node-as-pin(port 0) cases, and
// checks it stays in lockstep with inp_edges()[].driver.
void test_pin_get_driver_pins() {
  hhds::GraphLibrary lib;
  auto               gio = lib.create_io("top");
  auto               g   = gio->create_graph();

  auto src1 = g->create_node();
  auto src2 = g->create_node();
  auto sink = g->create_node();

  auto d1      = src1.create_driver_pin();   // port 0 driver (node-as-pin)
  auto d2      = src2.create_driver_pin(3);  // non-zero port driver
  auto s_named = sink.create_sink_pin(2);    // non-zero port sink
  auto s_port0 = sink.create_sink_pin();     // node-as-pin sink (port 0)

  // Unconnected sink -> no drivers.
  assert(s_named.get_driver_pins().empty());

  // Single driver.
  s_named.connect_driver(d1);
  {
    auto drivers = s_named.get_driver_pins();
    assert(drivers.size() == 1);
    assert(drivers[0] == d1);
  }

  // Multi-driver sink (storage permits it; netlist single-driver is only a
  // convention). get_driver_pins must surface every driver.
  s_named.connect_driver(d2);
  {
    auto drivers = s_named.get_driver_pins();
    assert(drivers.size() == 2);
    bool saw_d1 = false;
    bool saw_d2 = false;
    for (const auto& p : drivers) {
      saw_d1 |= (p == d1);
      saw_d2 |= (p == d2);
    }
    assert(saw_d1 && saw_d2);
  }

  // Node-as-pin (port 0) sink path uses the NodeEntry edge list.
  s_port0.connect_driver(d2);
  {
    auto drivers = s_port0.get_driver_pins();
    assert(drivers.size() == 1);
    assert(drivers[0] == d2);
  }

  // Stays in lockstep with inp_edges() (same drivers, same order).
  {
    auto edges   = s_named.inp_edges();
    auto drivers = s_named.get_driver_pins();
    assert(edges.size() == drivers.size());
    for (size_t i = 0; i < edges.size(); ++i) {
      assert(edges[i].driver == drivers[i]);
    }
  }
}

// out_edges() is a lazy, auto-scaling range. Covers: small inline fanout,
// node-composite ordering (node-as-pin edges before pin-list edges), a huge
// overflow fanout (lazy walk + early break), del_driver snapshot-then-delete on
// a high-degree pin, and empty ranges.
void test_out_edges_lazy_range() {
  hhds::GraphLibrary lib;
  auto               gio = lib.create_io("top");
  auto               g   = gio->create_graph();

  const auto contains = [](const std::vector<hhds::Pid>& v, hhds::Pid p) { return std::find(v.begin(), v.end(), p) != v.end(); };

  // --- small inline fanout from a port-0 driver pin ---
  {
    auto                   drv = g->create_node().create_driver_pin();
    std::vector<hhds::Pid> sink_pids;
    for (int i = 0; i < 3; ++i) {
      auto s = g->create_node().create_sink_pin();
      drv.connect_sink(s);
      sink_pids.push_back(s.get_debug_pid());
    }
    auto outs = drv.out_edges();
    assert(!outs.empty());
    assert(outs.size() == 3);
    assert(outs.front().driver == drv);
    int                    count = 0;
    std::vector<hhds::Pid> seen;
    for (const auto& e : outs) {
      assert(e.driver == drv);
      seen.push_back(e.sink.get_debug_pid());
      ++count;
    }
    assert(count == 3);
    for (auto p : sink_pids) {
      assert(contains(seen, p));
    }
  }

  // --- node-composite ordering: node-as-pin (port 0) edges, then pin-list ---
  {
    auto n   = g->create_node();
    auto d0  = n.create_driver_pin();   // port 0 -> stored on the NodeEntry
    auto d2  = n.create_driver_pin(2);  // named pin -> stored on its PinEntry
    auto s_a = g->create_node().create_sink_pin();
    auto s_b = g->create_node().create_sink_pin();
    d0.connect_sink(s_a);
    d2.connect_sink(s_b);
    std::vector<hhds::Pid> drivers;
    for (const auto& e : n.out_edges()) {
      drivers.push_back(e.driver.get_debug_pid());
    }
    assert(drivers.size() == 2);
    assert(drivers[0] == d0.get_debug_pid());  // node-as-pin first
    assert(drivers[1] == d2.get_debug_pid());  // pin-list second
  }

  // --- huge fanout (forces the overflow set): lazy walk, early break, del ---
  {
    auto      drv = g->create_node().create_driver_pin();
    const int kN  = 1000;
    for (int i = 0; i < kN; ++i) {
      drv.connect_sink(g->create_node().create_sink_pin());
    }
    assert(!drv.out_edges().empty());
    assert(drv.out_edges().size() == static_cast<size_t>(kN));

    int count = 0;
    for (const auto& e : drv.out_edges()) {
      assert(e.driver == drv);
      assert(e.sink.is_sink());
      ++count;
    }
    assert(count == kN);

    // Early break must not require walking the whole fanout (laziness).
    int seen = 0;
    for (const auto& e : drv.out_edges()) {
      (void)e;
      if (++seen == 5) {
        break;
      }
    }
    assert(seen == 5);

    // del_driver snapshots first, then deletes — safe on a high-degree pin.
    drv.del_driver();
    assert(drv.out_edges().empty());
    assert(drv.out_edges().size() == 0);
  }

  // --- empty range on an unconnected driver pin ---
  {
    auto drv = g->create_node().create_driver_pin();
    assert(drv.out_edges().empty());
    assert(drv.out_edges().size() == 0);
    int count = 0;
    for (const auto& e : drv.out_edges()) {
      (void)e;
      ++count;
    }
    assert(count == 0);
  }
}

void test_forward_class_returns_wrappers() {
  hhds::GraphLibrary lib;
  auto               gio   = lib.create_io("top");
  auto               graph = gio->create_graph();

  auto n1 = graph->create_node();
  auto n2 = graph->create_node();
  n1.create_driver_pin().connect_sink(n2.create_sink_pin());

  std::vector<hhds::Nid> order;
  for (auto node : graph->forward_class()) {
    assert(node.get_graph() == graph.get());
    assert(node.is_class());
    order.push_back(node.get_debug_nid());
  }

  assert(order.size() == 2);
  assert(order[0] == n1.get_debug_nid());
  assert(order[1] == n2.get_debug_nid());
}

void test_backward_class_returns_wrappers() {
  hhds::GraphLibrary lib;
  auto               gio   = lib.create_io("top");
  auto               graph = gio->create_graph();

  auto n1 = graph->create_node();
  auto n2 = graph->create_node();
  n1.create_driver_pin().connect_sink(n2.create_sink_pin());

  std::vector<hhds::Nid> order;
  for (auto node : graph->backward_class()) {
    assert(node.get_graph() == graph.get());
    assert(node.is_class());
    order.push_back(node.get_debug_nid());
  }

  assert(order.size() == 2);
  assert(order[0] == n2.get_debug_nid());
  assert(order[1] == n1.get_debug_nid());
}

void test_traversal_contexts_use_one_node_type() {
  hhds::GraphLibrary lib;
  auto               leaf_io = lib.create_io("leaf");
  auto               leaf    = leaf_io->create_graph();
  auto               leaf_n  = leaf->create_node();

  auto top_io = lib.create_io("top");
  auto top    = top_io->create_graph();
  auto inst   = top->create_node();
  inst.set_subnode(leaf_io);

  for (auto node : top->forward_class()) {
    assert(node.is_class());
    assert(!node.is_flat());
    assert(!node.is_hier());
  }

  bool saw_flat_leaf = false;
  for (auto node : top->forward_flat()) {
    assert(node.is_flat());
    assert(!node.is_hier());
    if (node.get_debug_nid() == leaf_n.get_debug_nid()) {
      auto pin = node.create_sink_pin();
      assert(pin.is_flat());
      saw_flat_leaf = true;
    }
  }
  assert(saw_flat_leaf);

  bool saw_hier_leaf = false;
  for (auto node : top->forward_hier()) {
    assert(node.is_hier());
    if (node.get_debug_nid() == leaf_n.get_debug_nid()) {
      auto pin = node.create_driver_pin();
      assert(pin.is_hier());
      saw_hier_leaf = true;
    }
  }
  assert(saw_hier_leaf);

  bool saw_backward_flat_leaf = false;
  for (auto node : top->backward_flat()) {
    assert(node.is_flat());
    assert(!node.is_hier());
    if (node.get_debug_nid() == leaf_n.get_debug_nid()) {
      auto pin = node.create_sink_pin();
      assert(pin.is_flat());
      saw_backward_flat_leaf = true;
    }
  }
  assert(saw_backward_flat_leaf);

  bool saw_backward_hier_leaf = false;
  for (auto node : top->backward_hier()) {
    assert(node.is_hier());
    if (node.get_debug_nid() == leaf_n.get_debug_nid()) {
      auto pin = node.create_driver_pin();
      assert(pin.is_hier());
      saw_backward_hier_leaf = true;
    }
  }
  assert(saw_backward_hier_leaf);
}

void test_forward_loop_break_is_source() {
  // A user node marked loop_break (odd type) acts as a forward source:
  // emitted unconditionally and does not propagate to sinks. The feedback
  // edge through the flop therefore does not block combinational users.
  hhds::GraphLibrary lib;
  auto               gio   = lib.create_io("top");
  auto               graph = gio->create_graph();

  auto n1 = graph->create_node();
  auto n2 = graph->create_node();
  auto n3 = graph->create_node();
  n3.set_type(3);  // bit 0 set -> is_loop_break

  n1.create_driver_pin().connect_sink(n2.create_sink_pin());
  n2.create_driver_pin().connect_sink(n3.create_sink_pin());
  // Feedback: flop drives n2, closing the loop. Without loop_break, this
  // would either cycle or force n3 before n2. With loop_break, n3 is a
  // source and does not contribute to n2's pending count.
  n3.create_driver_pin().connect_sink(n2.create_sink_pin());

  assert(n3.is_loop_break());
  assert(!n1.is_loop_break());
  assert(!n2.is_loop_break());

  std::vector<hhds::Nid> order;
  for (auto node : graph->forward_class()) {
    order.push_back(node.get_debug_nid());
  }
  assert(order.size() == 3);
  assert(order[0] == n1.get_debug_nid());
  assert(order[1] == n2.get_debug_nid());
  assert(order[2] == n3.get_debug_nid());
}

void test_backward_loop_break_is_sink() {
  hhds::GraphLibrary lib;
  auto               gio   = lib.create_io("top");
  auto               graph = gio->create_graph();

  auto n1 = graph->create_node();
  auto n2 = graph->create_node();
  auto n3 = graph->create_node();
  n3.set_type(3);  // bit 0 set -> is_loop_break

  n1.create_driver_pin().connect_sink(n2.create_sink_pin());
  n2.create_driver_pin().connect_sink(n3.create_sink_pin());
  n3.create_driver_pin().connect_sink(n2.create_sink_pin());

  std::vector<hhds::Nid> order;
  for (auto node : graph->backward_class()) {
    order.push_back(node.get_debug_nid());
  }
  assert(order.size() == 3);
  assert(order[0] == n3.get_debug_nid());
  assert(order[1] == n2.get_debug_nid());
  assert(order[2] == n1.get_debug_nid());
}

void test_forward_loop_break_visit_flags() {
  // n1 -> n2(loop_break/flop) -> n3. n2 is a forward source, so its edges
  // impose no ordering and all three nodes are emittable from the start;
  // Pass 1 yields them in storage order. The loop_break_first/last flags only
  // move n2 (the flop) within that sequence.
  hhds::GraphLibrary lib;
  auto               gio   = lib.create_io("top");
  auto               graph = gio->create_graph();

  auto n1 = graph->create_node();
  auto n2 = graph->create_node();
  auto n3 = graph->create_node();
  n2.set_type(3);  // bit 0 set -> loop_break
  assert(n2.is_loop_break());

  n1.create_driver_pin().connect_sink(n2.create_sink_pin());
  n2.create_driver_pin().connect_sink(n3.create_sink_pin());

  const auto a = n1.get_debug_nid();
  const auto b = n2.get_debug_nid();
  const auto c = n3.get_debug_nid();
  using V      = std::vector<hhds::Nid>;

  // Default == (loop_break_first=true, loop_break_last=false): flop visited first.
  assert(collect_nids(graph->forward_class()) == (V{a, b, c}));
  assert(collect_nids(graph->forward_class(true, false)) == (V{a, b, c}));
  // See the flop only at the end.
  assert(collect_nids(graph->forward_class(false, true)) == (V{a, c, b}));
  // See the flop both first and last.
  assert(collect_nids(graph->forward_class(true, true)) == (V{a, b, c, b}));
  // Never see the flop (but it still breaks the cycle for n3).
  assert(collect_nids(graph->forward_class(false, false)) == (V{a, c}));
}

void test_backward_loop_break_visit_flags() {
  // Mirror of the forward case: n2 is a backward sink, emitted high->low in
  // Pass 1; the flags move it within [c, b, a].
  hhds::GraphLibrary lib;
  auto               gio   = lib.create_io("top");
  auto               graph = gio->create_graph();

  auto n1 = graph->create_node();
  auto n2 = graph->create_node();
  auto n3 = graph->create_node();
  n2.set_type(3);  // bit 0 set -> loop_break
  assert(n2.is_loop_break());

  n1.create_driver_pin().connect_sink(n2.create_sink_pin());
  n2.create_driver_pin().connect_sink(n3.create_sink_pin());

  const auto a = n1.get_debug_nid();
  const auto b = n2.get_debug_nid();
  const auto c = n3.get_debug_nid();
  using V      = std::vector<hhds::Nid>;

  assert(collect_nids(graph->backward_class()) == (V{c, b, a}));
  assert(collect_nids(graph->backward_class(true, false)) == (V{c, b, a}));
  assert(collect_nids(graph->backward_class(false, true)) == (V{c, a, b}));
  assert(collect_nids(graph->backward_class(true, true)) == (V{c, b, a, b}));
  assert(collect_nids(graph->backward_class(false, false)) == (V{c, a}));
}

void test_forward_hier_loop_break_both_descends_once() {
  // A loop_break flop instance emitted both first and last (loop_break_first &&
  // loop_break_last) must have its subnode body walked only once — on the
  // first emission, not the LoopLast replay.
  hhds::GraphLibrary lib;

  auto leaf_gio = lib.create_io("leaf");
  leaf_gio->add_output("Q", 0, /*loop_break=*/true);  // makes the instance a loop_break
  auto leaf   = leaf_gio->create_graph();
  auto leaf_n = leaf->create_node();

  auto top_gio   = lib.create_io("top");
  auto top       = top_gio->create_graph();
  auto flop_inst = top->create_node();
  flop_inst.set_subnode(leaf_gio);
  assert(flop_inst.is_loop_break());

  const std::vector<std::pair<hhds::Gid, hhds::Nid>> expected{
      { top->get_gid(), flop_inst.get_debug_nid()},
      {leaf->get_gid(),    leaf_n.get_debug_nid()},
      { top->get_gid(), flop_inst.get_debug_nid()},
  };
  assert(collect_gid_nids(top->forward_hier(/*loop_break_first=*/true, /*loop_break_last=*/true)) == expected);
}

void test_forward_hier_globally_topological_across_stateful_sub() {
  // Regression: forward_hier must be a flat-module TOPOLOGICAL order — a driver
  // precedes its consumer even ACROSS a module boundary. The old per-body DFS
  // emitted a stateful (loop_break) submodule's whole subtree at the
  // loop_break_first slot, so the submodule's INPUT-side combinational logic came
  // out BEFORE the parent node that drives that input. Loop breaks belong at the
  // LEAF flop/mem, not the submodule instance.
  hhds::GraphLibrary lib;

  // reg_mod: din -> cs (comb) -> dout(loop_break). The loop_break OUTPUT makes an
  // instance of reg_mod a loop_break node at its parent; `cs` reads the module
  // input, so once flattened it depends on whatever drives the instance's `din`.
  auto reg_gio = lib.create_io("reg_mod_f");
  reg_gio->add_input("din", 0);
  reg_gio->add_output("dout", 0, /*loop_break=*/true);
  auto reg = reg_gio->create_graph();
  auto cs  = reg->create_node();
  cs.create_sink_pin().connect_driver(reg->get_input_pin("din"));  // cs reads din
  cs.create_driver_pin().connect_sink(reg->get_output_pin("dout"));

  // top: top_in -> g (comb) -> S.din ; S is the stateful submodule. Create S
  // BEFORE g so the DFS reaches the loop_break instance first (both are Pass-1
  // sources, tie-broken by storage order) — that is exactly the case the old
  // walk mis-ordered: it drained S's subtree (cs) before emitting g.
  auto top_gio = lib.create_io("top_f");
  top_gio->add_input("top_in", 0);
  auto top = top_gio->create_graph();
  auto s   = top->create_node();
  s.set_subnode(reg_gio);
  assert(s.is_loop_break() && "a submodule with internal state is loop_break at the parent");
  auto g = top->create_node();
  g.create_sink_pin().connect_driver(top->get_input_pin("top_in"));
  s.create_sink_pin("din").connect_driver(g.create_driver_pin());  // g drives S.din

  const auto order = collect_gid_nids(top->forward_hier());
  auto       pos   = [&](hhds::Gid gid, hhds::Nid nid) -> size_t {
    for (size_t i = 0; i < order.size(); ++i) {
      if (order[i].first == gid && node_of(order[i].second) == node_of(nid)) {
        return i;
      }
    }
    assert(false && "node not found in forward_hier walk");
    return 0;
  };
  // g (drives S.din) must precede cs (reads S.din), though cs lives inside the
  // loop_break submodule. The buggy DFS yielded [S, cs, g] (cs before g).
  assert(pos(top->get_gid(), g.get_debug_nid()) < pos(reg->get_gid(), cs.get_debug_nid())
         && "forward_hier: cross-boundary driver must precede its consumer");
}

void test_backward_hier_globally_topological_across_stateful_sub() {
  // Symmetric regression for backward_hier: a flat-module REVERSE topological
  // order, so a CONSUMER precedes its driver even across a module boundary. The
  // old DFS emitted a stateful submodule's OUTPUT-side logic before the parent
  // node that consumes the submodule output.
  hhds::GraphLibrary lib;

  auto reg_gio = lib.create_io("reg_mod_b");
  reg_gio->add_output("dout", 0, /*loop_break=*/true);
  auto reg = reg_gio->create_graph();
  auto cs  = reg->create_node();
  cs.create_driver_pin().connect_sink(reg->get_output_pin("dout"));  // cs drives dout

  auto top_gio = lib.create_io("top_b");
  auto top     = top_gio->create_graph();
  auto s       = top->create_node();
  s.set_subnode(reg_gio);
  assert(s.is_loop_break());
  auto g = top->create_node();
  g.create_sink_pin().connect_driver(s.create_driver_pin("dout"));  // g reads S.dout

  const auto order = collect_gid_nids(top->backward_hier());
  auto       pos   = [&](hhds::Gid gid, hhds::Nid nid) -> size_t {
    for (size_t i = 0; i < order.size(); ++i) {
      if (order[i].first == gid && node_of(order[i].second) == node_of(nid)) {
        return i;
      }
    }
    assert(false && "node not found in backward_hier walk");
    return 0;
  };
  // g (reads S.dout) must precede cs (drives S.dout inside the submodule).
  assert(pos(top->get_gid(), g.get_debug_nid()) < pos(reg->get_gid(), cs.get_debug_nid())
         && "backward_hier: cross-boundary consumer must precede its driver");
}

void test_forward_out_of_order_uses_pending_list() {
  // Exercises Pass 2: a driver created AFTER its sink in storage order.
  // Pass 1 skips the sink (count > 0), emits the driver, decrements the
  // sink to zero, and pushes it onto the pending FIFO.
  hhds::GraphLibrary lib;
  auto               gio   = lib.create_io("top");
  auto               graph = gio->create_graph();

  auto n1 = graph->create_node();
  auto n2 = graph->create_node();
  auto n3 = graph->create_node();

  n1.create_driver_pin().connect_sink(n2.create_sink_pin());
  n3.create_driver_pin().connect_sink(n1.create_sink_pin());

  std::vector<hhds::Nid> order;
  for (auto node : graph->forward_class()) {
    order.push_back(node.get_debug_nid());
  }
  assert(order.size() == 3);
  assert(order[0] == n3.get_debug_nid());
  assert(order[1] == n1.get_debug_nid());
  assert(order[2] == n2.get_debug_nid());
}

void test_backward_out_of_order_uses_pending_list() {
  hhds::GraphLibrary lib;
  auto               gio   = lib.create_io("top");
  auto               graph = gio->create_graph();

  auto n1 = graph->create_node();
  auto n2 = graph->create_node();
  auto n3 = graph->create_node();

  n1.create_driver_pin().connect_sink(n2.create_sink_pin());
  n3.create_driver_pin().connect_sink(n1.create_sink_pin());

  std::vector<hhds::Nid> order;
  for (auto node : graph->backward_class()) {
    order.push_back(node.get_debug_nid());
  }
  assert(order.size() == 3);
  assert(order[0] == n2.get_debug_nid());
  assert(order[1] == n1.get_debug_nid());
  assert(order[2] == n3.get_debug_nid());
}

void test_backward_cache_invalidates_after_set_type() {
  // Cycle n1<->n2 with no loop_break: both nodes fall through to the Tail
  // phase (Pass 1 and Pass 2 can't break the cycle), so order is [n2, n1].
  // After set_type marks n2 as loop_break, n2 becomes a sink: Pass 1 emits it
  // first and frees n1's count, yielding [n2, n1]. The Pass-1 vs Tail
  // distinction is what we exercise — a stale cache would surface as the
  // wrong relative ordering when nodes shift between phases.
  hhds::GraphLibrary lib;
  auto               gio   = lib.create_io("top");
  auto               graph = gio->create_graph();

  auto n1 = graph->create_node();
  auto n2 = graph->create_node();

  n1.create_driver_pin().connect_sink(n2.create_sink_pin());
  n2.create_driver_pin().connect_sink(n1.create_sink_pin());

  const std::vector<hhds::Nid> before{n2.get_debug_nid(), n1.get_debug_nid()};
  assert(collect_nids(graph->backward_class()) == before);

  n2.set_type(3);
  assert(n2.is_loop_break());

  const std::vector<hhds::Nid> after{n2.get_debug_nid(), n1.get_debug_nid()};
  assert(collect_nids(graph->backward_class()) == after);
}

void test_backward_cache_invalidates_after_edge_mutation() {
  hhds::GraphLibrary lib;
  auto               gio   = lib.create_io("top");
  auto               graph = gio->create_graph();

  auto n1 = graph->create_node();
  auto n2 = graph->create_node();
  auto n3 = graph->create_node();

  const std::vector<hhds::Nid> initial{n3.get_debug_nid(), n2.get_debug_nid(), n1.get_debug_nid()};
  assert(collect_nids(graph->backward_class()) == initial);

  n3.create_driver_pin().connect_sink(n1.create_sink_pin());

  const std::vector<hhds::Nid> after_edge{n2.get_debug_nid(), n1.get_debug_nid(), n3.get_debug_nid()};
  assert(collect_nids(graph->backward_class()) == after_edge);
}

void test_backward_diamond_fan_in() {
  // Diamond: n1 fans out to n2 and n3, both feed n4.
  // remaining_out: n1=2, n2=1, n3=1, n4=0. Pass 1 reverse emits n4 (sink),
  // then n3 and n2 (counts hit zero in storage-reverse order), then n1.
  hhds::GraphLibrary lib;
  auto               gio   = lib.create_io("top");
  auto               graph = gio->create_graph();

  auto n1 = graph->create_node();
  auto n2 = graph->create_node();
  auto n3 = graph->create_node();
  auto n4 = graph->create_node();

  n1.create_driver_pin().connect_sink(n2.create_sink_pin());
  n1.create_driver_pin().connect_sink(n3.create_sink_pin());
  n2.create_driver_pin().connect_sink(n4.create_sink_pin());
  n3.create_driver_pin().connect_sink(n4.create_sink_pin());

  const std::vector<hhds::Nid> expected{
      n4.get_debug_nid(),
      n3.get_debug_nid(),
      n2.get_debug_nid(),
      n1.get_debug_nid(),
  };
  assert(collect_nids(graph->backward_class()) == expected);
}

void test_backward_named_pin_and_declared_io_edges() {
  hhds::GraphLibrary lib;
  auto               gio = lib.create_io("top");
  gio->add_input("in", 1);
  gio->add_output("out", 2);
  auto graph = gio->create_graph();

  auto n1 = graph->create_node();
  auto n2 = graph->create_node();

  graph->get_input_pin("in").connect_sink(n1.create_sink_pin(11));
  n1.create_driver_pin(7).connect_sink(n2.create_sink_pin(8));
  n2.create_driver_pin(9).connect_sink(graph->get_output_pin("out"));

  const std::vector<hhds::Nid> expected{n2.get_debug_nid(), n1.get_debug_nid()};
  assert(collect_nids(graph->backward_class()) == expected);
}

void test_backward_skips_tombstones_after_delete() {
  hhds::GraphLibrary lib;
  auto               gio   = lib.create_io("top");
  auto               graph = gio->create_graph();

  auto n1 = graph->create_node();
  auto n2 = graph->create_node();
  auto n3 = graph->create_node();

  const std::vector<hhds::Nid> initial{n3.get_debug_nid(), n2.get_debug_nid(), n1.get_debug_nid()};
  assert(collect_nids(graph->backward_class()) == initial);

  n2.del_node();

  const std::vector<hhds::Nid> after_delete{n3.get_debug_nid(), n1.get_debug_nid()};
  assert(collect_nids(graph->backward_class()) == after_delete);
}

void test_backward_cycle_tail_without_loop_break() {
  hhds::GraphLibrary lib;
  auto               gio   = lib.create_io("top");
  auto               graph = gio->create_graph();

  auto n1 = graph->create_node();
  auto n2 = graph->create_node();

  n1.create_driver_pin().connect_sink(n2.create_sink_pin());
  n2.create_driver_pin().connect_sink(n1.create_sink_pin());

  const std::vector<hhds::Nid> expected{n2.get_debug_nid(), n1.get_debug_nid()};
  assert(collect_nids(graph->backward_class()) == expected);
}

// Incremental cache patching: deleting an edge after a traversal must keep
// both forward and backward iterations correct without a full cache rebuild.
void test_traversal_caches_after_edge_delete() {
  hhds::GraphLibrary lib;
  auto               gio   = lib.create_io("top");
  auto               graph = gio->create_graph();

  auto n1 = graph->create_node();
  auto n2 = graph->create_node();
  auto n3 = graph->create_node();

  auto n1d = n1.create_driver_pin();
  auto n2s = n2.create_sink_pin();
  n1d.connect_sink(n2s);
  n2.create_driver_pin().connect_sink(n3.create_sink_pin());

  // Prime both caches.
  const std::vector<hhds::Nid> fwd_chain{n1.get_debug_nid(), n2.get_debug_nid(), n3.get_debug_nid()};
  const std::vector<hhds::Nid> bwd_chain{n3.get_debug_nid(), n2.get_debug_nid(), n1.get_debug_nid()};
  assert(collect_nids(graph->forward_class()) == fwd_chain);
  assert(collect_nids(graph->backward_class()) == bwd_chain);

  // Delete the n1→n2 edge. n1 and {n2,n3} are now disconnected.
  for (auto e : n1d.out_edges()) {
    if (e.sink.get_debug_pid() == n2s.get_debug_pid()) {
      e.del_edge();
      break;
    }
  }

  // All nodes still emit. Forward must list n1 before n2 is unconstrained but
  // n2 before n3 still holds; backward is symmetric.
  auto fwd = collect_nids(graph->forward_class());
  auto bwd = collect_nids(graph->backward_class());
  assert(fwd.size() == 3 && bwd.size() == 3);
  auto pos
      = [](const std::vector<hhds::Nid>& v, hhds::Nid x) { return std::distance(v.begin(), std::find(v.begin(), v.end(), x)); };
  assert(pos(fwd, n2.get_debug_nid()) < pos(fwd, n3.get_debug_nid()));
  assert(pos(bwd, n3.get_debug_nid()) < pos(bwd, n2.get_debug_nid()));
}

// Incremental cache patching: deleting a pin must decrement counts for every
// edge connected to that pin.
void test_traversal_caches_after_pin_delete() {
  hhds::GraphLibrary lib;
  auto               gio   = lib.create_io("top");
  auto               graph = gio->create_graph();

  auto n1 = graph->create_node();
  auto n2 = graph->create_node();
  auto n3 = graph->create_node();

  n1.create_driver_pin().connect_sink(n2.create_sink_pin());
  auto n2d = n2.create_driver_pin();
  n2d.connect_sink(n3.create_sink_pin());

  // Prime caches.
  (void)collect_nids(graph->forward_class());
  (void)collect_nids(graph->backward_class());

  // Removing n2's driver pin severs n2→n3.
  n2d.del_pin();

  auto fwd = collect_nids(graph->forward_class());
  auto bwd = collect_nids(graph->backward_class());
  assert(fwd.size() == 3 && bwd.size() == 3);
  auto pos
      = [](const std::vector<hhds::Nid>& v, hhds::Nid x) { return std::distance(v.begin(), std::find(v.begin(), v.end(), x)); };
  // n1→n2 still holds.
  assert(pos(fwd, n1.get_debug_nid()) < pos(fwd, n2.get_debug_nid()));
  assert(pos(bwd, n2.get_debug_nid()) < pos(bwd, n1.get_debug_nid()));
}

// Incremental cache patching: adding a back-edge (driver.idx > sink.idx) must
// still produce a topologically valid emission via the Tail phase.
void test_traversal_caches_after_back_edge_add() {
  hhds::GraphLibrary lib;
  auto               gio   = lib.create_io("top");
  auto               graph = gio->create_graph();

  auto n1 = graph->create_node();
  auto n2 = graph->create_node();
  auto n3 = graph->create_node();

  // Prime caches with no edges.
  (void)collect_nids(graph->forward_class());

  // n3 (later idx) → n1 (earlier idx): driver after sink in cached order.
  n3.create_driver_pin().connect_sink(n1.create_sink_pin());

  auto fwd = collect_nids(graph->forward_class());
  assert(fwd.size() == 3);
  auto pos
      = [](const std::vector<hhds::Nid>& v, hhds::Nid x) { return std::distance(v.begin(), std::find(v.begin(), v.end(), x)); };
  // The new n3→n1 edge requires n3 before n1 in forward emission.
  assert(pos(fwd, n3.get_debug_nid()) < pos(fwd, n1.get_debug_nid()));
}

void test_backward_flat_exact_order_and_shared_body_dedup() {
  hhds::GraphLibrary lib;

  auto leaf_io = lib.create_io("leaf");
  auto leaf    = leaf_io->create_graph();
  auto leaf_n1 = leaf->create_node();
  auto leaf_n2 = leaf->create_node();
  leaf_n1.create_driver_pin().connect_sink(leaf_n2.create_sink_pin());

  auto top_io = lib.create_io("top");
  auto top    = top_io->create_graph();
  auto inst1  = top->create_node();
  auto inst2  = top->create_node();
  inst1.set_subnode(leaf_io);
  inst2.set_subnode(leaf_io);

  const std::vector<std::pair<hhds::Gid, hhds::Nid>> expected{
      { top->get_gid(),   inst2.get_debug_nid()},
      {leaf->get_gid(), leaf_n2.get_debug_nid()},
      {leaf->get_gid(), leaf_n1.get_debug_nid()},
      { top->get_gid(),   inst1.get_debug_nid()},
  };
  assert(collect_gid_nids(top->backward_flat()) == expected);
}

void test_backward_hier_exact_order_and_shared_body_per_instance() {
  hhds::GraphLibrary lib;

  auto leaf_io = lib.create_io("leaf");
  auto leaf    = leaf_io->create_graph();
  auto leaf_n1 = leaf->create_node();
  auto leaf_n2 = leaf->create_node();
  leaf_n1.create_driver_pin().connect_sink(leaf_n2.create_sink_pin());

  auto top_io = lib.create_io("top");
  auto top    = top_io->create_graph();
  auto inst1  = top->create_node();
  auto inst2  = top->create_node();
  inst1.set_subnode(leaf_io);
  inst2.set_subnode(leaf_io);

  const std::vector<std::pair<hhds::Gid, hhds::Nid>> expected{
      { top->get_gid(),   inst2.get_debug_nid()},
      {leaf->get_gid(), leaf_n2.get_debug_nid()},
      {leaf->get_gid(), leaf_n1.get_debug_nid()},
      { top->get_gid(),   inst1.get_debug_nid()},
      {leaf->get_gid(), leaf_n2.get_debug_nid()},
      {leaf->get_gid(), leaf_n1.get_debug_nid()},
  };
  assert(collect_gid_nids(top->backward_hier()) == expected);
}

void test_backward_hier_descends_into_nested_subnodes() {
  // 3-level nesting: top -> mid -> leaf. backward_hier from top must yield
  // each instance node first, then recurse into its body in backward order,
  // popping back to the parent's CONST. Mirrors the forward
  // test_hier_range_descends_into_nested_subnodes shape.
  hhds::GraphLibrary lib;

  auto leaf_gio = lib.create_io("leaf");
  auto leaf     = leaf_gio->create_graph();

  auto mid_gio          = lib.create_io("mid");
  auto mid              = mid_gio->create_graph();
  auto mid_inst_of_leaf = mid->create_node();
  mid_inst_of_leaf.set_subnode(leaf_gio);

  auto top_gio         = lib.create_io("top");
  auto top             = top_gio->create_graph();
  auto top_inst_of_mid = top->create_node();
  top_inst_of_mid.set_subnode(mid_gio);

  const std::vector<std::pair<hhds::Gid, hhds::Nid>> expected{
      {top->get_gid(),  top_inst_of_mid.get_debug_nid()},
      {mid->get_gid(), mid_inst_of_leaf.get_debug_nid()},
  };
  assert(collect_gid_nids(top->backward_hier()) == expected);
}

void test_subnode_with_loop_break_pin_marks_node() {
  // set_subnode must stamp the node type with bit 0 set iff the subnode's
  // declared IO contains any loop_break pin (flop/register instance).
  hhds::GraphLibrary lib;

  auto flop_gio = lib.create_io("flop");
  flop_gio->add_input("D", 0, /*loop_break=*/false);
  flop_gio->add_output("Q", 0, /*loop_break=*/true);
  (void)flop_gio->create_graph();

  auto buf_gio = lib.create_io("buf");
  buf_gio->add_input("a", 0);
  buf_gio->add_output("y", 0);
  (void)buf_gio->create_graph();

  auto top_gio = lib.create_io("top");
  auto top     = top_gio->create_graph();

  auto flop_inst = top->create_node();
  flop_inst.set_subnode(flop_gio);
  auto buf_inst = top->create_node();
  buf_inst.set_subnode(buf_gio);

  assert(flop_inst.is_loop_break());
  assert((flop_inst.get_type() & 1) == 1);
  assert(!buf_inst.is_loop_break());
  assert((buf_inst.get_type() & 1) == 0);
}

void test_hier_range_flat_graph_is_empty() {
  // A graph with no subnodes should produce an empty hier_range — even when
  // it has plain (non-subnode) graph nodes and IO pins.
  hhds::GraphLibrary lib;
  auto               gio = lib.create_io("flat");
  gio->add_input("in", 0);
  gio->add_output("out", 0);
  auto graph = gio->create_graph();
  (void)graph->create_node();
  (void)graph->create_node();

  size_t count = 0;
  for (auto inst : graph->hier_range()) {
    (void)inst;
    ++count;
  }
  assert(count == 0);
}

void test_hier_range_yields_one_per_subnode() {
  // Two leaf submodules, three top-level instances (two of leaf_a, one of
  // leaf_b). hier_range should yield exactly 3 entries — one per instance,
  // even when multiple instances share a GraphIO.
  hhds::GraphLibrary lib;

  auto leaf_a = lib.create_io("leaf_a");
  (void)leaf_a->create_graph();
  auto leaf_b = lib.create_io("leaf_b");
  (void)leaf_b->create_graph();

  auto top_gio = lib.create_io("top");
  auto top     = top_gio->create_graph();

  auto i1 = top->create_node();
  i1.set_subnode(leaf_a);
  auto i2 = top->create_node();
  i2.set_subnode(leaf_a);
  auto i3 = top->create_node();
  i3.set_subnode(leaf_b);

  std::vector<hhds::Gid> targets;
  std::vector<hhds::Nid> parents;
  for (auto inst : top->hier_range()) {
    targets.push_back(inst.get_target_gid());
    parents.push_back(inst.get_parent_nid());
    assert(inst.get_parent_graph() == top.get());
    assert(inst.is_valid());
  }
  assert(targets.size() == 3);
  // Tree inserts siblings in set_subnode call order, so we expect a, a, b.
  assert(targets[0] == leaf_a->get_gid());
  assert(targets[1] == leaf_a->get_gid());
  assert(targets[2] == leaf_b->get_gid());
  assert(parents[0] == i1.get_debug_nid());
  assert(parents[1] == i2.get_debug_nid());
  assert(parents[2] == i3.get_debug_nid());
}

void test_hier_range_descends_into_nested_subnodes() {
  // top contains mid; mid contains leaf. hier_range from top should yield
  // both the mid instance AND the leaf instance (seen through mid's body),
  // in that order (mid first, then leaf via recursion).
  hhds::GraphLibrary lib;

  auto leaf_gio = lib.create_io("leaf");
  (void)leaf_gio->create_graph();

  auto mid_gio          = lib.create_io("mid");
  auto mid              = mid_gio->create_graph();
  auto mid_inst_of_leaf = mid->create_node();
  mid_inst_of_leaf.set_subnode(leaf_gio);

  auto top_gio         = lib.create_io("top");
  auto top             = top_gio->create_graph();
  auto top_inst_of_mid = top->create_node();
  top_inst_of_mid.set_subnode(mid_gio);

  std::vector<hhds::Gid> targets;
  for (auto inst : top->hier_range()) {
    targets.push_back(inst.get_target_gid());
  }
  assert(targets.size() == 2);
  assert(targets[0] == mid_gio->get_gid());
  assert(targets[1] == leaf_gio->get_gid());
}

#ifdef NDEBUG
void test_hier_range_cycle_guard() {
  // Release-only: structure-tree cycles are blocked at insert time by the
  // debug assertion in set_subnode (would_create_cycle). When asserts are
  // off, the runtime guard in HierIterator (active_graphs_) is the sole
  // line of defense — verify it still terminates on a self-referencing
  // submodule and doesn't infinite-loop.
  hhds::GraphLibrary lib;

  auto self_gio = lib.create_io("self");
  auto self     = self_gio->create_graph();
  auto inst     = self->create_node();
  inst.set_subnode(self_gio);

  size_t count = 0;
  for (auto it : self->hier_range()) {
    assert(it.get_target_gid() == self_gio->get_gid());
    ++count;
    assert(count < 100 && "cycle guard failed — hier_range is not terminating");
  }
  assert(count == 1);
}
#endif

void test_hier_range_target_graph_and_parent_node() {
  // get_target_graph() should resolve to the actual body, and
  // get_parent_node() should return a valid hier-context Node_class usable
  // for attribute queries.
  hhds::GraphLibrary lib;
  auto               leaf_gio = lib.create_io("leaf");
  auto               leaf     = leaf_gio->create_graph();

  auto top_gio = lib.create_io("top");
  auto top     = top_gio->create_graph();
  auto inst    = top->create_node();
  inst.set_subnode(leaf_gio);

  auto hier = top->hier_range();
  auto it   = hier.begin();
  assert(it != hier.end());
  auto handle = *it;
  assert(handle.get_target_graph() == leaf);
  auto pnode = handle.get_parent_node();
  assert(pnode.is_valid());
  assert(pnode.is_hier());
  assert(pnode.get_debug_nid() == inst.get_debug_nid());
}

void test_set_subnode_linear_deep_chain_ok() {
  // Linear chain a -> b -> c -> d. No cycle, so set_subnode must succeed
  // at every level. Walking a's hier_range yields 3 instances.
  hhds::GraphLibrary lib;
  auto               a_gio = lib.create_io("a");
  auto               a     = a_gio->create_graph();
  auto               b_gio = lib.create_io("b");
  auto               b     = b_gio->create_graph();
  auto               c_gio = lib.create_io("c");
  auto               c     = c_gio->create_graph();
  auto               d_gio = lib.create_io("d");
  (void)d_gio->create_graph();

  a->create_node().set_subnode(b_gio);
  b->create_node().set_subnode(c_gio);
  c->create_node().set_subnode(d_gio);

  size_t count = 0;
  for (auto inst : a->hier_range()) {
    (void)inst;
    ++count;
  }
  assert(count == 3);
}

void test_set_subnode_diamond_ok() {
  // Diamond DAG: top instantiates b1 and b2; both b1 and b2 instantiate
  // leaf. This is a DAG (leaf is reached two ways), not a cycle —
  // would_create_cycle must return false for every set_subnode call here.
  hhds::GraphLibrary lib;
  auto               top_gio  = lib.create_io("top");
  auto               top      = top_gio->create_graph();
  auto               b1_gio   = lib.create_io("b1");
  auto               b1       = b1_gio->create_graph();
  auto               b2_gio   = lib.create_io("b2");
  auto               b2       = b2_gio->create_graph();
  auto               leaf_gio = lib.create_io("leaf");
  (void)leaf_gio->create_graph();

  top->create_node().set_subnode(b1_gio);
  top->create_node().set_subnode(b2_gio);
  b1->create_node().set_subnode(leaf_gio);
  b2->create_node().set_subnode(leaf_gio);

  // top -> b1 -> leaf, top -> b2 -> leaf : 4 instances total.
  size_t count = 0;
  for (auto inst : top->hier_range()) {
    (void)inst;
    ++count;
  }
  assert(count == 4);
}

#ifndef NDEBUG
// Death-test helper: forks, runs `body` in the child, expects the child to
// die via SIGABRT (the libc assert path). Returns true on the expected
// outcome. Used to encode "this set_subnode call must trigger the cycle
// assertion" without aborting the whole test binary.
template <typename Fn>
bool expect_assert_abort(Fn body) {
  pid_t pid = fork();
  assert(pid >= 0 && "fork failed");
  if (pid == 0) {
    // Child: silence assert's stderr noise so the test log stays readable;
    // we only care about the exit signal, not the message.
    int devnull = ::open("/dev/null", O_WRONLY);
    if (devnull >= 0) {
      ::dup2(devnull, STDERR_FILENO);
      ::close(devnull);
    }
    body();
    _exit(0);  // body returned without aborting — failure mode
  }
  int status = 0;
  waitpid(pid, &status, 0);
  return WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT;
}

void test_set_subnode_self_cycle_aborts() {
  // Self-instantiation: graph "self" tries to contain an instance of itself.
  // would_create_cycle short-circuits on target_gid == self_gid_.
  const bool aborted = expect_assert_abort([]() {
    hhds::GraphLibrary lib;
    auto               gio = lib.create_io("self");
    auto               g   = gio->create_graph();
    g->create_node().set_subnode(gio);
  });
  assert(aborted && "self-cycle did not trigger the set_subnode assertion");
}

void test_set_subnode_indirect_cycle_aborts() {
  // Indirect cycle: a contains b, then b tries to contain a. Exercises the
  // BFS path of would_create_cycle (not just the self-gid short circuit).
  const bool aborted = expect_assert_abort([]() {
    hhds::GraphLibrary lib;
    auto               a_gio = lib.create_io("a");
    auto               a     = a_gio->create_graph();
    auto               b_gio = lib.create_io("b");
    auto               b     = b_gio->create_graph();
    a->create_node().set_subnode(b_gio);
    b->create_node().set_subnode(a_gio);
  });
  assert(aborted && "indirect cycle did not trigger the set_subnode assertion");
}
#endif

void test_set_subnode_retarget_ok() {
  // Re-calling set_subnode on the same node with a different target must
  // succeed (no cycle is created — same node, different target). The
  // structure tree retains a single child for this subnode, retargeted
  // to the new gid.
  hhds::GraphLibrary lib;
  auto               a_gio = lib.create_io("a");
  auto               a     = a_gio->create_graph();
  auto               b_gio = lib.create_io("b");
  (void)b_gio->create_graph();
  auto c_gio = lib.create_io("c");
  (void)c_gio->create_graph();

  auto inst = a->create_node();
  inst.set_subnode(b_gio);
  inst.set_subnode(c_gio);

  size_t    count       = 0;
  hhds::Gid last_target = hhds::Gid_invalid;
  for (auto h : a->hier_range()) {
    last_target = h.get_target_gid();
    ++count;
  }
  assert(count == 1);
  assert(last_target == c_gio->get_gid());
}

// Task 1m-C: GraphLibrary::load_merge — assemble several saved libraries into
// one. With name-hash gids a name keeps the same gid across libraries, so the
// merge is conflict-free and dedups by name.
void test_load_merge() {
  namespace fs    = std::filesystem;
  const auto base = fs::temp_directory_path() / "hhds_load_merge_test";
  fs::remove_all(base);
  const auto dirA = (base / "A").string();
  const auto dirB = (base / "B").string();

  hhds::Gid foo_gid = hhds::Gid_invalid;
  hhds::Gid bar_gid = hhds::Gid_invalid;
  {
    hhds::GraphLibrary a;
    auto               foo_gio = a.create_io("foo");
    foo_gio->add_input("x", 0);
    foo_gio->add_output("y", 0);
    {
      auto g = foo_gio->create_graph();
      g->create_node();
    }  // publish on scope-exit
    foo_gid = foo_gio->get_gid();
    a.save(dirA);
  }
  {
    hhds::GraphLibrary b;
    auto               bar_gio = b.create_io("bar");
    bar_gio->add_input("p", 0);
    bar_gio->add_output("q", 0);
    {
      auto g = bar_gio->create_graph();
      g->create_node();
    }
    bar_gid = bar_gio->get_gid();
    b.save(dirB);
  }

  hhds::GraphLibrary c;
  c.load_merge(dirA);
  c.load_merge(dirB);

  auto cfoo = c.find_io("foo");
  auto cbar = c.find_io("bar");
  assert(cfoo && cbar);
  assert(cfoo->get_gid() == foo_gid && "name-hash gid preserved across merge");
  assert(cbar->get_gid() == bar_gid);
  assert(c.has_graph(cfoo->get_gid()) && c.has_graph(cbar->get_gid()));
  assert(c.all_gids().size() == 2);

  // Dedup: re-merging A keeps one `foo` (no duplicate, same GraphIO).
  c.load_merge(dirA);
  assert(c.find_io("foo") == cfoo);
  assert(c.all_gids().size() == 2);

  fs::remove_all(base);
}

// Shared in-memory source map: a Forest (LNAST) and a GraphLibrary (LGraph)
// sharing one db directory must share ONE Source_locator and a single
// srcmap.txt writer, so tree-side and graph-side provenance both survive a
// co-save instead of clobbering each other (the old per-structure behavior).
void test_shared_source_map() {
  namespace fs   = std::filesystem;
  const auto dir = (fs::temp_directory_path() / "hhds_shared_srcmap_test").string();
  fs::remove_all(dir);

  hhds::SourceId id_tree = 0, id_graph = 0;
  {
    auto               forest = hhds::Forest::create();
    hhds::GraphLibrary lib;
    // One shared table; the library is the sole srcmap.txt writer.
    forest->share_source_map(lib.source_map_shared(), /*persist=*/false);
    assert(&forest->source_map() == &lib.source_map() && "Forest and library share one table");

    // Tree-side span minted directly into the shared map.
    id_tree = forest->source_map().mint("tree_file.v", 0, 5, 1);

    // Graph-side span minted into a graph's locator; folds into the shared map at save.
    auto gio = lib.create_io("top");
    {
      auto g = gio->create_graph();
      (void)g->create_node();
      id_graph = g->source_locator().mint("graph_file.v", 10, 20, 2);
    }

    lib.save(dir);      // folds graph delta + writes srcmap.txt (+ library.txt, bodies)
    forest->save(dir);  // borrower: writes forest.txt, but must NOT touch srcmap.txt
  }

  // The borrower's save must not clobber or empty-remove the shared table.
  assert(fs::exists(fs::path(dir) / "srcmap.txt") && "shared srcmap.txt survives borrower save");

  // Reload into a fresh shared map; BOTH spans must resolve (no clobber).
  {
    auto               forest2 = hhds::Forest::create();
    hhds::GraphLibrary lib2;
    forest2->share_source_map(lib2.source_map_shared(), /*persist=*/false);
    lib2.load(dir);      // owner loads srcmap.txt into the shared table
    forest2->load(dir);  // borrower shares the already-loaded table

    const auto a_tree  = lib2.source_map().resolve(id_tree);
    const auto a_graph = lib2.source_map().resolve(id_graph);
    assert(a_tree && a_tree->path == "tree_file.v" && "tree-side provenance survived shared save");
    assert(a_graph && a_graph->path == "graph_file.v" && "graph-side provenance survived shared save");
    assert(forest2->source_map().resolve(id_tree) && "shared table visible from the forest side");
  }

  fs::remove_all(dir);
}

// ===========================================================================
// EXPECTED-BEHAVIOR SPEC: cross-boundary inp_edges()/out_edges() in hier context
// ===========================================================================
// These encode the agreed contract: in a HIER traversal, inp_edges()/out_edges()
// must NOT stop at a sub-module's GraphIO boundary pin. The reported driver/sink
// pin must hop through the instance(s) that wrap the module — up to the caller
// and/or down into a callee — crossing the boundary as many times as needed,
// until it reaches a real driver/sink leaf. Only the *top* (starting) graph's
// own IO pins may surface as a driver/sink.
//
// NOTE: these are RED against current hhds (which returns the local boundary
// pin). They define what the resolver must produce before we implement it.

// Single boundary, both directions:
//
//   top:   src ──▶ r.a │ leaf:  a ──▶ buf ──▶ y │ r.y ──▶ dst
//
// From inside `leaf` (instance r), buf's driver/sink must resolve up into `top`.
void test_hier_edges_cross_one_boundary_EXPECTED() {
  hhds::GraphLibrary lib;

  auto leaf_io = lib.create_io("leaf");
  leaf_io->add_input("a", 1);
  leaf_io->add_output("y", 2);
  auto leaf = leaf_io->create_graph();
  auto buf  = leaf->create_node();
  leaf->get_input_pin("a").connect_sink(buf.create_sink_pin());     // a   -> buf (port 0)
  buf.create_driver_pin().connect_sink(leaf->get_output_pin("y"));  // buf -> y   (port 0)

  auto top_io = lib.create_io("top");
  auto top    = top_io->create_graph();
  auto src    = top->create_node();
  auto r      = top->create_node();
  r.set_subnode(leaf_io);
  auto dst = top->create_node();
  src.create_driver_pin().connect_sink(r.create_sink_pin(1));  // src -> r.a (port 1)
  r.create_driver_pin(2).connect_sink(dst.create_sink_pin());  // r.y -> dst (port 2)

  const auto buf_h = find_hier_node(top.get(), leaf->get_gid(), buf.get_debug_nid());

  // inp_edges: driver resolves up to src (top), NOT leaf's input pin "a".
  const auto ins = buf_h.inp_edges();
  assert(ins.size() == 1);
  const auto drv = ins[0].driver;
  assert(drv.get_current_gid() == top->get_gid());
  assert(node_of(drv.get_master_node().get_debug_nid()) == node_of(src.get_debug_nid()));
  assert(drv.is_driver());

  // out_edges: sink resolves up to dst (top), NOT leaf's output pin "y".
  const auto outs = buf_h.out_edges();
  assert(outs.size() == 1);
  const auto snk = outs.front().sink;
  assert(snk.get_current_gid() == top->get_gid());
  assert(node_of(snk.get_master_node().get_debug_nid()) == node_of(dst.get_debug_nid()));
  assert(snk.is_sink());
}

// Up THEN down through several nodes, plus stopping at top-level IO:
//
//   top "pi" ─▶ ra(leafA): a─▶bufA─▶y │ ra.y ─▶ rb.a │ rb(leafB): a─▶bufB─▶y │ rb.y ─▶ top "po"
//
//   bufA.out_edges -> (up out of leafA via ra.y, down into rb) -> bufB sink
//   bufB.inp_edges -> (up out of leafB via rb.a, down into ra) -> bufA driver
//   bufA.inp_edges -> (up via ra.a) -> top input  "pi"   (top-level IO: visible)
//   bufB.out_edges -> (up via rb.y) -> top output "po"   (top-level IO: visible)
void test_hier_edges_cross_up_then_down_EXPECTED() {
  hhds::GraphLibrary lib;

  auto leafA_io = lib.create_io("leafA");
  leafA_io->add_input("a", 1);
  leafA_io->add_output("y", 2);
  auto leafA = leafA_io->create_graph();
  auto bufA  = leafA->create_node();
  leafA->get_input_pin("a").connect_sink(bufA.create_sink_pin());
  bufA.create_driver_pin().connect_sink(leafA->get_output_pin("y"));

  auto leafB_io = lib.create_io("leafB");
  leafB_io->add_input("a", 1);
  leafB_io->add_output("y", 2);
  auto leafB = leafB_io->create_graph();
  auto bufB  = leafB->create_node();
  leafB->get_input_pin("a").connect_sink(bufB.create_sink_pin());
  bufB.create_driver_pin().connect_sink(leafB->get_output_pin("y"));

  auto top_io = lib.create_io("top");
  top_io->add_input("pi", 1);
  top_io->add_output("po", 2);
  auto top = top_io->create_graph();
  auto ra  = top->create_node();
  ra.set_subnode(leafA_io);
  auto rb = top->create_node();
  rb.set_subnode(leafB_io);
  top->get_input_pin("pi").connect_sink(ra.create_sink_pin(1));     // pi   -> ra.a
  ra.create_driver_pin(2).connect_sink(rb.create_sink_pin(1));      // ra.y -> rb.a
  rb.create_driver_pin(2).connect_sink(top->get_output_pin("po"));  // rb.y -> po

  const auto bufA_h = find_hier_node(top.get(), leafA->get_gid(), bufA.get_debug_nid());
  const auto bufB_h = find_hier_node(top.get(), leafB->get_gid(), bufB.get_debug_nid());

  // bufA.out -> bufB sink (up out of leafA, down into leafB)
  {
    const auto outs = bufA_h.out_edges();
    assert(outs.size() == 1);
    const auto snk = outs.front().sink;
    assert(snk.get_current_gid() == leafB->get_gid());
    assert(node_of(snk.get_master_node().get_debug_nid()) == node_of(bufB.get_debug_nid()));
    assert(snk.is_sink());
  }
  // bufB.inp -> bufA driver (up out of leafB, down into leafA)
  {
    const auto ins = bufB_h.inp_edges();
    assert(ins.size() == 1);
    const auto drv = ins[0].driver;
    assert(drv.get_current_gid() == leafA->get_gid());
    assert(node_of(drv.get_master_node().get_debug_nid()) == node_of(bufA.get_debug_nid()));
    assert(drv.is_driver());
  }
  // bufA.inp -> top input "pi" (resolution stops at the starting graph's own IO)
  {
    const auto ins = bufA_h.inp_edges();
    assert(ins.size() == 1);
    const auto drv = ins[0].driver;
    assert(drv.get_current_gid() == top->get_gid());
    assert(node_of(drv.get_master_node().get_debug_nid()) == node_of(top->get_input_node().get_debug_nid()));
    assert(drv.get_pin_name() == "pi");
    assert(drv.is_driver());
  }
  // bufB.out -> top output "po"
  {
    const auto outs = bufB_h.out_edges();
    assert(outs.size() == 1);
    const auto snk = outs.front().sink;
    assert(snk.get_current_gid() == top->get_gid());
    assert(node_of(snk.get_master_node().get_debug_nid()) == node_of(top->get_output_node().get_debug_nid()));
    assert(snk.get_pin_name() == "po");
    assert(snk.is_sink());
  }
}

// Three levels of pass-through wrappers. From inside the deepest leaf, edges
// must resolve up TWO module boundaries to the top's primary IO — exercising
// the instance-chain reconstruction:
//   top "pi" ─▶ mi.a │ mid: a ─▶ li.a │ leaf: a ─▶ bufL ─▶ y │ li.y ─▶ mid.y │ mi.y ─▶ top "po"
void test_hier_edges_three_levels_EXPECTED() {
  hhds::GraphLibrary lib;

  auto leaf_io = lib.create_io("leaf3");
  leaf_io->add_input("a", 1);
  leaf_io->add_output("y", 2);
  auto leaf = leaf_io->create_graph();
  auto bufL = leaf->create_node();
  leaf->get_input_pin("a").connect_sink(bufL.create_sink_pin());
  bufL.create_driver_pin().connect_sink(leaf->get_output_pin("y"));

  auto mid_io = lib.create_io("mid3");
  mid_io->add_input("a", 1);
  mid_io->add_output("y", 2);
  auto mid = mid_io->create_graph();
  auto li  = mid->create_node();
  li.set_subnode(leaf_io);
  mid->get_input_pin("a").connect_sink(li.create_sink_pin(1));     // mid.a -> li.a
  li.create_driver_pin(2).connect_sink(mid->get_output_pin("y"));  // li.y -> mid.y

  auto top_io = lib.create_io("top3");
  top_io->add_input("pi", 1);
  top_io->add_output("po", 2);
  auto top = top_io->create_graph();
  auto mi  = top->create_node();
  mi.set_subnode(mid_io);
  top->get_input_pin("pi").connect_sink(mi.create_sink_pin(1));     // top.pi -> mi.a
  mi.create_driver_pin(2).connect_sink(top->get_output_pin("po"));  // mi.y -> top.po

  const auto bufL_h = find_hier_node(top.get(), leaf->get_gid(), bufL.get_debug_nid());

  // inp_edges resolves up two boundaries to top input "pi".
  {
    const auto ins = bufL_h.inp_edges();
    assert(ins.size() == 1);
    const auto drv = ins[0].driver;
    assert(drv.get_current_gid() == top->get_gid());
    assert(node_of(drv.get_master_node().get_debug_nid()) == node_of(top->get_input_node().get_debug_nid()));
    assert(drv.get_pin_name() == "pi");
    assert(drv.is_driver());
  }
  // out_edges resolves up two boundaries to top output "po".
  {
    const auto outs = bufL_h.out_edges();
    assert(outs.size() == 1);
    const auto snk = outs.front().sink;
    assert(snk.get_current_gid() == top->get_gid());
    assert(node_of(snk.get_master_node().get_debug_nid()) == node_of(top->get_output_node().get_debug_nid()));
    assert(snk.get_pin_name() == "po");
    assert(snk.is_sink());
  }
}

// Fan-out across a boundary + resolving DOWN from a root node: a top driver
// feeding an instance input that fans out to two leaf nodes must resolve to
// BOTH leaf sinks.
//   top: src ─▶ r.a │ leaf: a ─┬▶ b1
//                              └▶ b2
void test_hier_edges_fanout_down_from_root_EXPECTED() {
  hhds::GraphLibrary lib;

  auto leaf_io = lib.create_io("leafF");
  leaf_io->add_input("a", 1);
  auto leaf = leaf_io->create_graph();
  auto b1   = leaf->create_node();
  auto b2   = leaf->create_node();
  leaf->get_input_pin("a").connect_sink(b1.create_sink_pin());  // a -> b1
  leaf->get_input_pin("a").connect_sink(b2.create_sink_pin());  // a -> b2 (fan-out)

  auto top_io = lib.create_io("topF");
  auto top    = top_io->create_graph();
  auto src    = top->create_node();
  auto r      = top->create_node();
  r.set_subnode(leaf_io);
  src.create_driver_pin().connect_sink(r.create_sink_pin(1));  // src -> r.a

  const auto src_h = find_hier_node(top.get(), top->get_gid(), src.get_debug_nid());
  const auto outs  = src_h.out_edges();
  assert(outs.size() == 2);  // resolves down through r into both leaf sinks

  std::vector<hhds::Nid> sinks;
  for (const auto& e : outs) {
    assert(e.sink.get_current_gid() == leaf->get_gid());
    assert(e.sink.is_sink());
    sinks.push_back(node_of(e.sink.get_master_node().get_debug_nid()));
  }
  const auto has = [&](hhds::Nid n) { return std::find(sinks.begin(), sinks.end(), n) != sinks.end(); };
  assert(has(node_of(b1.get_debug_nid())));
  assert(has(node_of(b2.get_debug_nid())));
}

// REUSED CELL: the same body B is instantiated both shallow (top->bdir) and
// deep (top->mi->mid->bi). Both B instances place their inner node at the same
// per-graph tree_pos, so (gid, immediate-parent hier_pos) COLLIDES. The full
// instance chain carried on the handle must still resolve each correctly:
//   shallow bufR.inp -> srcShallow   |   deep bufR.inp -> top input "pi"
void test_hier_edges_reused_cell_distinct_paths_EXPECTED() {
  hhds::GraphLibrary lib;

  auto b_io = lib.create_io("B");
  b_io->add_input("a", 1);
  b_io->add_output("y", 2);
  auto b    = b_io->create_graph();
  auto bufR = b->create_node();
  b->get_input_pin("a").connect_sink(bufR.create_sink_pin());
  bufR.create_driver_pin().connect_sink(b->get_output_pin("y"));

  auto mid_io = lib.create_io("midR");
  mid_io->add_input("a", 1);
  mid_io->add_output("y", 2);
  auto mid = mid_io->create_graph();
  auto bi  = mid->create_node();
  bi.set_subnode(b_io);
  mid->get_input_pin("a").connect_sink(bi.create_sink_pin(1));
  bi.create_driver_pin(2).connect_sink(mid->get_output_pin("y"));

  auto top_io = lib.create_io("topR");
  top_io->add_input("pi", 1);
  top_io->add_output("po", 2);
  auto top         = top_io->create_graph();
  auto src_shallow = top->create_node();
  auto bdir        = top->create_node();
  bdir.set_subnode(b_io);  // shallow reuse of B
  auto mi = top->create_node();
  mi.set_subnode(mid_io);                                                 // deep reuse of B (via mid)
  src_shallow.create_driver_pin().connect_sink(bdir.create_sink_pin(1));  // srcShallow -> bdir.a
  top->get_input_pin("pi").connect_sink(mi.create_sink_pin(1));           // top.pi -> mi.a

  // Both B-instance copies of bufR appear in the hier walk with DISTINCT chains.
  std::vector<hhds::Node_class> matches;
  for (auto n : top->forward_hier()) {
    if (n.get_current_gid() == b->get_gid() && node_of(n.get_debug_nid()) == node_of(bufR.get_debug_nid())) {
      matches.push_back(n);
    }
  }
  assert(matches.size() == 2);

  int checked = 0;
  for (const auto& n : matches) {
    const auto path = n.get_hier_path();
    assert(path);
    const auto ins = n.inp_edges();
    assert(ins.size() == 1);
    const auto drv = ins[0].driver;
    if (path->size() == 1) {  // shallow: top -> bdir -> B
      assert(drv.get_current_gid() == top->get_gid());
      assert(node_of(drv.get_master_node().get_debug_nid()) == node_of(src_shallow.get_debug_nid()));
      ++checked;
    } else {  // deep: top -> mi -> mid -> bi -> B
      assert(path->size() == 2);
      assert(drv.get_current_gid() == top->get_gid());
      assert(node_of(drv.get_master_node().get_debug_nid()) == node_of(top->get_input_node().get_debug_nid()));
      assert(drv.get_pin_name() == "pi");
      ++checked;
    }
  }
  assert(checked == 2);
}

// get_hier_name(): Verilog-style dotted name from the instance chain + node/pin.
//   top(u_mi:M) -> M(u_li:L) -> L(u_buf)
void test_get_hier_name_EXPECTED() {
  using hhds::attrs::name;
  hhds::GraphLibrary lib;

  auto l_io = lib.create_io("L");
  l_io->add_input("a", 1);
  l_io->add_output("y", 2);
  auto l   = l_io->create_graph();
  auto buf = l->create_node();
  buf.attr(name).set("u_buf");
  l->get_input_pin("a").connect_sink(buf.create_sink_pin());
  buf.create_driver_pin().connect_sink(l->get_output_pin("y"));

  auto m_io = lib.create_io("M");
  m_io->add_input("a", 1);
  m_io->add_output("y", 2);
  auto m  = m_io->create_graph();
  auto li = m->create_node();
  li.set_subnode(l_io);
  li.attr(name).set("u_li");
  m->get_input_pin("a").connect_sink(li.create_sink_pin(1));
  li.create_driver_pin(2).connect_sink(m->get_output_pin("y"));

  auto top_io = lib.create_io("top");
  auto top    = top_io->create_graph();
  auto mi     = top->create_node();
  mi.set_subnode(m_io);
  mi.attr(name).set("u_mi");

  // Deep node: full instance chain + node name.
  const auto buf_h = find_hier_node(top.get(), l->get_gid(), buf.get_debug_nid());
  assert(buf_h.get_hier_name() == "u_mi.u_li.u_buf");

  // Instance node one level up.
  const auto li_h = find_hier_node(top.get(), m->get_gid(), li.get_debug_nid());
  assert(li_h.get_hier_name() == "u_mi.u_li");

  // Pin (node-as-pin == the node, no port suffix); carries the same chain.
  assert(buf_h.create_driver_pin().get_hier_name() == "u_mi.u_li.u_buf");

  // Fallback when no `name` attr is set: the instantiated module name, then "n<id>".
  auto top2 = lib.create_io("top2")->create_graph();
  auto inst = top2->create_node();
  inst.set_subnode(m_io);  // unnamed instance -> module name "M"
  const auto li2_h = find_hier_node(top2.get(), m->get_gid(), li.get_debug_nid());
  // top2 -> M(u_li) -> L : inst unnamed (module "M"), li named "u_li"
  assert(li2_h.get_hier_name() == "M.u_li");
}

// Resolved-leaf names must NOT leak the reserved-singleton ids: a root primary
// input is "clk" (not "n1.clk"), a constant is "const" (not "n3"). And
// get_master_node() on a resolved pin must keep the instance chain.
void test_get_hier_name_resolved_leaves_EXPECTED() {
  hhds::GraphLibrary lib;

  auto l_io = lib.create_io("Lr");
  l_io->add_input("a", 1);
  auto l   = l_io->create_graph();
  auto buf = l->create_node();
  l->get_input_pin("a").connect_sink(buf.create_sink_pin());

  auto top_io = lib.create_io("topr");
  top_io->add_input("clk", 1);
  auto top = top_io->create_graph();
  auto r1  = top->create_node();
  r1.set_subnode(l_io);
  top->get_input_pin("clk").connect_sink(r1.create_sink_pin(1));  // primary input -> r1.a
  auto r2 = top->create_node();
  r2.set_subnode(l_io);
  top->create_constant().connect_sink(r2.create_sink_pin(1));  // constant -> r2.a

  std::vector<std::string> names;
  for (auto n : top->forward_hier()) {
    if (n.get_current_gid() != l->get_gid() || node_of(n.get_debug_nid()) != node_of(buf.get_debug_nid())) {
      continue;
    }
    const auto ins = n.inp_edges();
    assert(ins.size() == 1);
    const auto drv = ins[0].driver;
    names.push_back(drv.get_hier_name());
    // get_master_node() must keep the resolved leaf's instance chain (no drop).
    assert(drv.get_master_node().get_hier_path() == drv.get_hier_path());
  }
  std::sort(names.begin(), names.end());
  assert((names == std::vector<std::string>{"clk", "const"}));  // not "n1.clk" / "n3"
}

// --- visit_io hierarchical traversal tests ----------------------------------

// (gid, nid, kind) where kind is 'I' for a boundary INPUT_NODE, 'O' for a
// boundary OUTPUT_NODE, and '.' for an ordinary body node. Reads the kind via
// the public Node_class::is_input_node()/is_output_node() predicates so these
// tests also cover the helpers.
template <typename Range>
std::vector<std::tuple<hhds::Gid, hhds::Nid, char>> collect_gid_nid_kind(Range&& range) {
  std::vector<std::tuple<hhds::Gid, hhds::Nid, char>> order;
  for (auto node : range) {
    const char kind = node.is_input_node() ? 'I' : (node.is_output_node() ? 'O' : '.');
    order.emplace_back(node.get_current_gid(), node.get_debug_nid(), kind);
  }
  return order;
}

using IoStep = std::tuple<hhds::Gid, hhds::Nid, char>;

// Shared fixture: top { pi, po, inst=leaf }, leaf { a, z, leaf_n }.
struct IoFixture {
  hhds::GraphLibrary           lib;
  std::shared_ptr<hhds::Graph> leaf;
  std::shared_ptr<hhds::Graph> top;
  hhds::Node_class             inst;
  hhds::Node_class             leaf_n;

  IoFixture() {
    auto leaf_io = lib.create_io("leaf");
    leaf_io->add_input("a", 1);
    leaf_io->add_output("z", 1);
    leaf   = leaf_io->create_graph();
    leaf_n = leaf->create_node();
    // Wire a -> leaf_n -> z so the boundary IO pins carry edges (get_driver_pins
    // / get_sink_pins only surface connected ports). Adds no user node, so the
    // traversal order below is unchanged.
    leaf->get_input_pin("a").connect_sink(leaf_n.create_sink_pin(1));
    leaf_n.create_driver_pin(1).connect_sink(leaf->get_output_pin("z"));

    auto top_io = lib.create_io("top");
    top_io->add_input("pi", 1);
    top_io->add_output("po", 1);
    top  = top_io->create_graph();
    inst = top->create_node();
    inst.set_subnode(leaf_io);
  }
};

void test_fast_hier_visit_io_storage_order() {
  IoFixture                 f;
  const std::vector<IoStep> expected{
      { f.top->get_gid(),  hhds::Graph::INPUT_NODE, 'I'},
      { f.top->get_gid(),   f.inst.get_debug_nid(), '.'},
      {f.leaf->get_gid(),  hhds::Graph::INPUT_NODE, 'I'},
      {f.leaf->get_gid(), f.leaf_n.get_debug_nid(), '.'},
      {f.leaf->get_gid(), hhds::Graph::OUTPUT_NODE, 'O'},
      { f.top->get_gid(), hhds::Graph::OUTPUT_NODE, 'O'},
  };
  assert(collect_gid_nid_kind(f.top->fast_hier(/*visit_io=*/true)) == expected);
}

// ── fast_hier opacity ───────────────────────────────────────────────────────
// An opaque subnode is yielded as a LEAF Sub: the instance node itself is still
// emitted, but its body is NOT descended into. fast_hier MUST agree with
// forward_hier and the cross-boundary edge resolver here — if it descended into a
// sub the resolver black-boxes, a caller that blackboxes an instance (pass/lec
// --collapse) would still cut the state inside it while modelling its boundary as
// free => false PROVEN.

void test_fast_hier_opaque_explicit_not_descended() {
  IoFixture                               f;
  ankerl::unordered_dense::set<hhds::Gid> opaque{f.inst.get_subnode_gid()};

  // Opaque: the Sub instance is still yielded, its body is not.
  const std::vector<IoStep> expected{
      {f.top->get_gid(), f.inst.get_debug_nid(), '.'},
  };
  assert(collect_gid_nid_kind(f.top->fast_hier(/*visit_io=*/false, &opaque)) == expected);

  // Control: with no opacity the SAME walk descends into the leaf body.
  const std::vector<IoStep> expected_open{
      { f.top->get_gid(),   f.inst.get_debug_nid(), '.'},
      {f.leaf->get_gid(), f.leaf_n.get_debug_nid(), '.'},
  };
  assert(collect_gid_nid_kind(f.top->fast_hier()) == expected_open);
}

void test_fast_hier_honors_ambient_opaque_scope() {
  IoFixture                               f;
  ankerl::unordered_dense::set<hhds::Gid> opaque{f.inst.get_subnode_gid()};
  const std::vector<IoStep>               expected{
      {f.top->get_gid(), f.inst.get_debug_nid(), '.'},
  };
  {
    hhds::Hier_opaque_scope sc(&opaque);  // ambient, no explicit argument
    assert(collect_gid_nid_kind(f.top->fast_hier()) == expected);
  }
  // Scope popped => descends again (the RAII really restores).
  assert(collect_gid_nid_kind(f.top->fast_hier()).size() == 2);
}

void test_fast_hier_opaque_matches_forward_hier_node_set() {
  // The contract fast_hier documents: ordering is the ONLY difference from
  // forward_hier — the node SET is identical, opacity included.
  IoFixture                                     f;
  const ankerl::unordered_dense::set<hhds::Gid> opaque{f.inst.get_subnode_gid()};
  const ankerl::unordered_dense::set<hhds::Gid>* cases[] = {nullptr, &opaque};
  for (const auto* opq : cases) {
    auto fast = collect_gid_nid_kind(f.top->fast_hier(false, opq));
    auto fwd  = collect_gid_nid_kind(f.top->forward_hier(true, false, opq));
    std::sort(fast.begin(), fast.end());
    std::sort(fwd.begin(), fwd.end());
    assert(fast == fwd);
  }
}

void test_fast_hier_opaque_emits_no_boundary_io() {
  // Documented: an opaque sub contributes no boundary IO under visit_io, because
  // the body is never entered. Only the ROOT bracket remains.
  IoFixture                               f;
  ankerl::unordered_dense::set<hhds::Gid> opaque{f.inst.get_subnode_gid()};
  const std::vector<IoStep>               expected{
      {f.top->get_gid(),  hhds::Graph::INPUT_NODE, 'I'},
      {f.top->get_gid(),   f.inst.get_debug_nid(), '.'},
      {f.top->get_gid(), hhds::Graph::OUTPUT_NODE, 'O'},
  };
  assert(collect_gid_nid_kind(f.top->fast_hier(/*visit_io=*/true, &opaque)) == expected);
}

void test_fast_hier_opaque_explicit_unions_with_ambient() {
  // forward_hier's rule, mirrored: explicit OR ambient (never an override).
  IoFixture                               f;
  ankerl::unordered_dense::set<hhds::Gid> ambient{f.inst.get_subnode_gid()};
  ankerl::unordered_dense::set<hhds::Gid> unrelated;  // explicit set that matches nothing
  const std::vector<IoStep>               expected{
      {f.top->get_gid(), f.inst.get_debug_nid(), '.'},
  };
  hhds::Hier_opaque_scope sc(&ambient);
  // An explicit set that does NOT list the sub must not re-enable descent.
  assert(collect_gid_nid_kind(f.top->fast_hier(false, &unrelated)) == expected);
}

void test_hier_visit_io_flat_top_only() {
  // A flat top (no subnodes) still emits its own root IO bracket around its body.
  hhds::GraphLibrary lib;
  auto               top_io = lib.create_io("top");
  auto               top    = top_io->create_graph();
  auto               n1     = top->create_node();

  const std::vector<IoStep> expected{
      {top->get_gid(),  hhds::Graph::INPUT_NODE, 'I'},
      {top->get_gid(),       n1.get_debug_nid(), '.'},
      {top->get_gid(), hhds::Graph::OUTPUT_NODE, 'O'},
  };
  assert(collect_gid_nid_kind(top->fast_hier(true)) == expected);
}

}  // namespace

int main() {
  test_declaration_api();
  test_subnode_accessors_round_trip_with_set_subnode();
  test_wrapper_pin_connect_api();
  test_same_index_pin_to_node_port0_edge_survives();
  test_node_port0_self_loop_edge_survives();
  test_pin_get_driver_pins();
  test_out_edges_lazy_range();
  test_forward_class_returns_wrappers();
  test_backward_class_returns_wrappers();
  test_traversal_contexts_use_one_node_type();
  test_forward_loop_break_is_source();
  test_backward_loop_break_is_sink();
  test_forward_loop_break_visit_flags();
  test_backward_loop_break_visit_flags();
  test_forward_hier_loop_break_both_descends_once();
  test_forward_hier_globally_topological_across_stateful_sub();
  test_backward_hier_globally_topological_across_stateful_sub();
  test_forward_out_of_order_uses_pending_list();
  test_backward_out_of_order_uses_pending_list();
  test_backward_cache_invalidates_after_set_type();
  test_backward_cache_invalidates_after_edge_mutation();
  test_backward_diamond_fan_in();
  test_backward_named_pin_and_declared_io_edges();
  test_backward_skips_tombstones_after_delete();
  test_backward_cycle_tail_without_loop_break();
  test_traversal_caches_after_edge_delete();
  test_traversal_caches_after_pin_delete();
  test_traversal_caches_after_back_edge_add();
  test_backward_flat_exact_order_and_shared_body_dedup();
  test_backward_hier_exact_order_and_shared_body_per_instance();
  test_backward_hier_descends_into_nested_subnodes();
  test_subnode_with_loop_break_pin_marks_node();
  test_hier_range_flat_graph_is_empty();
  test_hier_range_yields_one_per_subnode();
  test_hier_range_descends_into_nested_subnodes();
#ifdef NDEBUG
  // In debug builds, set_subnode asserts on the cycle. The runtime
  // iterator guard is only the active line of defense in release.
  test_hier_range_cycle_guard();
#endif
  test_hier_range_target_graph_and_parent_node();
  test_set_subnode_linear_deep_chain_ok();
  test_set_subnode_diamond_ok();
  test_set_subnode_retarget_ok();
#ifndef NDEBUG
  // Death tests for the cycle assertion. Each forks a child that should
  // crash inside set_subnode; parent verifies the child died via SIGABRT.
  test_set_subnode_self_cycle_aborts();
  test_set_subnode_indirect_cycle_aborts();
#endif
  test_load_merge();
  test_shared_source_map();
  // Cross-boundary hier edge resolution: drivers/sinks hop module boundaries
  // until a real leaf (or the root's own IO) is reached.
  // fast_hier opacity: opaque subs are leaves, matching forward_hier + the resolver.
  test_fast_hier_opaque_explicit_not_descended();
  test_fast_hier_honors_ambient_opaque_scope();
  test_fast_hier_opaque_matches_forward_hier_node_set();
  test_fast_hier_opaque_emits_no_boundary_io();
  test_fast_hier_opaque_explicit_unions_with_ambient();
  test_hier_edges_cross_one_boundary_EXPECTED();
  test_hier_edges_cross_up_then_down_EXPECTED();
  test_hier_edges_three_levels_EXPECTED();
  test_hier_edges_fanout_down_from_root_EXPECTED();
  test_hier_edges_reused_cell_distinct_paths_EXPECTED();
  test_get_hier_name_EXPECTED();
  test_get_hier_name_resolved_leaves_EXPECTED();
  test_fast_hier_visit_io_storage_order();
  test_hier_visit_io_flat_top_only();
  std::cout << "graph_test passed\n";
  return 0;
}
