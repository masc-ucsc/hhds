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

  assert(order.size() == 2);
  assert(order[0] == n1.get_debug_nid());
  assert(order[1] == n2.get_debug_nid());
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
  assert(order.size() == 3);
  assert(order[0] == n1.get_debug_nid());
  assert(order[1] == n2.get_debug_nid());
  assert(order[2] == n3.get_debug_nid());
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

}  // namespace

int main() {
  test_declaration_api();
  test_wrapper_pin_connect_api();
  test_forward_class_returns_wrappers();
  test_traversal_contexts_use_one_node_type();
  test_forward_loop_last_is_source();
  test_forward_out_of_order_uses_pending_list();
  test_subnode_with_loop_last_pin_marks_node();
  std::cout << "graph_test passed\n";
  return 0;
}
