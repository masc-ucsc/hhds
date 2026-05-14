#include "hhds/graph.hpp"

#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cassert>
#include <csignal>
#include <iostream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

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

void test_forward_loop_last_is_source() {
  // A user node marked loop_last (odd type) acts as a forward source:
  // emitted unconditionally and does not propagate to sinks. The feedback
  // edge through the flop therefore does not block combinational users.
  hhds::GraphLibrary lib;
  auto               gio   = lib.create_io("top");
  auto               graph = gio->create_graph();

  auto n1 = graph->create_node();
  auto n2 = graph->create_node();
  auto n3 = graph->create_node();
  n3.set_type(3);  // bit 0 set -> is_loop_last

  n1.create_driver_pin().connect_sink(n2.create_sink_pin());
  n2.create_driver_pin().connect_sink(n3.create_sink_pin());
  // Feedback: flop drives n2, closing the loop. Without loop_last, this
  // would either cycle or force n3 before n2. With loop_last, n3 is a
  // source and does not contribute to n2's pending count.
  n3.create_driver_pin().connect_sink(n2.create_sink_pin());

  assert(n3.is_loop_last());
  assert(!n1.is_loop_last());
  assert(!n2.is_loop_last());

  std::vector<hhds::Nid> order;
  for (auto node : graph->forward_class()) {
    order.push_back(node.get_debug_nid());
  }
  assert(order.size() == 3);
  assert(order[0] == n1.get_debug_nid());
  assert(order[1] == n2.get_debug_nid());
  assert(order[2] == n3.get_debug_nid());
}

void test_backward_loop_last_is_sink() {
  hhds::GraphLibrary lib;
  auto               gio   = lib.create_io("top");
  auto               graph = gio->create_graph();

  auto n1 = graph->create_node();
  auto n2 = graph->create_node();
  auto n3 = graph->create_node();
  n3.set_type(3);  // bit 0 set -> is_loop_last

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
  // Cycle n1<->n2 with no loop_last: both nodes fall through to the Tail
  // phase (Pass 1 and Pass 2 can't break the cycle), so order is [n2, n1].
  // After set_type marks n2 as loop_last, n2 becomes a sink: Pass 1 emits it
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
  assert(n2.is_loop_last());

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

void test_backward_cycle_tail_without_loop_last() {
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
      {top->get_gid(), inst2.get_debug_nid()},
      {leaf->get_gid(), leaf_n2.get_debug_nid()},
      {leaf->get_gid(), leaf_n1.get_debug_nid()},
      {top->get_gid(), inst1.get_debug_nid()},
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
      {top->get_gid(), inst2.get_debug_nid()},
      {leaf->get_gid(), leaf_n2.get_debug_nid()},
      {leaf->get_gid(), leaf_n1.get_debug_nid()},
      {top->get_gid(), inst1.get_debug_nid()},
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
      {top->get_gid(), top_inst_of_mid.get_debug_nid()},
      {mid->get_gid(), mid_inst_of_leaf.get_debug_nid()},
  };
  assert(collect_gid_nids(top->backward_hier()) == expected);
}

void test_subnode_with_loop_last_pin_marks_node() {
  // set_subnode must stamp the node type with bit 0 set iff the subnode's
  // declared IO contains any loop_last pin (flop/register instance).
  hhds::GraphLibrary lib;

  auto flop_gio = lib.create_io("flop");
  flop_gio->add_input("D", 0, /*loop_last=*/false);
  flop_gio->add_output("Q", 0, /*loop_last=*/true);
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

  assert(flop_inst.is_loop_last());
  assert((flop_inst.get_type() & 1) == 1);
  assert(!buf_inst.is_loop_last());
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

}  // namespace

int main() {
  test_declaration_api();
  test_wrapper_pin_connect_api();
  test_forward_class_returns_wrappers();
  test_backward_class_returns_wrappers();
  test_traversal_contexts_use_one_node_type();
  test_forward_loop_last_is_source();
  test_backward_loop_last_is_sink();
  test_forward_out_of_order_uses_pending_list();
  test_backward_out_of_order_uses_pending_list();
  test_backward_cache_invalidates_after_set_type();
  test_backward_cache_invalidates_after_edge_mutation();
  test_backward_diamond_fan_in();
  test_backward_named_pin_and_declared_io_edges();
  test_backward_skips_tombstones_after_delete();
  test_backward_cycle_tail_without_loop_last();
  test_traversal_caches_after_edge_delete();
  test_traversal_caches_after_pin_delete();
  test_traversal_caches_after_back_edge_add();
  test_backward_flat_exact_order_and_shared_body_dedup();
  test_backward_hier_exact_order_and_shared_body_per_instance();
  test_backward_hier_descends_into_nested_subnodes();
  test_subnode_with_loop_last_pin_marks_node();
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
  std::cout << "graph_test passed\n";
  return 0;
}
