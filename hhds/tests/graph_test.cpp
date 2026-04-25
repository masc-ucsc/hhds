#include "hhds/graph.hpp"

#include <cassert>
#include <iostream>
#include <string>
#include <vector>

namespace {

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

  assert(order.size() == 3);
  assert(order[0] == hhds::Graph::CONST_NODE);
  assert(order[1] == n1.get_debug_nid());
  assert(order[2] == n2.get_debug_nid());
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
  assert(order.size() == 4);
  assert(order[0] == hhds::Graph::CONST_NODE);
  assert(order[1] == n1.get_debug_nid());
  assert(order[2] == n2.get_debug_nid());
  assert(order[3] == n3.get_debug_nid());
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
  assert(order.size() == 4);
  assert(order[0] == hhds::Graph::CONST_NODE);
  assert(order[1] == n3.get_debug_nid());
  assert(order[2] == n1.get_debug_nid());
  assert(order[3] == n2.get_debug_nid());
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
  auto               gio   = lib.create_io("flat");
  gio->add_input("in", 0);
  gio->add_output("out", 0);
  auto               graph = gio->create_graph();
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

  auto mid_gio = lib.create_io("mid");
  auto mid     = mid_gio->create_graph();
  auto mid_inst_of_leaf = mid->create_node();
  mid_inst_of_leaf.set_subnode(leaf_gio);

  auto top_gio = lib.create_io("top");
  auto top     = top_gio->create_graph();
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

void test_hier_range_cycle_guard() {
  // Self-referencing submodule must not cause infinite recursion —
  // active_graphs_ deduplicates on descent. Cycles are disallowed by design
  // (see todo.md Task 2), but the iterator must be robust in release
  // builds regardless.
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

}  // namespace

int main() {
  test_declaration_api();
  test_wrapper_pin_connect_api();
  test_forward_class_returns_wrappers();
  test_traversal_contexts_use_one_node_type();
  test_forward_loop_last_is_source();
  test_forward_out_of_order_uses_pending_list();
  test_subnode_with_loop_last_pin_marks_node();
  test_hier_range_flat_graph_is_empty();
  test_hier_range_yields_one_per_subnode();
  test_hier_range_descends_into_nested_subnodes();
  test_hier_range_cycle_guard();
  test_hier_range_target_graph_and_parent_node();
  std::cout << "graph_test passed\n";
  return 0;
}
