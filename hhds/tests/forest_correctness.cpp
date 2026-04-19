// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <gtest/gtest.h>

#include <stdexcept>
#include <string>
#include <vector>

#include "hhds/tree.hpp"

namespace {

struct DeclaredTree {
  std::shared_ptr<hhds::TreeIO> tio;
  std::shared_ptr<hhds::Tree>   tree;
};

DeclaredTree create_rooted_tree(const std::shared_ptr<hhds::Forest>& forest, std::string_view name) {
  auto tio  = forest->create_io(name);
  auto tree = tio->create_tree();
  (void)tree->add_root_node();
  return {std::move(tio), std::move(tree)};
}

}  // namespace

TEST(ForestCorrectness, BasicForestOperations) {
  auto forest = hhds::Forest::create();

  const auto tree1 = create_rooted_tree(forest, "tree1");
  const auto tree2 = create_rooted_tree(forest, "tree2");

  EXPECT_LT(tree1.tio->get_tid(), 0);
  EXPECT_LT(tree2.tio->get_tid(), 0);
  EXPECT_NE(tree1.tio->get_tid(), tree2.tio->get_tid());
  EXPECT_TRUE(tree1.tree->get_root_node().is_valid());
  EXPECT_TRUE(tree2.tree->get_root_node().is_valid());
  EXPECT_EQ(tree1.tree->get_root_node().get_debug_nid(), hhds::ROOT);
}

TEST(ForestCorrectness, TreeIOCanExistWithoutBody) {
  auto forest = hhds::Forest::create();

  auto tio = forest->create_io("parser");

  ASSERT_NE(tio, nullptr);
  EXPECT_EQ(tio->get_name(), "parser");
  EXPECT_FALSE(tio->has_tree());
  EXPECT_EQ(tio->get_tree(), nullptr);
  EXPECT_EQ(forest->find_io("parser"), tio);
  EXPECT_EQ(forest->find_tree("parser"), nullptr);

  auto tree = tio->create_tree();
  ASSERT_NE(tree, nullptr);
  EXPECT_TRUE(tio->has_tree());
  EXPECT_EQ(tio->get_tree(), tree);
  EXPECT_EQ(tree->get_io(), tio);
  EXPECT_EQ(tree->get_tid(), tio->get_tid());
  EXPECT_EQ(forest->find_tree("parser"), tree);
}

TEST(ForestCorrectness, SubtreeReferences) {
  auto forest = hhds::Forest::create();

  auto main_tree = create_rooted_tree(forest, "main");
  auto sub_tree  = create_rooted_tree(forest, "sub");

  auto main_root = main_tree.tree->get_root_node();
  auto sub_root  = sub_tree.tree->get_root_node();

  auto child1 = main_root.add_child();
  main_root.add_child();
  sub_root.add_child();

  child1.set_subnode(sub_tree.tio);

  bool deleted = forest->delete_tree(sub_tree.tio->get_tid());
  EXPECT_FALSE(deleted);
  auto still_there = forest->find_tree("sub");
  ASSERT_NE(still_there, nullptr);
  EXPECT_TRUE(still_there->get_root_node().is_valid());

  child1.del_node();

  deleted = forest->delete_tree(sub_tree.tio->get_tid());
  EXPECT_TRUE(deleted);
  EXPECT_EQ(forest->find_tree("sub"), nullptr);
  EXPECT_EQ(forest->find_io("sub"), nullptr);
}

TEST(ForestCorrectness, TreeTraversalWithSubtrees) {
  auto forest = hhds::Forest::create();

  auto main_tree = create_rooted_tree(forest, "main");
  auto sub_tree  = create_rooted_tree(forest, "sub");

  auto main_root = main_tree.tree->get_root_node();
  auto sub_root  = sub_tree.tree->get_root_node();

  auto child1 = main_root.add_child();
  main_root.add_child();
  sub_root.add_child();
  sub_root.add_child();

  child1.set_subnode(sub_tree.tio);

  int count = 0;
  for (auto it = main_tree.tree->pre_order_with_subtrees(main_root, true).begin();
       it != main_tree.tree->pre_order_with_subtrees(main_root, true).end();
       ++it) {
    ++count;
  }
  EXPECT_EQ(count, 6);

  int count_no_subtree = 0;
  for (auto it = main_tree.tree->pre_order_with_subtrees(main_root, false).begin();
       it != main_tree.tree->pre_order_with_subtrees(main_root, false).end();
       ++it) {
    ++count_no_subtree;
  }
  EXPECT_EQ(count_no_subtree, 3);
}

TEST(ForestCorrectness, CycleTraversal) {
  auto forest = hhds::Forest::create();

  auto a = create_rooted_tree(forest, "a");
  auto b = create_rooted_tree(forest, "b");

  auto a_root = a.tree->get_root_node();
  auto b_root = b.tree->get_root_node();

  auto a_child_ref = a_root.add_child();
  auto b_child_ref = b_root.add_child();

  a_child_ref.set_subnode(b.tio);
  b_child_ref.set_subnode(a.tio);

  int count = 0;
  for (auto it = a.tree->pre_order_with_subtrees(a_root, true).begin();
       it != a.tree->pre_order_with_subtrees(a_root, true).end();
       ++it) {
    ++count;
  }

  EXPECT_EQ(count, 6);
}

TEST(ForestCorrectness, ComplexForestOperations) {
  auto forest = hhds::Forest::create();

  auto main_tree = create_rooted_tree(forest, "main");
  auto sub_tree1 = create_rooted_tree(forest, "sub1");
  auto sub_tree2 = create_rooted_tree(forest, "sub2");
  auto sub_tree3 = create_rooted_tree(forest, "sub3");

  std::vector<hhds::Tree::Node_class> main_nodes{main_tree.tree->get_root_node()};
  std::vector<hhds::Tree::Node_class> sub1_nodes{sub_tree1.tree->get_root_node()};
  std::vector<hhds::Tree::Node_class> sub2_nodes{sub_tree2.tree->get_root_node()};
  std::vector<hhds::Tree::Node_class> sub3_nodes{sub_tree3.tree->get_root_node()};

  for (int i = 0; i < 100; ++i) {
    auto parent = main_nodes[i / 3];
    main_nodes.push_back(parent.add_child());
  }

  auto sub1_parent = sub_tree1.tree->get_root_node();
  for (int i = 0; i < 50; ++i) {
    sub1_nodes.push_back(sub1_parent.add_child());
  }

  for (int i = 0; i < 32; ++i) {
    auto parent = sub2_nodes[i];
    sub2_nodes.push_back(parent.add_child());
    sub2_nodes.push_back(parent.add_child());
  }

  auto current_node = sub_tree3.tree->get_root_node();
  for (int i = 0; i < 100; ++i) {
    auto new_node = current_node.add_child();
    sub3_nodes.push_back(new_node);
    current_node = new_node;
  }

  main_nodes[0].set_subnode(sub_tree1.tio);
  sub1_nodes[0].set_subnode(sub_tree2.tio);
  sub2_nodes[0].set_subnode(sub_tree3.tio);

  bool deleted = forest->delete_tree(sub_tree3.tio->get_tid());
  EXPECT_FALSE(deleted);

  auto main_root = main_tree.tree->get_root_node();
  int  node_count = 0;
  for (auto it = main_tree.tree->pre_order_with_subtrees(main_root, true).begin();
       it != main_tree.tree->pre_order_with_subtrees(main_root, true).end();
       ++it) {
    ++node_count;
    ASSERT_LE(node_count, 10000);
  }

  EXPECT_GE(node_count, 318);
}

TEST(ForestCorrectness, TombstoneDeletion) {
  auto forest = hhds::Forest::create();

  const auto t1 = create_rooted_tree(forest, "t1");
  const auto t2 = create_rooted_tree(forest, "t2");
  const auto t3 = create_rooted_tree(forest, "t3");

  EXPECT_EQ(t1.tio->get_tid(), -1);
  EXPECT_EQ(t2.tio->get_tid(), -2);
  EXPECT_EQ(t3.tio->get_tid(), -3);

  EXPECT_TRUE(forest->get_tree(t2.tio->get_tid()).get_root_node().is_valid());
  EXPECT_TRUE(forest->delete_tree(t2.tio->get_tid()));

  EXPECT_THROW(static_cast<void>(forest->get_tree(t2.tio->get_tid())), std::runtime_error);

  const auto t4 = create_rooted_tree(forest, "t4");
  EXPECT_EQ(t4.tio->get_tid(), -4);
}

TEST(ForestCorrectness, CreateAndFindByName) {
  auto forest = hhds::Forest::create();

  auto parser_tio = forest->create_io("parser");
  auto parser     = parser_tio->create_tree();

  ASSERT_NE(parser_tio, nullptr);
  ASSERT_NE(parser, nullptr);
  EXPECT_EQ(parser_tio->get_tid(), -1);
  EXPECT_EQ(parser_tio->get_name(), "parser");
  EXPECT_EQ(parser->get_name(), "parser");
  EXPECT_EQ(forest->find_io("parser"), parser_tio);
  EXPECT_EQ(forest->find_tree("parser"), parser);
  EXPECT_EQ(forest->find_tree("missing"), nullptr);
}

TEST(ForestCorrectness, DeleteTreeRemovesNameLookup) {
  auto forest = hhds::Forest::create();

  auto parser_tio = forest->create_io("parser");
  (void)parser_tio->create_tree();
  ASSERT_NE(forest->find_tree("parser"), nullptr);
  ASSERT_NE(forest->find_io("parser"), nullptr);

  forest->delete_treeio(parser_tio);
  EXPECT_EQ(forest->find_tree("parser"), nullptr);
  EXPECT_EQ(forest->find_io("parser"), nullptr);
}

TEST(ForestCorrectness, DuplicateNamesRejected) {
  auto forest = hhds::Forest::create();
  (void)forest->create_io("parser");

  EXPECT_THROW(static_cast<void>(forest->create_io("parser")), std::runtime_error);
}
