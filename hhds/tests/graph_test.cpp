#include "graph.hpp"

#include <cassert>
#include <iostream>
#include <random>
#include <vector>

using namespace hhds;

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

  // Create 1000 intermediate nodes
  std::vector<Nid> intermediate_nodes;
  for (int i = 0; i < 1000; ++i) {
    intermediate_nodes.push_back(g.create_node());
  }

  // Create the central node with 3 input pins
  Nid central_node = g.create_node();
  Pid central_pin0 = g.create_pin(central_node, 0);
  Pid central_pin1 = g.create_pin(central_node, 1);
  Pid central_pin2 = g.create_pin(central_node, 2);

  // Random number generator for distributing connections
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<> pin_dist(0, 2);

  // Connect all 1000 nodes to input1 and to one of the central node's pins
  std::vector<Pid> central_pins = {central_pin0, central_pin1, central_pin2};
  ankerl::unordered_dense::map<Pid, int> pin_connection_counts;
  pin_connection_counts[central_pin0] = 0;
  pin_connection_counts[central_pin1] = 0;
  pin_connection_counts[central_pin2] = 0;

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
    auto edges = g.ref_node(central_node)->get_edges(central_node);
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
    auto edges = g.ref_node(intermediate_nodes[i])->get_edges(intermediate_nodes[i]);
    int edge_count = 0;
    for (auto e : edges) {
      (void)e;
      edge_count++;
    }
    assert(edge_count >= 2 && "test_large_fanin_deletion: intermediate nodes should have at least 2 edges");
  }

  // Check that central pins have edges
  for (auto pin : central_pins) {
    auto edges = g.ref_pin(pin)->get_edges(pin);
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
    auto edges = g.ref_node(central_node)->get_edges(central_node);
    int edge_count = 0;
    for (auto e : edges) {
      (void)e;
      edge_count++;
    }
    assert(edge_count == 0 && "test_large_fanin_deletion: central node should have no edges after deletion");
  }

  // Check that intermediate nodes' edges to central node are gone
  for (int i = 0; i < 1000; ++i) {
    auto edges = g.ref_node(intermediate_nodes[i])->get_edges(intermediate_nodes[i]);
    bool found_central = false;
    for (auto e : edges) {
      Vid e_base = (e >> 2) << 2;
      if (e_base == central_node || e_base == central_pin0 || e_base == central_pin1 || e_base == central_pin2) {
        found_central = true;
        break;
      }
    }
    assert(!found_central
           && "test_large_fanin_deletion: intermediate nodes should not have edges to deleted central node");
  }

  // Check that central pins have no edges
  for (auto pin : central_pins) {
    auto edges = g.ref_pin(pin)->get_edges(pin);
    int edge_count = 0;
    for (auto e : edges) {
      (void)e;
      edge_count++;
    }
    assert(edge_count == 0 && "test_large_fanin_deletion: central pins should have no edges after deletion");
  }

  std::cout << "test_large_fanin_deletion: verified edges are removed\n";
  std::cout << "test_large_fanin_deletion passed\n";
}

int main() {
  std::cout << "Running graph tests...\n";
  test_node_to_node();
  test_pin_to_pin();
  test_node_to_pin();
  test_pin_to_node();
  test_sedges_ledges();
  test_overflow_handling();
  test_large_fanin_deletion();
  std::cout << "All graph tests passed.\n";
  return 0;
}
