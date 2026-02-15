// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// API iterator tests for the planned HHDS API (see api_todo.md).
// These tests document the intended API usage; they will not compile
// until the corresponding features are implemented.
//
// All public APIs return compact types (Node_class, Pin_class, Tnode_class).
// Raw IDs (Nid, Pid, Tree_pos) are internal and never exposed to users.

#include <gtest/gtest.h>

#include "hhds/graph.hpp"
#include "hhds/tree.hpp"

// ---------------------------------------------------------------------------
// Section 1: Compact ID types (api_todo.md #1)
// ---------------------------------------------------------------------------

TEST(CompactTypes, NodeClassFromGraph) {
  hhds::Graph g;

  // create_node returns Node_class directly (not raw Nid)
  auto node = g.create_node();
  EXPECT_EQ(node.get_port_id(), 0);  // Node always has port 0
}

TEST(CompactTypes, PinClassFromGraph) {
  hhds::Graph g;
  auto node = g.create_node();
  auto pin  = g.create_pin(node, 3);

  EXPECT_EQ(pin.get_nid(), node.get_nid());
  EXPECT_EQ(pin.get_port_id(), 3);
}

TEST(CompactTypes, NodeClassHashable) {
  hhds::Graph g;
  auto n1 = g.create_node();
  auto n2 = g.create_node();

  EXPECT_EQ(n1, n1);
  EXPECT_NE(n1, n2);

  // Usable as hash map key for external attribute tables
  std::unordered_map<hhds::Node_class, int> attrs;
  attrs[n1] = 42;
  EXPECT_EQ(attrs[n1], 42);
}

// ---------------------------------------------------------------------------
// Section 2: Direction-aware edge iteration (api_todo.md #2)
// ---------------------------------------------------------------------------

TEST(EdgeIteration, OutEdges) {
  hhds::Graph g;
  auto n1 = g.create_node();
  auto n2 = g.create_node();
  auto n3 = g.create_node();
  g.add_edge(n1, n2);
  g.add_edge(n1, n3);

  // out_edges: edges where n1 is the driver
  int count = 0;
  for (auto edge : g.out_edges(n1)) {
    EXPECT_EQ(edge.driver.get_port_id(), 0);
    count++;
  }
  EXPECT_EQ(count, 2);
}

TEST(EdgeIteration, InpEdges) {
  hhds::Graph g;
  auto n1 = g.create_node();
  auto n2 = g.create_node();
  auto n3 = g.create_node();
  g.add_edge(n1, n3);
  g.add_edge(n2, n3);

  // inp_edges: edges where n3 is the sink
  int count = 0;
  for (auto edge : g.inp_edges(n3)) {
    EXPECT_EQ(edge.sink.get_port_id(), 0);
    count++;
  }
  EXPECT_EQ(count, 2);
}

// ---------------------------------------------------------------------------
// Section 3: del_edge (api_todo.md #3)
// ---------------------------------------------------------------------------

TEST(DelEdge, BasicRemoval) {
  hhds::Graph g;
  auto n1  = g.create_node();
  auto n2  = g.create_node();
  auto p1  = g.create_pin(n1, 0);
  auto p2  = g.create_pin(n2, 0);
  g.add_edge(p1, p2);

  g.del_edge(p1, p2);

  int count = 0;
  for (auto edge : g.out_edges(n1)) {
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
  auto n1 = g.create_node();
  auto n2 = g.create_node();
  auto n3 = g.create_node();
  g.add_edge(n1, n2);
  g.add_edge(n2, n3);

  // fast_class: lazy range over all nodes in a single graph
  int count = 0;
  for (auto node : g.fast_class()) {
    (void)node.get_port_id();
    count++;
  }
  // 3 user nodes + built-in nodes (INPUT, OUTPUT, CONST)
  EXPECT_GE(count, 3);
}

// ---------------------------------------------------------------------------
// Section 8: connect(Node_class, Node_class) shorthand (api_todo.md #8)
// ---------------------------------------------------------------------------

TEST(ConnectShorthand, NodeToNode) {
  hhds::Graph g;
  auto n1 = g.create_node();
  auto n2 = g.create_node();

  // Implicit pin 0 on both sides
  g.connect(n1, n2);

  int count = 0;
  for (auto edge : g.out_edges(n1)) {
    (void)edge;
    count++;
  }
  EXPECT_EQ(count, 1);
}

// ---------------------------------------------------------------------------
// Section 9: Node pin iteration (api_todo.md #9)
// ---------------------------------------------------------------------------

TEST(PinIteration, GetPins) {
  hhds::Graph g;
  auto node = g.create_node();
  g.create_pin(node, 1);
  g.create_pin(node, 2);
  g.create_pin(node, 3);

  int count = 0;
  for (auto pin : g.get_pins(node)) {
    (void)pin.get_port_id();
    count++;
  }
  EXPECT_EQ(count, 3);
}

// ---------------------------------------------------------------------------
// Section 10: Tree iterators return Tnode_class (api_todo.md #10)
// ---------------------------------------------------------------------------

TEST(TreeIterator, PreOrderYieldsTnodeClass) {
  hhds::tree<int> t;
  auto root = t.add_root(1);       // returns Tnode_class
  auto c1   = t.add_child(root, 2);
  t.add_child(root, 3);
  t.add_child(c1, 4);

  // pre_order yields Tnode_class
  int count = 0;
  for (auto tnode : t.pre_order()) {
    auto& data = tnode.get_data();
    (void)data;
    count++;
  }
  EXPECT_EQ(count, 4);
}

// ---------------------------------------------------------------------------
// Section 15: Hierarchy cursor — graphs (api_todo.md #15)
// ---------------------------------------------------------------------------

TEST(HierCursor, GraphBasicNavigation) {
  hhds::GraphLibrary lib;

  // Create a 3-level hierarchy: top -> mid -> leaf
  auto top_gid  = lib.create_graph();
  auto mid_gid  = lib.create_graph();
  auto leaf_gid = lib.create_graph();

  auto& top = lib.get_graph(top_gid);
  auto& mid = lib.get_graph(mid_gid);

  // top has a node that instantiates mid
  auto sub_mid = top.create_node();
  top.set_subnode(sub_mid, mid_gid);

  // mid has a node that instantiates leaf
  auto sub_leaf = mid.create_node();
  mid.set_subnode(sub_leaf, leaf_gid);

  // Create cursor rooted at top
  auto cursor = lib.create_cursor(top_gid);
  EXPECT_TRUE(cursor.is_root());
  EXPECT_EQ(cursor.get_current_gid(), top_gid);
  EXPECT_EQ(cursor.get_root_gid(), top_gid);

  // Descend into mid
  EXPECT_TRUE(cursor.goto_first_child());
  EXPECT_EQ(cursor.get_current_gid(), mid_gid);
  EXPECT_FALSE(cursor.is_root());
  EXPECT_EQ(cursor.depth(), 1);

  // Descend into leaf
  EXPECT_TRUE(cursor.goto_first_child());
  EXPECT_EQ(cursor.get_current_gid(), leaf_gid);
  EXPECT_TRUE(cursor.is_leaf());
  EXPECT_EQ(cursor.depth(), 2);

  // Cannot go further down
  EXPECT_FALSE(cursor.goto_first_child());

  // Go back up to mid
  EXPECT_TRUE(cursor.goto_parent());
  EXPECT_EQ(cursor.get_current_gid(), mid_gid);

  // Go back up to top
  EXPECT_TRUE(cursor.goto_parent());
  EXPECT_EQ(cursor.get_current_gid(), top_gid);
  EXPECT_TRUE(cursor.is_root());

  // Cannot go above root
  EXPECT_FALSE(cursor.goto_parent());
}

TEST(HierCursor, GraphSiblingNavigation) {
  hhds::GraphLibrary lib;

  auto top_gid = lib.create_graph();
  auto alu_gid = lib.create_graph();
  auto reg_gid = lib.create_graph();

  auto& top = lib.get_graph(top_gid);

  // top has two sub-instances: ALU and RegFile
  auto n_alu = top.create_node();
  top.set_subnode(n_alu, alu_gid);

  auto n_reg = top.create_node();
  top.set_subnode(n_reg, reg_gid);

  auto cursor = lib.create_cursor(top_gid);
  EXPECT_TRUE(cursor.goto_first_child());

  // Should be at first sub-instance
  auto first = cursor.get_current_gid();

  // Move to sibling
  EXPECT_TRUE(cursor.goto_next_sibling());
  auto second = cursor.get_current_gid();
  EXPECT_NE(first, second);

  // No more siblings
  EXPECT_FALSE(cursor.goto_next_sibling());

  // Go back to previous sibling
  EXPECT_TRUE(cursor.goto_prev_sibling());
  EXPECT_EQ(cursor.get_current_gid(), first);
}

TEST(HierCursor, GraphSharedModuleDisambiguation) {
  hhds::GraphLibrary lib;

  auto cpu_a_gid = lib.create_graph();
  auto cpu_b_gid = lib.create_graph();
  auto alu_gid   = lib.create_graph();

  auto& cpu_a = lib.get_graph(cpu_a_gid);
  auto& cpu_b = lib.get_graph(cpu_b_gid);

  // Both CPUs instantiate the same ALU
  auto a_sub = cpu_a.create_node();
  cpu_a.set_subnode(a_sub, alu_gid);

  auto b_sub = cpu_b.create_node();
  cpu_b.set_subnode(b_sub, alu_gid);

  // Cursor rooted at CPU_A: going into ALU then up returns to CPU_A
  auto cursor_a = lib.create_cursor(cpu_a_gid);
  EXPECT_TRUE(cursor_a.goto_first_child());
  EXPECT_EQ(cursor_a.get_current_gid(), alu_gid);
  EXPECT_TRUE(cursor_a.goto_parent());
  EXPECT_EQ(cursor_a.get_current_gid(), cpu_a_gid);

  // Cursor rooted at CPU_B: going into ALU then up returns to CPU_B
  auto cursor_b = lib.create_cursor(cpu_b_gid);
  EXPECT_TRUE(cursor_b.goto_first_child());
  EXPECT_EQ(cursor_b.get_current_gid(), alu_gid);
  EXPECT_TRUE(cursor_b.goto_parent());
  EXPECT_EQ(cursor_b.get_current_gid(), cpu_b_gid);
}

TEST(HierCursor, GraphIterateNodesAtLevel) {
  hhds::GraphLibrary lib;

  auto top_gid = lib.create_graph();
  auto& top    = lib.get_graph(top_gid);

  top.create_node();
  top.create_node();
  top.create_node();

  auto cursor = lib.create_cursor(top_gid);

  int count = 0;
  for (auto node : cursor.each_node()) {
    (void)node.get_port_id();
    count++;
  }
  // 3 user nodes + built-in nodes
  EXPECT_GE(count, 3);
}

// ---------------------------------------------------------------------------
// Section 15: Hierarchy cursor — trees/forest (api_todo.md #15)
// ---------------------------------------------------------------------------

TEST(ForestCursor, BasicNavigation) {
  hhds::Forest<int> forest;

  auto main_ref = forest.create_tree(1);
  auto sub_ref  = forest.create_tree(10);
  auto leaf_ref = forest.create_tree(100);

  auto& main_tree = forest.get_tree(main_ref);
  auto& sub_tree  = forest.get_tree(sub_ref);

  // main_tree root has a child that references sub_tree
  auto root  = main_tree.get_root();  // returns Tnode_class
  auto child = main_tree.add_child(root, 2);
  main_tree.add_subtree_ref(child, sub_ref);

  // sub_tree root has a child that references leaf_tree
  auto sub_root  = sub_tree.get_root();
  auto sub_child = sub_tree.add_child(sub_root, 20);
  sub_tree.add_subtree_ref(sub_child, leaf_ref);

  auto cursor = forest.create_cursor(main_ref);
  EXPECT_TRUE(cursor.is_root());
  EXPECT_EQ(cursor.get_current_tid(), main_ref);

  // Descend into sub_tree
  EXPECT_TRUE(cursor.goto_first_child());
  EXPECT_EQ(cursor.get_current_tid(), sub_ref);

  // Descend into leaf_tree
  EXPECT_TRUE(cursor.goto_first_child());
  EXPECT_EQ(cursor.get_current_tid(), leaf_ref);
  EXPECT_TRUE(cursor.is_leaf());

  // Go back up
  EXPECT_TRUE(cursor.goto_parent());
  EXPECT_EQ(cursor.get_current_tid(), sub_ref);

  EXPECT_TRUE(cursor.goto_parent());
  EXPECT_EQ(cursor.get_current_tid(), main_ref);
  EXPECT_TRUE(cursor.is_root());

  EXPECT_FALSE(cursor.goto_parent());
}

// ---------------------------------------------------------------------------
// Section 15: get_callers (api_todo.md #15)
// ---------------------------------------------------------------------------

TEST(GetCallers, GraphCallersTracking) {
  hhds::GraphLibrary lib;

  auto cpu_a_gid = lib.create_graph();
  auto cpu_b_gid = lib.create_graph();
  auto alu_gid   = lib.create_graph();

  auto& cpu_a = lib.get_graph(cpu_a_gid);
  auto& cpu_b = lib.get_graph(cpu_b_gid);

  auto a_sub = cpu_a.create_node();
  cpu_a.set_subnode(a_sub, alu_gid);

  auto b_sub = cpu_b.create_node();
  cpu_b.set_subnode(b_sub, alu_gid);

  // ALU should have 2 callers
  int caller_count = 0;
  for (auto& c : lib.get_callers(alu_gid)) {
    (void)c.caller_gid;
    (void)c.caller_node;
    caller_count++;
  }
  EXPECT_EQ(caller_count, 2);
}

TEST(GetCallers, ForestCallersTracking) {
  hhds::Forest<int> forest;

  auto tree_a_ref = forest.create_tree(1);
  auto tree_b_ref = forest.create_tree(2);
  auto shared_ref = forest.create_tree(100);

  auto& tree_a = forest.get_tree(tree_a_ref);
  auto& tree_b = forest.get_tree(tree_b_ref);

  auto a_root  = tree_a.get_root();
  auto a_child = tree_a.add_child(a_root, 10);
  tree_a.add_subtree_ref(a_child, shared_ref);

  auto b_root  = tree_b.get_root();
  auto b_child = tree_b.add_child(b_root, 20);
  tree_b.add_subtree_ref(b_child, shared_ref);

  // shared_tree should have 2 callers
  int caller_count = 0;
  for (auto& c : forest.get_callers(shared_ref)) {
    (void)c.caller_tid;
    (void)c.caller_tnode;
    caller_count++;
  }
  EXPECT_EQ(caller_count, 2);
}

TEST(GetCallers, CreateCursorFromCaller) {
  hhds::GraphLibrary lib;

  auto cpu_a_gid = lib.create_graph();
  auto cpu_b_gid = lib.create_graph();
  auto alu_gid   = lib.create_graph();

  auto& cpu_a = lib.get_graph(cpu_a_gid);
  auto& cpu_b = lib.get_graph(cpu_b_gid);

  auto a_sub = cpu_a.create_node();
  cpu_a.set_subnode(a_sub, alu_gid);

  auto b_sub = cpu_b.create_node();
  cpu_b.set_subnode(b_sub, alu_gid);

  // Pick a caller and create a rooted cursor from it
  auto callers_it = lib.get_callers(alu_gid).begin();
  auto cursor     = lib.create_cursor(callers_it->caller_gid);

  // Descend into ALU from that specific parent
  EXPECT_TRUE(cursor.goto_first_child());
  EXPECT_EQ(cursor.get_current_gid(), alu_gid);

  // Going up returns to the caller we picked, not the other one
  EXPECT_TRUE(cursor.goto_parent());
  EXPECT_EQ(cursor.get_current_gid(), callers_it->caller_gid);
}
