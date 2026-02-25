// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// Runnable iterator/API tests for features that are currently implemented.
// Pending API tests remain in tests/iterators.cpp.

#include <gtest/gtest.h>

#include "absl/container/flat_hash_map.h"
#include "hhds/graph.hpp"

// ---------------------------------------------------------------------------
// Section 1: Compact ID types (api_todo.md #1)
// ---------------------------------------------------------------------------

TEST(CompactTypes, NodeClassFromGraph) {
  hhds::Graph g;

  // create_node returns Node_class directly (not raw Nid)
  auto node = g.create_node();
  EXPECT_EQ(node.get_port_id(), 0);  // Node_class encodes port-id 0 identity
}

TEST(CompactTypes, PinClassFromGraph) {
  hhds::Graph g;
  auto        node = g.create_node();
  auto        pin  = node.create_pin(3);

  EXPECT_EQ(pin.get_raw_nid(), node.get_raw_nid());  // get_raw_nid() for internal cross-check only
  EXPECT_EQ(pin.get_port_id(), 3);
}

TEST(CompactTypes, NodeClassHashable) {
  hhds::Graph g;
  auto        n1 = g.create_node();
  auto        n2 = g.create_node();

  EXPECT_EQ(n1, n1);
  EXPECT_NE(n1, n2);

  // Usable as absl::flat_hash_map key for external attribute tables
  absl::flat_hash_map<hhds::Node_class, int> attrs;
  attrs[n1] = 42;
  EXPECT_EQ(attrs[n1], 42);
}

// ---------------------------------------------------------------------------
// Section 2: Direction-aware edge iteration (api_todo.md #2)
// ---------------------------------------------------------------------------

TEST(EdgeIteration, OutEdges) {
  hhds::Graph g;
  auto        n1 = g.create_node();
  auto        n2 = g.create_node();
  auto        n3 = g.create_node();
  g.add_edge(n1, n2);
  g.add_edge(n1, n3);

  // out_edges: edges where n1 is the driver
  int count = 0;
  for (auto edge : g.out_edges(n1)) {
    EXPECT_EQ(edge.driver.get_port_id(), 0);
    count++;
  }
  EXPECT_EQ(count, 2);

  count = 0;
  for (auto edge : g.inp_edges(n1)) {
    EXPECT_EQ(edge.driver.get_port_id(), 0);
    count++;
  }
  EXPECT_EQ(count, 0);

  count = 0;
  for (auto edge : g.out_edges(n2)) {
    EXPECT_EQ(edge.driver.get_port_id(), 0);
    count++;
  }
  EXPECT_EQ(count, 0);

  count = 0;
  for (auto edge : g.inp_edges(n2)) {
    EXPECT_EQ(edge.driver.get_port_id(), 0);
    count++;
  }
  EXPECT_EQ(count, 1);
}

TEST(EdgeIteration, InpEdges) {
  hhds::Graph g;
  auto        n1 = g.create_node();
  auto        n2 = g.create_node();
  auto        n3 = g.create_node();
  g.add_edge(n1, n3);
  g.add_edge(n2, n3);

  // inp_edges: edges where n3 is the sink
  int count = 0;
  for (auto edge : g.inp_edges(n3)) {
    EXPECT_EQ(edge.sink.get_port_id(), 0);
    count++;
  }
  EXPECT_EQ(count, 2);

  count = 0;
  for (auto edge : g.inp_edges(n1)) {
    EXPECT_EQ(edge.sink.get_port_id(), 0);
    count++;
  }
  EXPECT_EQ(count, 0);
}

// ---------------------------------------------------------------------------
// Section 3: del_edge (api_todo.md #3)
// ---------------------------------------------------------------------------

TEST(DelEdge, BasicRemoval) {
  hhds::Graph g;
  auto        n1 = g.create_node();
  auto        n2 = g.create_node();
  auto        p1 = n1.create_pin(0);
  auto        p2 = n2.create_pin(0);
  g.add_edge(p1, p2);

  int count = 0;
  for (auto edge : g.out_edges(n1)) {
    (void)edge;
    count++;
  }
  EXPECT_EQ(count, 0);  // node (pin0) has no edge

  count = 0;
  // TODO: out_edge/inp_edges accepts node (pin0) and Pin_class/Pin_flat too
  for (auto edge : g.out_edges(p1)) {
    EXPECT_EQ(edge.driver, p1);
    EXPECT_EQ(edge.sink, p2);

    // TODO: get_master_node()-> Node_class
    EXPECT_EQ(edge.driver.get_master_node(), n1);
    EXPECT_EQ(edge.sink.get_master_node(), n2);
    count++;
  }
  EXPECT_EQ(count, 1);
  count = 0;
  for (auto edge : g.inp_edges(p2)) {
    EXPECT_EQ(edge.driver, p1);
    EXPECT_EQ(edge.sink, p2);

    // TODO: get_master_node()-> Node_class
    EXPECT_EQ(edge.driver.get_master_node(), n1);
    EXPECT_EQ(edge.sink.get_master_node(), n2);
    count++;
  }
  EXPECT_EQ(count, 1);

  count = 0;
  for (auto edge : g.inp_edges(p1)) {
    (void)edge;
    count++;
  }
  EXPECT_EQ(count, 0);

  count = 0;
  for (auto edge : g.out_edges(p2)) {
    (void)edge;
    count++;
  }
  EXPECT_EQ(count, 0);

  count = 0;
  for (auto edge : g.out_edges(n1)) {
    (void)edge;
    count++;
  }
  EXPECT_EQ(count, 0);

  count = 0;
  for (auto edge : g.out_edges(n2)) {
    (void)edge;
    count++;
  }
  EXPECT_EQ(count, 0);

  // DELETE, and then no edges in any direction
  g.del_edge(p1, p2);

  count = 0;
  for (auto edge : g.out_edges(n1)) {
    (void)edge;
    count++;
  }
  EXPECT_EQ(count, 0);

  count = 0;
  for (auto edge : g.inp_edges(n1)) {
    (void)edge;
    count++;
  }
  EXPECT_EQ(count, 0);

  count = 0;
  for (auto edge : g.out_edges(p1)) {
    (void)edge;
    count++;
  }
  EXPECT_EQ(count, 0);

  count = 0;
  for (auto edge : g.inp_edges(p1)) {
    (void)edge;
    count++;
  }

  count = 0;
  for (auto edge : g.out_edges(n2)) {
    (void)edge;
    count++;
  }
  EXPECT_EQ(count, 0);

  count = 0;
  for (auto edge : g.inp_edges(n2)) {
    (void)edge;
    count++;
  }
  EXPECT_EQ(count, 0);

  count = 0;
  for (auto edge : g.out_edges(p2)) {
    (void)edge;
    count++;
  }
  EXPECT_EQ(count, 0);

  count = 0;
  for (auto edge : g.inp_edges(p2)) {
    (void)edge;
    count++;
  }
  EXPECT_EQ(count, 0);
}

// ---------------------------------------------------------------------------
// Section 4: Lazy graph traversal (api_todo.md #4)
// ---------------------------------------------------------------------------

TEST(LazyTraversal, FastClassSingleGraph) {
  hhds::Graph g;
  auto        n1 = g.create_node();
  auto        n2 = g.create_node();
  auto        n3 = g.create_node();
  g.add_edge(n1, n2);
  g.add_edge(n2, n3);

  // fast_class: span over the graph's internal node storage (cheap, no allocation)
  int count = 0;
  for (auto node : g.fast_class()) {
    (void)node.get_port_id();
    count++;
  }
  // 3 user nodes + built-in nodes (INPUT, OUTPUT, CONST)
  EXPECT_GE(count, 3);
}

TEST(LazyTraversal, FastFlatSingleGraph) {
  hhds::GraphLibrary lib;
  const hhds::Gid    gid = lib.create_graph();
  auto&              g   = lib.get_graph(gid);

  (void)g.create_node();
  (void)g.create_node();

  auto nodes = g.fast_flat();

  // 3 built-ins + 2 created nodes
  EXPECT_EQ(nodes.size(), 5);
  for (const auto& node : nodes) {
    EXPECT_EQ(node.get_root_gid(), gid);
    EXPECT_EQ(node.get_current_gid(), gid);
    EXPECT_EQ(node.get_port_id(), 0);
  }
}

TEST(LazyTraversal, FastFlatHierarchy) {
  hhds::GraphLibrary lib;
  const hhds::Gid    root_gid  = lib.create_graph();
  const hhds::Gid    child_gid = lib.create_graph();
  const hhds::Gid    leaf_gid  = lib.create_graph();

  auto& root  = lib.get_graph(root_gid);
  auto& child = lib.get_graph(child_gid);
  auto& leaf  = lib.get_graph(leaf_gid);

  (void)root.create_node();
  auto root_sub = root.create_node();
  (void)root.create_node();
  root.ref_node(root_sub.get_raw_nid())->set_subnode(child_gid);

  (void)child.create_node();
  auto child_sub = child.create_node();
  (void)child.create_node();
  child.ref_node(child_sub.get_raw_nid())->set_subnode(leaf_gid);

  (void)leaf.create_node();

  auto nodes = root.fast_flat();

  EXPECT_EQ(nodes.size(), 14);

  int root_count  = 0;
  int child_count = 0;
  int leaf_count  = 0;

  for (const auto& node : nodes) {
    EXPECT_EQ(node.get_root_gid(), root_gid);
    if (node.get_current_gid() == root_gid) {
      root_count++;
    } else if (node.get_current_gid() == child_gid) {
      child_count++;
    } else if (node.get_current_gid() == leaf_gid) {
      leaf_count++;
    } else {
      FAIL() << "Unexpected current_gid in fast_flat traversal";
    }
  }

  EXPECT_EQ(root_count, 5);
  EXPECT_EQ(child_count, 5);
  EXPECT_EQ(leaf_count, 4);
}

// ---------------------------------------------------------------------------
// Section 4 (cont.): add_edge(Node_class, Node_class) pin-0 shorthand
// ---------------------------------------------------------------------------

TEST(AddEdgeShorthand, NodeToNode) {
  hhds::Graph g;
  auto        n1 = g.create_node();
  auto        n2 = g.create_node();

  // add_edge with Node_class uses implicit pin 0 on both sides
  g.add_edge(n1, n2);

  int count = 0;
  for (auto edge : g.out_edges(n1)) {
    (void)edge;
    count++;
  }
  EXPECT_EQ(count, 1);
}
