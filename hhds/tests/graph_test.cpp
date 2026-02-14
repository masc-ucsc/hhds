#include "graph.hpp"

#include <cassert>
#include <iostream>
#include <random>
#include <vector>

using namespace hhds;

void test_add_input_output_create_pins_on_builtin_nodes() {
  Graph g;

  Pid in0  = g.add_input(10);
  Pid in1  = g.add_input(11);
  Pid out0 = g.add_output(20);

  auto* in0_pin  = g.ref_pin(in0);  // return: std:unique_ptr<Pin>&
  auto* in1_pin  = g.ref_pin(in1);
  auto* out0_pin = g.ref_pin(out0);

  assert(in0_pin->get_master_nid() == Graph::INPUT_NODE && "add_input must attach to input node");
  assert(in1_pin->get_master_nid() == Graph::INPUT_NODE && "add_input must attach to input node");
  assert(out0_pin->get_master_nid() == Graph::OUTPUT_NODE && "add_output must attach to output node");

  assert(in0_pin->get_port_id() == 10 && "add_input must preserve port id");
  assert(in1_pin->get_port_id() == 11 && "add_input must preserve port id");
  assert(out0_pin->get_port_id() == 20 && "add_output must preserve port id");

  // Input node should chain both created input pins in creation order.
  auto* input_node = g.ref_node(Graph::INPUT_NODE);
  assert(input_node->get_next_pin_id() == in0 && "input node first pin mismatch");
  assert(g.ref_pin(in0)->get_next_pin_id() == in1 && "input pin chain mismatch");
  assert(g.ref_pin(in1)->get_next_pin_id() == 0 && "input pin chain must terminate");

  // Output node should point to its first output pin.
  auto* output_node = g.ref_node(Graph::OUTPUT_NODE);
  assert(output_node->get_next_pin_id() == out0 && "output node first pin mismatch");
  assert(g.ref_pin(out0)->get_next_pin_id() == 0 && "single output pin chain must terminate");

  std::cout << "test_add_input_output_create_pins_on_builtin_nodes passed\n";
}

void test_create_pin_chains_pins_on_node() {
  Graph g;

  Nid n  = g.create_node();
  Pid p0 = g.create_pin(n, 0);
  Pid p1 = g.create_pin(n, 1);
  Pid p2 = g.create_pin(n, 2);

  auto* node = g.ref_node(n);
  assert(node->get_next_pin_id() == p0 && "node first pin should be first created pin");
  assert(g.ref_pin(p0)->get_next_pin_id() == p1 && "first pin should link to second pin");
  assert(g.ref_pin(p1)->get_next_pin_id() == p2 && "second pin should link to third pin");
  assert(g.ref_pin(p2)->get_next_pin_id() == 0 && "last pin should terminate chain");

  std::cout << "test_create_pin_chains_pins_on_node passed\n";
}

void test_node_to_node() {
  Graph g;
  Nid   n1 = g.create_node();
  Nid   n2 = g.create_node();
  g.add_edge(n1, n2);

  {
    auto range = g.ref_node(n1)->get_edges(n1);
    auto it    = range.begin();
    assert(it != range.end() && "no edge out of n1");
    assert(*it == n2 && "edge out of n1 should be n2");
    ++it;
    assert(it == range.end() && "only one edge should exist");
  }

  {
    auto range = g.ref_node(n2)->get_edges(n2);
    auto it    = range.begin();
    assert(it != range.end() && "no edge out of n2");
    assert(*it == n1 | 2 && "edge out of n2 should be n1");
    ++it;
    assert(it == range.end() && "only one edge should exist");
  }
  std::cout << "test_node_to_node passed\n";
}

void test_pin_to_pin() {
  Graph g;
  Nid   n  = g.create_node();
  Pid   p1 = g.create_pin(n, 0);
  Pid   p2 = g.create_pin(n, 1);
  g.add_edge(p1, p2);
  {
    auto range = g.ref_pin(p1)->get_edges(p1);
    auto it    = range.begin();
    assert(it != range.end() && *it == p2 && "test_pin_to_pin failed: missing p2");
    ++it;
    assert(it == range.end() && "test_pin_to_pin failed: extra edges found");
  }

  {
    auto range = g.ref_pin(p2)->get_edges(p2);
    auto it    = range.begin();
    assert(it != range.end() && *it == p1 | 2 && "test_pin_to_pin failed: missing p1");
    ++it;
    assert(it == range.end() && "test_pin_to_pin failed: extra edges found");
  }

  std::cout << "test_pin_to_pin passed\n";
}

void test_node_to_pin() {
  Graph g;
  Nid   n = g.create_node();
  Pid   p = g.create_pin(n, 0);
  g.add_edge(n, p);
  {
    auto range = g.ref_node(n)->get_edges(n);
    auto it    = range.begin();
    assert(it != range.end() && *it == p && "test_node_to_pin failed: missing p");
    ++it;
    assert(it == range.end() && "test_node_to_pin failed: extra edges found");
  }
  {
    auto range = g.ref_pin(p)->get_edges(p);
    auto it    = range.begin();
    assert(it != range.end() && *it == n | 2 && "test_node_to_pin failed: missing n");
    ++it;
    assert(it == range.end() && "test_node_to_pin failed: extra edges found");
  }
  std::cout << "test_node_to_pin passed\n";
}

void test_pin_to_node() {
  Graph g;
  Nid   n = g.create_node();
  Pid   p = g.create_pin(n, 0);
  g.add_edge(p, n);
  {
    auto range = g.ref_pin(p)->get_edges(p);
    auto it    = range.begin();
    assert(it != range.end() && *it == n && "test_pin_to_node failed: missing n");
    ++it;
    assert(it == range.end() && "test_pin_to_node failed: extra edges found");
  }
  {
    auto range = g.ref_node(n)->get_edges(n);
    auto it    = range.begin();
    assert(it != range.end() && *it == p | 2 && "test_pin_to_node failed: missing p");
    ++it;
    assert(it == range.end() && "test_pin_to_node failed: extra edges found");
  }

  std::cout << "test_pin_to_node passed\n";
}

void test_sedges_ledges() {
  Graph g;
  Nid   n1 = g.create_node();
  Nid   n2 = g.create_node();
  Nid   n3 = g.create_node();
  Nid   n4 = g.create_node();

  Pid p1 = g.create_pin(n1, 0);
  Pid p2 = g.create_pin(n2, 0);
  Pid p3 = g.create_pin(n3, 0);
  Pid p4 = g.create_pin(n4, 0);

  g.add_edge(p1, p2);
  g.add_edge(p1, p3);
  g.add_edge(p1, p4);
  g.add_edge(p1, n2);
  g.add_edge(p1, n3);
  g.add_edge(p1, n4);

  auto sed = g.ref_pin(p1)->get_edges(p1);
  assert(sed.begin() != sed.end() && "test_sedges_ledges failed: no edges found");
  ankerl::unordered_dense::set<Vid> expected_set = {p2, p3, p4, n2, n3, n4};
  ankerl::unordered_dense::set<Vid> actual_set;
  for (auto it = sed.begin(); it != sed.end(); ++it) {
    actual_set.insert(*it);
  }
  assert(actual_set.size() == expected_set.size() && "test_sedges_ledges failed: size mismatch");
  for (const auto& expected : expected_set) {
    assert(actual_set.count(expected) && "test_sedges_ledges failed: missing expected edge");
  }

  auto sed2 = g.ref_pin(p2)->get_edges(p2);
  assert(sed2.begin() != sed2.end() && "test_sedges_ledges failed: no edges found for p2");
  auto it2 = sed2.begin();
  assert(*it2 == p1 | 2 && "test_sedges_ledges failed: sedges[0] != src for p2");
  ++it2;
  assert(it2 == sed2.end() && "test_sedges_ledges failed: extra edges found for p2");

  auto sed3 = g.ref_pin(p3)->get_edges(p3);
  assert(sed3.begin() != sed3.end() && "test_sedges_ledges failed: no edges found for p3");
  auto it3 = sed3.begin();
  assert(*it3 == p1 | 2 && "test_sedges_ledges failed: sedges[0] != src for p3");
  ++it3;
  assert(it3 == sed3.end() && "test_sedges_ledges failed: extra edges found for p3");

  auto sed4 = g.ref_pin(p4)->get_edges(p4);
  assert(sed4.begin() != sed4.end() && "test_sedges_ledges failed: no edges found for p4");
  auto it4 = sed4.begin();
  assert(*it4 == p1 | 2 && "test_sedges_ledges failed: sedges[0] != src for p4");
  ++it4;
  assert(it4 == sed4.end() && "test_sedges_ledges failed: extra edges found for p4");

  auto sed5 = g.ref_node(n2)->get_edges(n2);
  assert(sed5.begin() != sed5.end() && "test_sedges_ledges failed: no edges found for n2");
  auto it5 = sed5.begin();
  assert(*it5 == p1 | 2 && "test_sedges_ledges failed: sedges[0] != src for n2");
  ++it5;
  assert(it5 == sed5.end() && "test_sedges_ledges failed: extra edges found for n2");

  auto sed6 = g.ref_node(n3)->get_edges(n3);
  assert(sed6.begin() != sed6.end() && "test_sedges_ledges failed: no edges found for n3");
  auto it6 = sed6.begin();
  assert(*it6 == p1 | 2 && "test_sedges_ledges failed: sedges[0] != src for n3");
  ++it6;
  assert(it6 == sed6.end() && "test_sedges_ledges failed: extra edges found for n3");

  auto sed7 = g.ref_node(n4)->get_edges(n4);
  assert(sed7.begin() != sed7.end() && "test_sedges_ledges failed: no edges found for n4");
  auto it7 = sed7.begin();
  assert(*it7 == p1 | 2 && "test_sedges_ledges failed: sedges[0] != src for n4");
  ++it7;
  assert(it7 == sed7.end() && "test_sedges_ledges failed: extra edges found for n4");
  std::cout << "test_sedges_ledges passed\n";
}

void test_overflow_handling() {
  Graph g;
  Nid   n1 = g.create_node();
  Nid   n2 = g.create_node();
  Nid   n3 = g.create_node();
  Nid   n4 = g.create_node();
  Nid   n5 = g.create_node();

  Pid p1 = g.create_pin(n1, 0);
  Pid p2 = g.create_pin(n2, 0);
  Pid p3 = g.create_pin(n3, 0);
  Pid p4 = g.create_pin(n4, 0);

  g.add_edge(p1, p2);
  g.add_edge(p1, p3);
  g.add_edge(p1, p4);
  g.add_edge(p1, n3);
  g.add_edge(p1, n4);
  g.add_edge(p1, n2);
  g.add_edge(p1, n5);  // This will trigger overflow handling

  auto sed = g.ref_pin(p1)->get_edges(p1);
  assert(sed.begin() != sed.end() && "test_overflow_handling failed: no edges found");
  ankerl::unordered_dense::set<Vid> expected_set = {p2, p3, p4, n3, n4, n2, n5};

  ankerl::unordered_dense::set<Vid> actual_set;
  for (auto it = sed.begin(); it != sed.end(); ++it) {
    actual_set.insert(*it);
  }
  assert(actual_set.size() == expected_set.size() && "test_overflow_handling failed: size mismatch");
  for (const auto& expected : expected_set) {
    assert(actual_set.count(expected) && "test_overflow_handling failed: missing expected edge");
  }

  // TODO: Testing other way around
  auto sed2 = g.ref_pin(p2)->get_edges(p2);
  auto it2  = sed2.begin();
  assert(it2 != sed2.end() && "test_overflow_handling failed: no edges found for p2");
  assert(*it2 == p1 | 2 && "test_overflow_handling failed: sedges[0] != src");

  auto sed3 = g.ref_pin(p3)->get_edges(p3);
  auto it3  = sed3.begin();
  assert(it3 != sed3.end() && "test_overflow_handling failed: no edges found for p3");
  assert(*it3 == p1 | 2 && "test_overflow_handling failed: sedges[0] != src");

  auto sed4 = g.ref_pin(p4)->get_edges(p4);
  auto it4  = sed4.begin();
  assert(it4 != sed4.end() && "test_overflow_handling failed: no edges found for p4");
  assert(*it4 == p1 | 2 && "test_overflow_handling failed: sedges[0] != src");

  auto sed5 = g.ref_node(n3)->get_edges(n3);
  auto it5  = sed5.begin();
  assert(it5 != sed5.end() && "test_overflow_handling failed: no edges found for n3");
  assert(*it5 == p1 | 2 && "test_overflow_handling failed: sedges[0] != src");

  auto sed6 = g.ref_node(n4)->get_edges(n4);
  auto it6  = sed6.begin();
  assert(it6 != sed6.end() && "test_overflow_handling failed: no edges found for n4");
  assert(*it6 == p1 | 2 && "test_overflow_handling failed: sedges[0] != src");

  auto sed7 = g.ref_node(n2)->get_edges(n2);
  auto it7  = sed7.begin();
  assert(it7 != sed7.end() && "test_overflow_handling failed: no edges found for n2");
  assert(*it7 == p1 | 2 && "test_overflow_handling failed: sedges[0] != src");

  auto sed8 = g.ref_node(n5)->get_edges(n5);
  auto it8  = sed8.begin();
  assert(it8 != sed8.end() && "test_overflow_handling failed: no edges found for n5");
  assert(*it8 == p1 | 2 && "test_overflow_handling failed: sedges[0] != src");

  // check if the overflow handling is done correctly
  auto p1_ptr = g.ref_pin(p1);
  assert(p1_ptr->check_overflow() == true && "test_overflow_handling failed: use_overflow != true");

  std::cout << "test_overflow_handling passed\n";
}

void test_large_fanin_deletion() {
  Graph g;

  // Create graph with 2 inputs and 1 output
  Pid input1 = g.add_input(1);
  Pid input2 = g.add_input(2);
  Pid output = g.add_output(1);

  assert(input1 == 1 << 2 | 1 && "test_large_fanin_deletion: input1 should be pin 1 on input node");
  assert(input2 == 2 << 2 | 3 && "test_large_fanin_deletion: input2 should be pin 3 on input node");
  assert(output == 3 << 2 | 1 && "test_large_fanin_deletion: output should be pin 1 on output node");
  std::cout << "test_large_fanin_deletion: created input and output pins\n";

  // Create 1000 intermediate nodes
  std::vector<Nid> intermediate_nodes;
  // node : 3 to 1002
  // total nodes: 1002 (including input and output and 0)
  for (int i = 0; i < 1000; ++i) {
    intermediate_nodes.push_back(g.create_node());
  }

  // Create the central node with 3 input pins
  Nid central_node = g.create_node();                // node: 1003
  Pid central_pin0 = g.create_pin(central_node, 0);  // pin: 4 attached to node 1003
  Pid central_pin1 = g.create_pin(central_node, 1);  // pin: 5 attached to node 1003
  Pid central_pin2 = g.create_pin(central_node, 2);  // pin: 6 attached to node 1003

  // Random number generator for distributing connections
  std::random_device              rd;
  std::mt19937                    gen(rd());
  std::uniform_int_distribution<> pin_dist(0, 2);

  // Connect all 1000 nodes to input1 and to one of the central node's pins
  std::vector<Pid>                       central_pins = {central_pin0, central_pin1, central_pin2};
  ankerl::unordered_dense::map<Pid, int> pin_connection_counts;
  pin_connection_counts[central_pin0] = 0;
  pin_connection_counts[central_pin1] = 0;
  pin_connection_counts[central_pin2] = 0;

  assert(central_pin0 == 4 << 2 | 1 && "test_large_fanin_deletion: central_pin0 should be pin 4 on central node");
  assert(central_pin1 == 5 << 2 | 1 && "test_large_fanin_deletion: central_pin1 should be pin 5 on central node");
  assert(central_pin2 == 6 << 2 | 1 && "test_large_fanin_deletion: central_pin2 should be pin 6 on central node");
  std::cout << "test_large_fanin_deletion: created central node and pins\n";

  // assert next pin of each node and pin is correct
  assert(g.ref_node(central_node)->get_next_pin_id() == central_pin0
         && "test_large_fanin_deletion: central node next pin should be central_pin0");
  assert(g.ref_pin(central_pin0)->get_next_pin_id() == central_pin1
         && "test_large_fanin_deletion: central_pin0 next pin should be central_pin1");
  assert(g.ref_pin(central_pin1)->get_next_pin_id() == central_pin2
         && "test_large_fanin_deletion: central_pin1 next pin should be central_pin2");
  assert(g.ref_pin(central_pin2)->get_next_pin_id() == 0 && "test_large_fanin_deletion: central_pin2 should terminate pin chain");

  // assert master node of each pin is correct
  assert(g.ref_pin(central_pin0)->get_master_nid() == central_node
         && "test_large_fanin_deletion: central_pin0 master nid should be central_node");
  assert(g.ref_pin(central_pin1)->get_master_nid() == central_node
         && "test_large_fanin_deletion: central_pin1 master nid should be central_node");
  assert(g.ref_pin(central_pin2)->get_master_nid() == central_node
         && "test_large_fanin_deletion: central_pin2 master nid should be central_node");

  std::cout << "test_large_fanin_deletion: verified central node and pin setup\n";

  for (int i = 0; i < 1000; ++i) {
    // Connect intermediate node to input1
    g.add_edge(input1, intermediate_nodes[i]);

    // Randomly select which central pin to connect to
    int pin_idx = pin_dist(gen);
    g.add_edge(intermediate_nodes[i], central_pins[pin_idx]);
    pin_connection_counts[central_pins[pin_idx]]++;
  }

  // Connect central node to output
  g.add_edge(central_node, output);

  std::cout << "test_large_fanin_deletion: created 1000 nodes + 1 central node\n";
  std::cout << "  connections to central_pin0: " << pin_connection_counts[central_pin0] << "\n";
  std::cout << "  connections to central_pin1: " << pin_connection_counts[central_pin1] << "\n";
  std::cout << "  connections to central_pin2: " << pin_connection_counts[central_pin2] << "\n";

  // Verify edges exist before deletion
  // Check that central node has edge to output
  {
    auto edges        = g.ref_node(central_node)->get_edges(central_node);
    bool found_output = false;
    for (auto e : edges) {
      if ((e & ~3) == (output & ~3)) {
        found_output = true;
        break;
      }
    }
    assert(found_output && "test_large_fanin_deletion: central node should have edge to output");
  }

  // Check that each intermediate node has edges
  for (int i = 0; i < 1000; ++i) {
    auto edges      = g.ref_node(intermediate_nodes[i])->get_edges(intermediate_nodes[i]);
    int  edge_count = 0;
    for (auto e : edges) {
      (void)e;
      edge_count++;
    }
    assert(edge_count >= 2 && "test_large_fanin_deletion: intermediate nodes should have at least 2 edges");
  }

  // Check that central pins have edges
  for (auto pin : central_pins) {
    auto edges     = g.ref_pin(pin)->get_edges(pin);
    bool has_edges = false;
    for (auto e : edges) {
      (void)e;
      has_edges = true;
      break;
    }
    assert(has_edges && "test_large_fanin_deletion: central pins should have edges");
  }

  std::cout << "test_large_fanin_deletion: verified edges exist\n";

  // Delete the central node
  g.delete_node(central_node);

  std::cout << "test_large_fanin_deletion: deleted central node\n";

  // Verify edges are gone after deletion
  // Check that central node has no edges to output
  {
    auto edges      = g.ref_node(central_node)->get_edges(central_node);
    int  edge_count = 0;
    for (auto e : edges) {
      (void)e;
      edge_count++;
    }
    assert(edge_count == 0 && "test_large_fanin_deletion: central node should have no edges after deletion");
  }

  // Check that intermediate nodes' edges to central node are gone
  for (int i = 0; i < 1000; ++i) {
    auto edges         = g.ref_node(intermediate_nodes[i])->get_edges(intermediate_nodes[i]);
    bool found_central = false;
    for (auto e : edges) {
      Vid e_base = (e >> 2) << 2;
      if (e_base == central_node || e_base == central_pin0 || e_base == central_pin1 || e_base == central_pin2) {
        found_central = true;
        break;
      }
    }
    assert(!found_central && "test_large_fanin_deletion: intermediate nodes should not have edges to deleted central node");
  }

  // Check that central pins have no edges
  for (auto pin : central_pins) {
    auto edges      = g.ref_pin(pin)->get_edges(pin);
    int  edge_count = 0;
    for (auto e : edges) {
      (void)e;
      edge_count++;
    }
    assert(edge_count == 0 && "test_large_fanin_deletion: central pins should have no edges after deletion");
  }

  std::cout << "test_large_fanin_deletion: verified edges are removed\n";
  std::cout << "test_large_fanin_deletion passed\n";
}

void test_fast_iter_flat() {
  GraphLibrary lib;
  const Gid    gid = lib.create_graph();
  Graph&       g   = lib.get_graph(gid);

  (void)g.create_node();  // node 4
  (void)g.create_node();  // node 5

  const auto out = g.fast_iter(false, 0, 0);
  assert(out.size() == 5 && "test_fast_iter_flat: expected 5 nodes (1..5)");

  const std::vector<Nid> expected_ids = {1ULL << 2, 2ULL << 2, 3ULL << 2, 4ULL << 2, 5ULL << 2};
  for (size_t i = 0; i < out.size(); ++i) {
    assert(out[i].get_node_id() == expected_ids[i] && "test_fast_iter_flat: node order mismatch");
    assert(out[i].get_top_graph() == gid && "test_fast_iter_flat: top_graph mismatch");
    assert(out[i].get_curr_graph() == gid && "test_fast_iter_flat: curr_graph mismatch");
    assert(out[i].get_tree_node_num() == 1 && "test_fast_iter_flat: tree_node_num mismatch");
  }

  std::cout << "test_fast_iter_flat passed\n";
}

void assert_fast_iter_vector_eq(const std::vector<Graph::FastIterator>& actual, const std::vector<Graph::FastIterator>& expected,
                                const char* msg_prefix) {
  assert(actual.size() == expected.size() && msg_prefix);
  for (size_t i = 0; i < expected.size(); ++i) {
    assert(actual[i].get_node_id() == expected[i].get_node_id() && msg_prefix);
    assert(actual[i].get_top_graph() == expected[i].get_top_graph() && msg_prefix);
    assert(actual[i].get_curr_graph() == expected[i].get_curr_graph() && msg_prefix);
    assert(actual[i].get_tree_node_num() == expected[i].get_tree_node_num() && msg_prefix);
  }
}

void test_fast_iter_hierarchy() {
  GraphLibrary lib;
  const Gid    root_gid  = lib.create_graph();
  const Gid    child_gid = lib.create_graph();
  const Gid    leaf_gid  = lib.create_graph();

  Graph& root  = lib.get_graph(root_gid);
  Graph& child = lib.get_graph(child_gid);
  Graph& leaf  = lib.get_graph(leaf_gid);

  (void)root.create_node();                         // node 4
  const Nid root_sub = root.create_node();          // node 5
  (void)root.create_node();                         // node 6
  root.ref_node(root_sub)->set_subnode(child_gid);  // node 5 -> graph 2 subnode

  (void)child.create_node();                         // node 4
  const Nid child_sub = child.create_node();         // node 5
  (void)child.create_node();                         // node 6
  child.ref_node(child_sub)->set_subnode(leaf_gid);  // node 5 -> graph 3 subnode

  (void)leaf.create_node();  // node 4

  const auto out = root.fast_iter(true, 0, 0);
  assert(out.size() == 14 && "test_fast_iter_hierarchy: expected DFS-expanded traversal size");

  const std::vector<Graph::FastIterator> expected = {
      {1ULL << 2, root_gid, root_gid, 1},
      {2ULL << 2, root_gid, root_gid, 1},
      {3ULL << 2, root_gid, root_gid, 1},
      {4ULL << 2, root_gid, root_gid, 1},
      {1ULL << 2, root_gid, child_gid, 2},
      {2ULL << 2, root_gid, child_gid, 2},
      {3ULL << 2, root_gid, child_gid, 2},
      {4ULL << 2, root_gid, child_gid, 2},
      {1ULL << 2, root_gid, leaf_gid, 3},
      {2ULL << 2, root_gid, leaf_gid, 3},
      {3ULL << 2, root_gid, leaf_gid, 3},
      {4ULL << 2, root_gid, leaf_gid, 3},
      {6ULL << 2, root_gid, child_gid, 2},
      {6ULL << 2, root_gid, root_gid, 1},
  };
  assert_fast_iter_vector_eq(out, expected, "test_fast_iter_hierarchy: mismatch");

  std::cout << "test_fast_iter_hierarchy passed\n";
}

void test_fast_iter_hierarchy_multiple_subnodes() {
  GraphLibrary lib;
  const Gid    root_gid  = lib.create_graph();
  const Gid    child_gid = lib.create_graph();
  const Gid    leaf_gid  = lib.create_graph();

  Graph& root  = lib.get_graph(root_gid);
  Graph& child = lib.get_graph(child_gid);
  Graph& leaf  = lib.get_graph(leaf_gid);

  (void)root.create_node();                          // node 4
  const Nid root_sub = root.create_node();           // node 5
  (void)root.create_node();                          // node 6
  const Nid root_sub2 = root.create_node();          // node 7
  root.ref_node(root_sub)->set_subnode(child_gid);   // node 5 -> graph 2 subnode
  root.ref_node(root_sub2)->set_subnode(child_gid);  // node 7 -> graph 2 subnode

  (void)child.create_node();                         // node 4
  const Nid child_sub = child.create_node();         // node 5
  (void)child.create_node();                         // node 6
  child.ref_node(child_sub)->set_subnode(leaf_gid);  // node 5 -> graph 3 subnode

  (void)leaf.create_node();  // node 4

  const auto out = root.fast_iter(true, 0, 0);
  assert(out.size() == 23 && "test_fast_iter_hierarchy_multiple_subnodes: expected DFS-expanded traversal size");

  const std::vector<Graph::FastIterator> expected = {
      {1ULL << 2, root_gid, root_gid, 1},  {2ULL << 2, root_gid, root_gid, 1},  {3ULL << 2, root_gid, root_gid, 1},
      {4ULL << 2, root_gid, root_gid, 1},  {1ULL << 2, root_gid, child_gid, 2}, {2ULL << 2, root_gid, child_gid, 2},
      {3ULL << 2, root_gid, child_gid, 2}, {4ULL << 2, root_gid, child_gid, 2}, {1ULL << 2, root_gid, leaf_gid, 3},
      {2ULL << 2, root_gid, leaf_gid, 3},  {3ULL << 2, root_gid, leaf_gid, 3},  {4ULL << 2, root_gid, leaf_gid, 3},
      {6ULL << 2, root_gid, child_gid, 2}, {6ULL << 2, root_gid, root_gid, 1},  {1ULL << 2, root_gid, child_gid, 4},
      {2ULL << 2, root_gid, child_gid, 4}, {3ULL << 2, root_gid, child_gid, 4}, {4ULL << 2, root_gid, child_gid, 4},
      {1ULL << 2, root_gid, leaf_gid, 5},  {2ULL << 2, root_gid, leaf_gid, 5},  {3ULL << 2, root_gid, leaf_gid, 5},
      {4ULL << 2, root_gid, leaf_gid, 5},  {6ULL << 2, root_gid, child_gid, 4},
  };

  assert_fast_iter_vector_eq(out, expected, "test_fast_iter_hierarchy_multiple_subnodes: mismatch");

  std::cout << "test_fast_iter_hierarchy_multiple_subnodes passed\n";
}

int main() {
  std::cout << "Running graph tests...\n";
  test_add_input_output_create_pins_on_builtin_nodes();
  test_create_pin_chains_pins_on_node();
  test_node_to_node();
  test_pin_to_pin();
  test_node_to_pin();
  test_pin_to_node();
  test_sedges_ledges();
  test_overflow_handling();
  test_large_fanin_deletion();
  test_fast_iter_flat();
  test_fast_iter_hierarchy();
  test_fast_iter_hierarchy_multiple_subnodes();
  std::cout << "All graph tests passed.\n";
  return 0;
}
