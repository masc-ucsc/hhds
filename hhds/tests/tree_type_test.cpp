// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <gtest/gtest.h>

#include "hhds/tree.hpp"

TEST(TreeType, DefaultAndExplicitTypes) {
  auto tree = hhds::Tree::create();

  const auto root   = tree->add_root_node();
  const auto child1 = tree->add_child(root);
  const auto child2 = tree->add_child(root);

  EXPECT_EQ(tree->get_type(root), 0);
  EXPECT_EQ(tree->get_type(child1), 0);
  EXPECT_EQ(tree->get_type(child2), 0);

  tree->set_type(root, 7);
  tree->set_type(child1, 11);
  tree->set_type(child2, 13);

  EXPECT_EQ(tree->get_type(root), 7);
  EXPECT_EQ(tree->get_type(child1), 11);
  EXPECT_EQ(tree->get_type(child2), 13);
}

TEST(TreeType, TypeSurvivesLeafCompaction) {
  auto tree = hhds::Tree::create();

  const auto root   = tree->add_root_node();
  const auto child1 = tree->add_child(root);
  const auto child2 = tree->add_child(root);

  tree->set_type(child1, 21);
  tree->set_type(child2, 22);

  tree->delete_leaf(child1);

  const auto surviving = tree->get_first_child(root);
  EXPECT_TRUE(surviving.is_valid());
  EXPECT_EQ(tree->get_type(surviving), 22);
}
