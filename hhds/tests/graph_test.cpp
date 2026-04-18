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
    assert(edge.sink_pin() == and1_in);
  }

  auto output_edges = and1_out.out_edges();
  assert(output_edges.size() == 1);
  assert(output_edges.front().driver_pin() == and1_out);
  assert(output_edges.front().sink_pin() == z);
}

void test_forward_class_returns_wrappers() {
  hhds::GraphLibrary lib;
  auto               gio   = lib.create_io("top");
  auto               graph = gio->create_graph();

  auto n1 = graph->create_node();
  auto n2 = graph->create_node();
  n1.connect_sink(n2);

  std::vector<hhds::Nid> order;
  for (auto node : graph->forward_class()) {
    assert(node.get_graph() == graph.get());
    assert(node.is_class());
    order.push_back(node.get_raw_nid());
  }

  assert(order.size() == 2);
  assert(order[0] == n1.get_raw_nid());
  assert(order[1] == n2.get_raw_nid());
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
    if (node.get_raw_nid() == leaf_n.get_raw_nid()) {
      auto pin = node.create_sink_pin();
      assert(pin.is_flat());
      saw_flat_leaf = true;
    }
  }
  assert(saw_flat_leaf);

  bool saw_hier_leaf = false;
  for (auto node : top->forward_hier()) {
    assert(node.is_hier());
    if (node.get_raw_nid() == leaf_n.get_raw_nid()) {
      auto pin = node.create_driver_pin();
      assert(pin.is_hier());
      saw_hier_leaf = true;
    }
  }
  assert(saw_hier_leaf);
}

}  // namespace

int main() {
  test_declaration_api();
  test_wrapper_pin_connect_api();
  test_forward_class_returns_wrappers();
  test_traversal_contexts_use_one_node_type();
  std::cout << "graph_test passed\n";
  return 0;
}
