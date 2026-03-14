// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <set>
#include <stdexcept>
#include <vector>

#include <gtest/gtest.h>

#include "hhds/tree.hpp"

namespace {

hhds::Tree& create_rooted_tree(hhds::Forest& forest, hhds::Tree_pos& tree_tid) {
  tree_tid = forest.create_tree();
  auto& tree = forest.get_tree(tree_tid);
  tree.add_root();
  return tree;
}

}  // namespace

TEST(ForestCorrectness, BasicForestOperations) {
  hhds::Forest forest;

  hhds::Tree_pos tree1_tid;
  hhds::Tree_pos tree2_tid;
  auto&          tree1 = create_rooted_tree(forest, tree1_tid);
  auto&          tree2 = create_rooted_tree(forest, tree2_tid);

  EXPECT_LT(tree1_tid, 0);
  EXPECT_LT(tree2_tid, 0);
  EXPECT_NE(tree1_tid, tree2_tid);
  EXPECT_EQ(tree1.get_root(), hhds::ROOT);
  EXPECT_EQ(tree2.get_root(), hhds::ROOT);
}

TEST(ForestCorrectness, SubtreeReferences) {
  hhds::Forest forest;

  hhds::Tree_pos main_tree_tid;
  hhds::Tree_pos subtree_tid;
  auto&          main_tree = create_rooted_tree(forest, main_tree_tid);
  auto&          sub_tree  = create_rooted_tree(forest, subtree_tid);

  auto child1 = main_tree.add_child(main_tree.get_root());
  main_tree.add_child(main_tree.get_root());
  sub_tree.add_child(sub_tree.get_root());

  main_tree.add_subtree_ref(child1, subtree_tid);

  bool deleted = forest.delete_tree(subtree_tid);
  EXPECT_FALSE(deleted);
  auto& still_there = forest.get_tree(subtree_tid);
  EXPECT_EQ(still_there.get_root(), hhds::ROOT);

  main_tree.delete_leaf(child1);

  deleted = forest.delete_tree(subtree_tid);
  EXPECT_TRUE(deleted);
  EXPECT_THROW(static_cast<void>(forest.get_tree(subtree_tid)), std::runtime_error);
}

TEST(ForestCorrectness, TreeTraversalWithSubtrees) {
  hhds::Forest forest;

  hhds::Tree_pos main_tree_tid;
  hhds::Tree_pos subtree_tid;
  auto&          main_tree = create_rooted_tree(forest, main_tree_tid);
  auto&          sub_tree  = create_rooted_tree(forest, subtree_tid);

  auto child1 = main_tree.add_child(main_tree.get_root());
  main_tree.add_child(main_tree.get_root());
  sub_tree.add_child(sub_tree.get_root());
  sub_tree.add_child(sub_tree.get_root());

  main_tree.add_subtree_ref(child1, subtree_tid);

  int count = 0;
  for (auto it = main_tree.pre_order_with_subtrees(main_tree.get_root(), true).begin();
       it != main_tree.pre_order_with_subtrees(main_tree.get_root(), true).end();
       ++it) {
    ++count;
  }
  EXPECT_EQ(count, 6);

  int count_no_subtree = 0;
  for (auto it = main_tree.pre_order_with_subtrees(main_tree.get_root(), false).begin();
       it != main_tree.pre_order_with_subtrees(main_tree.get_root(), false).end();
       ++it) {
    ++count_no_subtree;
  }
  EXPECT_EQ(count_no_subtree, 3);
}

TEST(ForestCorrectness, CycleTraversal) {
  hhds::Forest forest;

  hhds::Tree_pos a_ref;
  hhds::Tree_pos b_ref;
  auto&          a = create_rooted_tree(forest, a_ref);
  auto&          b = create_rooted_tree(forest, b_ref);

  auto a_child_ref = a.add_child(a.get_root());
  auto b_child_ref = b.add_child(b.get_root());

  a.add_subtree_ref(a_child_ref, b_ref);
  b.add_subtree_ref(b_child_ref, a_ref);

  int count = 0;
  for (auto it = a.pre_order_with_subtrees(a.get_root(), true).begin();
       it != a.pre_order_with_subtrees(a.get_root(), true).end();
       ++it) {
    ++count;
  }

  EXPECT_EQ(count, 6);
}

TEST(ForestCorrectness, ComplexForestOperations) {
  hhds::Forest forest;

  hhds::Tree_pos main_tree_tid;
  hhds::Tree_pos subtree1_tid;
  hhds::Tree_pos subtree2_tid;
  hhds::Tree_pos subtree3_tid;
  auto&          main_tree = create_rooted_tree(forest, main_tree_tid);
  auto&          sub_tree1 = create_rooted_tree(forest, subtree1_tid);
  auto&          sub_tree2 = create_rooted_tree(forest, subtree2_tid);
  auto&          sub_tree3 = create_rooted_tree(forest, subtree3_tid);

  std::vector<hhds::Tree_pos> main_nodes{main_tree.get_root()};
  std::vector<hhds::Tree_pos> sub1_nodes{sub_tree1.get_root()};
  std::vector<hhds::Tree_pos> sub2_nodes{sub_tree2.get_root()};
  std::vector<hhds::Tree_pos> sub3_nodes{sub_tree3.get_root()};

  for (int i = 0; i < 100; ++i) {
    auto parent = main_nodes[i / 3];
    main_nodes.push_back(main_tree.add_child(parent));
  }

  auto sub1_parent = sub_tree1.get_root();
  for (int i = 0; i < 50; ++i) {
    sub1_nodes.push_back(sub_tree1.add_child(sub1_parent));
  }

  for (int i = 0; i < 32; ++i) {
    auto parent = sub2_nodes[i];
    sub2_nodes.push_back(sub_tree2.add_child(parent));
    sub2_nodes.push_back(sub_tree2.add_child(parent));
  }

  auto current_pos = sub_tree3.get_root();
  for (int i = 0; i < 100; ++i) {
    auto new_node = sub_tree3.add_child(current_pos);
    sub3_nodes.push_back(new_node);
    current_pos = new_node;
  }

  main_tree.add_subtree_ref(main_nodes[0], subtree1_tid);
  sub_tree1.add_subtree_ref(sub1_nodes[0], subtree2_tid);
  sub_tree2.add_subtree_ref(sub2_nodes[0], subtree3_tid);

  bool deleted = forest.delete_tree(subtree3_tid);
  EXPECT_FALSE(deleted);

  int node_count = 0;
  for (auto it = main_tree.pre_order_with_subtrees(main_tree.get_root(), true).begin();
       it != main_tree.pre_order_with_subtrees(main_tree.get_root(), true).end();
       ++it) {
    ++node_count;
    ASSERT_LE(node_count, 10000);
  }

  EXPECT_GE(node_count, 318);
}

TEST(ForestCorrectness, TombstoneDeletion) {
  hhds::Forest forest;

  hhds::Tree_pos t1;
  hhds::Tree_pos t2;
  hhds::Tree_pos t3;
  create_rooted_tree(forest, t1);
  create_rooted_tree(forest, t2);
  create_rooted_tree(forest, t3);

  EXPECT_EQ(t1, -1);
  EXPECT_EQ(t2, -2);
  EXPECT_EQ(t3, -3);

  EXPECT_EQ(forest.get_tree(t2).get_root(), hhds::ROOT);
  forest.delete_tree(t2);

  EXPECT_THROW(static_cast<void>(forest.get_tree(t2)), std::runtime_error);

  hhds::Tree_pos t4;
  create_rooted_tree(forest, t4);
  EXPECT_EQ(t4, -4);
}
