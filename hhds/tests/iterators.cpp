// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// API iterator tests for the planned HHDS API (see api_todo.md).
// These tests document the intended API usage; they will not compile
// until the corresponding features are implemented.
//
// All public APIs return compact types (Node_class, Pin_class, Tnode_class).
// Raw IDs (Nid, Pid, Tree_pos) are internal and never exposed to users.
//
// NOTE: Runnable tests for already-implemented APIs were moved to
// tests/iterators_impl.cpp. This file intentionally keeps only pending API
// tests (commented out) as TODO documentation.

#include <gtest/gtest.h>

#include "absl/container/flat_hash_map.h"
#include "hhds/graph.hpp"
#include "hhds/tree.hpp"

// // ---------------------------------------------------------------------------
// // Section 8: Node pin iteration (api_todo.md #8)
// // ---------------------------------------------------------------------------

TEST(PinIteration, GetPins) {
  hhds::Graph g;
  auto        node = g.create_node();
  node.create_pin(1);
  node.create_pin(2);
  node.create_pin(3);

  // pin 0 is not present yet: create_node() only creates the node entry.
  int count = 0;
  for (auto pin : g.get_pins(node)) {
    (void)pin.get_port_id();
    count++;
  }
  EXPECT_EQ(count, 3);
}

TEST(PinIteration, GetPinsIncludesPin0WhenMaterialized) {
  hhds::Graph g;
  auto        node = g.create_node();
  node.create_pin(0);  // materialize pin 0 on this node
  node.create_pin(1);
  node.create_pin(2);
  node.create_pin(3);

  int count = 0;
  for (auto pin : g.get_pins(node)) {
    (void)pin.get_port_id();
    count++;
  }
  EXPECT_EQ(count, 4);
}

// // ---------------------------------------------------------------------------
// // Section 9: Tree iterators return Tnode_class (api_todo.md #9)
// // ---------------------------------------------------------------------------

TEST(TreeIterator, PreOrderYieldsTnodeClass) {
  hhds::tree<int> t;
  auto            root = t.add_root(1);  // returns Tnode_class
  auto            c1   = t.add_child(root, 2);
  t.add_child(root, 3);
  t.add_child(c1, 4);

  // pre_order yields Tnode_class
  int count = 0;
  for (auto tnode : t.pre_order()) {
    const auto& data = tnode.get_data();  // get_data() is read-only (const X&)
    (void)data;
    count++;
  }
  EXPECT_EQ(count, 4);
}

TEST(TreeIterator, RefDataMutableAccess) {
  hhds::tree<int> t;
  auto            root = t.add_root(10);

  // get_data() is const — read-only
  const auto& ro = root.get_data();
  EXPECT_EQ(ro, 10);

  // ref_data() gives a mutable unique_ptr handle
  auto ptr = root.ref_data();
  *ptr     = 99;
  EXPECT_EQ(root.get_data(), 99);
}

// // ---------------------------------------------------------------------------
// // Section 14: Hierarchy cursor — graphs (api_todo.md #14)
// // ---------------------------------------------------------------------------

TEST(HierCursor, GraphBasicNavigation) {
  hhds::GraphLibrary lib;

  // Create a 3-level hierarchy: top -> mid -> leaf
  auto top      = lib.create_graph();  // returns shared_ptr<Graph>
  auto mid      = lib.create_graph();  // returns shared_ptr<Graph>
  auto leaf     = lib.create_graph();  // returns shared_ptr<Graph>
  auto top_gid  = top->get_gid();
  auto mid_gid  = mid->get_gid();
  auto leaf_gid = leaf->get_gid();

  // top has a node that instantiates mid
  auto sub_mid = top->create_node();
  top->set_subnode(sub_mid, mid_gid);

  // mid has a node that instantiates leaf
  auto sub_leaf = mid->create_node();
  mid->set_subnode(sub_leaf, leaf_gid);

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

  auto top     = lib.create_graph();  // returns shared_ptr<Graph>
  auto alu     = lib.create_graph();  // returns shared_ptr<Graph>
  auto reg     = lib.create_graph();  // returns shared_ptr<Graph>
  auto top_gid = top->get_gid();
  auto alu_gid = alu->get_gid();
  auto reg_gid = reg->get_gid();

  // top has two sub-instances: ALU and RegFile
  auto n_alu = top->create_node();
  top->set_subnode(n_alu, alu_gid);

  auto n_reg = top->create_node();
  top->set_subnode(n_reg, reg_gid);

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

  auto cpu_a     = lib.create_graph();  // returns shared_ptr<Graph>
  auto cpu_b     = lib.create_graph();  // returns shared_ptr<Graph>
  auto alu       = lib.create_graph();  // returns shared_ptr<Graph>
  auto cpu_a_gid = cpu_a->get_gid();
  auto cpu_b_gid = cpu_b->get_gid();
  auto alu_gid   = alu->get_gid();

  // Both CPUs instantiate the same ALU
  auto a_sub = cpu_a->create_node();
  cpu_a->set_subnode(a_sub, alu_gid);

  auto b_sub = cpu_b->create_node();
  cpu_b->set_subnode(b_sub, alu_gid);

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

  auto top     = lib.create_graph();  // returns shared_ptr<Graph>
  auto top_gid = top->get_gid();

  top->create_node();
  top->create_node();
  top->create_node();

  auto cursor = lib.create_cursor(top_gid);

  int count = 0;
  for (auto node : cursor.each_node()) {
    (void)node.get_current_gid();  // each_node yields Node_hier
    (void)node.get_port_id();
    count++;
  }
  // 3 user nodes + built-in nodes
  EXPECT_GE(count, 3);
}

// // ---------------------------------------------------------------------------
// // Section 14: Hierarchy cursor — trees/forest (api_todo.md #14)
// // ---------------------------------------------------------------------------

TEST(ForestCursor, BasicNavigation) {
  hhds::Forest<int> forest;

  auto main_tree = forest.create_tree();  // returns shared_ptr<tree<int>>
  auto sub_tree  = forest.create_tree();  // returns shared_ptr<tree<int>>
  auto leaf_tree = forest.create_tree();  // returns shared_ptr<tree<int>>
  auto main_tid  = main_tree->get_tid();
  auto sub_tid   = sub_tree->get_tid();
  auto leaf_tid  = leaf_tree->get_tid();

  // main_tree root has a child that references sub_tree
  auto root  = main_tree->add_root(1);
  auto child = main_tree->add_child(root, 2);
  main_tree->add_subtree_ref(child, sub_tid);

  // sub_tree root has a child that references leaf_tree
  auto sub_root  = sub_tree->add_root(10);
  auto sub_child = sub_tree->add_child(sub_root, 20);
  sub_tree->add_subtree_ref(sub_child, leaf_tid);

  leaf_tree->add_root(100);

  auto cursor = forest.create_cursor(main_tid);
  EXPECT_TRUE(cursor.is_root());
  EXPECT_EQ(cursor.get_current_tid(), main_tid);

  // Descend into sub_tree
  EXPECT_TRUE(cursor.goto_first_child());
  EXPECT_EQ(cursor.get_current_tid(), sub_tid);

  // Descend into leaf_tree
  EXPECT_TRUE(cursor.goto_first_child());
  EXPECT_EQ(cursor.get_current_tid(), leaf_tid);
  EXPECT_TRUE(cursor.is_leaf());

  // Go back up
  EXPECT_TRUE(cursor.goto_parent());
  EXPECT_EQ(cursor.get_current_tid(), sub_tid);

  EXPECT_TRUE(cursor.goto_parent());
  EXPECT_EQ(cursor.get_current_tid(), main_tid);
  EXPECT_TRUE(cursor.is_root());

  EXPECT_FALSE(cursor.goto_parent());
}

// // ---------------------------------------------------------------------------
// // Section 14: get_callers (api_todo.md #14)
// // ---------------------------------------------------------------------------

TEST(GetCallers, GraphCallersTracking) {
  hhds::GraphLibrary lib;

  auto cpu_a   = lib.create_graph();  // returns shared_ptr<Graph>
  auto cpu_b   = lib.create_graph();  // returns shared_ptr<Graph>
  auto alu     = lib.create_graph();  // returns shared_ptr<Graph>
  auto alu_gid = alu->get_gid();

  auto a_sub = cpu_a->create_node();
  cpu_a->set_subnode(a_sub, alu_gid);

  auto b_sub = cpu_b->create_node();
  cpu_b->set_subnode(b_sub, alu_gid);

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

  auto tree_a     = forest.create_tree();  // returns shared_ptr<tree<int>>
  auto tree_b     = forest.create_tree();  // returns shared_ptr<tree<int>>
  auto shared     = forest.create_tree();  // returns shared_ptr<tree<int>>
  auto shared_tid = shared->get_tid();

  auto a_root  = tree_a->add_root(1);
  auto a_child = tree_a->add_child(a_root, 10);
  tree_a->add_subtree_ref(a_child, shared_tid);

  auto b_root  = tree_b->add_root(2);
  auto b_child = tree_b->add_child(b_root, 20);
  tree_b->add_subtree_ref(b_child, shared_tid);

  shared->add_root(100);

  // shared_tree should have 2 callers
  int caller_count = 0;
  for (auto& c : forest.get_callers(shared_tid)) {
    (void)c.caller_tid;
    (void)c.caller_tnode;
    caller_count++;
  }
  EXPECT_EQ(caller_count, 2);
}

TEST(GetCallers, CreateCursorFromCaller) {
  hhds::GraphLibrary lib;

  auto cpu_a   = lib.create_graph();  // returns shared_ptr<Graph>
  auto cpu_b   = lib.create_graph();  // returns shared_ptr<Graph>
  auto alu     = lib.create_graph();  // returns shared_ptr<Graph>
  auto alu_gid = alu->get_gid();

  auto a_sub = cpu_a->create_node();
  cpu_a->set_subnode(a_sub, alu_gid);

  auto b_sub = cpu_b->create_node();
  cpu_b->set_subnode(b_sub, alu_gid);

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
