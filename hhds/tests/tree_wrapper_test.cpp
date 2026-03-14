// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <gtest/gtest.h>

#include <vector>

#include "absl/container/flat_hash_map.h"
#include "hhds/tree.hpp"

TEST(TreeWrappers, NodeClassHashable) {
  hhds::Tree tree;

  const auto root  = tree.add_root_node();
  const auto child = tree.add_child(root);

  EXPECT_TRUE(hhds::Tree::is_valid(root));
  EXPECT_TRUE(hhds::Tree::is_valid(child));
  EXPECT_NE(root, child);
  EXPECT_EQ(tree.get_parent(child), root);

  absl::flat_hash_map<hhds::Tree::Node_class, int> attrs;
  attrs[root]  = 1;
  attrs[child] = 2;

  EXPECT_EQ(attrs[root], 1);
  EXPECT_EQ(attrs[child], 2);
}

TEST(TreeWrappers, CompactConversions) {
  hhds::Tree      tree;
  const hhds::Tid current_tid = -7;
  const hhds::Tid root_tid    = -3;

  const auto root = tree.add_root_node();
  const auto flat = hhds::to_flat(root, current_tid, root_tid);

  EXPECT_EQ(flat.get_root_tid(), root_tid);
  EXPECT_EQ(flat.get_current_tid(), current_tid);
  EXPECT_EQ(flat.get_raw_tid(), root.get_raw_tid());

  const auto class_from_flat = hhds::to_class(flat);
  EXPECT_EQ(class_from_flat, root);

  const auto hier = tree.as_hier(root.get_raw_tid(), current_tid, 42, root_tid);
  EXPECT_EQ(hier.get_root_tid(), root_tid);
  EXPECT_EQ(hier.get_current_tid(), current_tid);
  EXPECT_EQ(hier.get_hier_pos(), 42);
  EXPECT_EQ(hier.get_raw_tid(), root.get_raw_tid());

  const auto flat_from_hier  = hhds::to_flat(hier);
  const auto class_from_hier = hhds::to_class(hier);
  EXPECT_EQ(flat_from_hier, flat);
  EXPECT_EQ(class_from_hier, root);
}

TEST(TreeWrappers, ForestContextAndSubtreeRefs) {
  hhds::Forest forest;

  const auto parent_tid = forest.create_tree();
  const auto child_tid  = forest.create_tree();

  auto& parent = forest.get_tree(parent_tid);
  auto& child  = forest.get_tree(child_tid);

  const auto parent_root = parent.add_root_node();
  const auto child_root  = child.add_root_node();
  const auto ref_node    = parent.add_child(parent_root);

  parent.add_subtree_ref(ref_node, child_tid);

  EXPECT_TRUE(parent.is_subtree_ref(ref_node));
  EXPECT_EQ(parent.get_subtree_ref(ref_node), child_tid);

  const auto flat = parent.as_flat(ref_node.get_raw_tid(), parent_tid);
  EXPECT_EQ(flat.get_root_tid(), parent_tid);
  EXPECT_EQ(flat.get_current_tid(), parent_tid);
  EXPECT_EQ(flat.get_raw_tid(), ref_node.get_raw_tid());

  const auto hier = parent.as_hier(ref_node.get_raw_tid(), parent_tid, child_root.get_raw_tid());
  EXPECT_EQ(hier.get_root_tid(), parent_tid);
  EXPECT_EQ(hier.get_current_tid(), parent_tid);
  EXPECT_EQ(hier.get_hier_pos(), child_root.get_raw_tid());
}

TEST(TreeWrappers, TraversalsYieldNodeClass) {
  hhds::Tree tree;

  const auto root   = tree.add_root_node();
  const auto child1 = tree.add_child(root);
  const auto child2 = tree.add_child(root);
  const auto grand  = tree.add_child(child1);

  std::vector<hhds::Tree::Node_class> preorder;
  for (auto node : tree.pre_order()) {
    preorder.push_back(node);
  }
  ASSERT_EQ(preorder.size(), 4U);
  EXPECT_EQ(preorder[0], root);
  EXPECT_EQ(preorder[1], child1);
  EXPECT_EQ(preorder[2], grand);
  EXPECT_EQ(preorder[3], child2);

  std::vector<hhds::Tree::Node_class> sibling_order;
  for (auto node : tree.sibling_order(child1)) {
    sibling_order.push_back(node);
  }
  ASSERT_EQ(sibling_order.size(), 2U);
  EXPECT_EQ(sibling_order[0], child1);
  EXPECT_EQ(sibling_order[1], child2);

  std::vector<hhds::Tree::Node_class> postorder;
  for (auto node : tree.post_order(root)) {
    postorder.push_back(node);
  }
  ASSERT_EQ(postorder.size(), 4U);
  EXPECT_EQ(postorder[0], grand);
  EXPECT_EQ(postorder[1], child1);
  EXPECT_EQ(postorder[2], child2);
  EXPECT_EQ(postorder[3], root);
}
