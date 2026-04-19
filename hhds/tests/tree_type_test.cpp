// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <gtest/gtest.h>

#include "hhds/tree.hpp"

TEST(TreeType, DefaultAndExplicitTypes) {
  auto tree = hhds::Tree::create();

  auto root   = tree->add_root_node();
  auto child1 = root.add_child();
  auto child2 = root.add_child();

  EXPECT_EQ(root.get_type(), 0);
  EXPECT_EQ(child1.get_type(), 0);
  EXPECT_EQ(child2.get_type(), 0);

  root.set_type(7);
  child1.set_type(11);
  child2.set_type(13);

  EXPECT_EQ(root.get_type(), 7);
  EXPECT_EQ(child1.get_type(), 11);
  EXPECT_EQ(child2.get_type(), 13);
}

TEST(TreeType, TypeSurvivesLeafCompaction) {
  auto tree = hhds::Tree::create();

  auto root   = tree->add_root_node();
  auto child1 = root.add_child();
  auto child2 = root.add_child();

  child1.set_type(21);
  child2.set_type(22);

  child1.del_node();

  auto surviving = root.first_child();
  EXPECT_TRUE(surviving.is_valid());
  EXPECT_EQ(surviving.get_type(), 22);
}
