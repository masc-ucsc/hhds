// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <stdexcept>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "hhds/tree.hpp"

namespace {

struct DeclaredTree {
  std::shared_ptr<hhds::TreeIO> tio;
  std::shared_ptr<hhds::Tree>   tree;
};

DeclaredTree create_rooted_tree(const std::shared_ptr<hhds::Forest>& forest, std::string_view name) {
  auto tio  = forest->create_io(name);
  auto tree = tio->create_tree();
  tree->add_root();
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
  EXPECT_EQ(tree1.tree->get_root(), hhds::ROOT);
  EXPECT_EQ(tree2.tree->get_root(), hhds::ROOT);
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

  auto child1 = main_tree.tree->add_child(main_tree.tree->get_root());
  main_tree.tree->add_child(main_tree.tree->get_root());
  sub_tree.tree->add_child(sub_tree.tree->get_root());

  main_tree.tree->set_subnode(child1, sub_tree.tio->get_tid());

  bool deleted = forest->delete_tree(sub_tree.tio->get_tid());
  EXPECT_FALSE(deleted);
  auto still_there = forest->find_tree("sub");
  ASSERT_NE(still_there, nullptr);
  EXPECT_EQ(still_there->get_root(), hhds::ROOT);

  main_tree.tree->delete_leaf(child1);

  deleted = forest->delete_tree(sub_tree.tio->get_tid());
  EXPECT_TRUE(deleted);
  EXPECT_EQ(forest->find_tree("sub"), nullptr);
  EXPECT_EQ(forest->find_io("sub"), nullptr);
}

TEST(ForestCorrectness, TreeTraversalWithSubtrees) {
  auto forest = hhds::Forest::create();

  auto main_tree = create_rooted_tree(forest, "main");
  auto sub_tree  = create_rooted_tree(forest, "sub");

  auto child1 = main_tree.tree->add_child(main_tree.tree->get_root());
  main_tree.tree->add_child(main_tree.tree->get_root());
  sub_tree.tree->add_child(sub_tree.tree->get_root());
  sub_tree.tree->add_child(sub_tree.tree->get_root());

  main_tree.tree->set_subnode(child1, sub_tree.tio->get_tid());

  int count = 0;
  for (auto it = main_tree.tree->pre_order_with_subtrees(main_tree.tree->get_root(), true).begin();
       it != main_tree.tree->pre_order_with_subtrees(main_tree.tree->get_root(), true).end();
       ++it) {
    ++count;
  }
  EXPECT_EQ(count, 6);

  int count_no_subtree = 0;
  for (auto it = main_tree.tree->pre_order_with_subtrees(main_tree.tree->get_root(), false).begin();
       it != main_tree.tree->pre_order_with_subtrees(main_tree.tree->get_root(), false).end();
       ++it) {
    ++count_no_subtree;
  }
  EXPECT_EQ(count_no_subtree, 3);
}

TEST(ForestCorrectness, CycleTraversal) {
  auto forest = hhds::Forest::create();

  auto a = create_rooted_tree(forest, "a");
  auto b = create_rooted_tree(forest, "b");

  auto a_child_ref = a.tree->add_child(a.tree->get_root());
  auto b_child_ref = b.tree->add_child(b.tree->get_root());

  a.tree->set_subnode(a_child_ref, b.tio->get_tid());
  b.tree->set_subnode(b_child_ref, a.tio->get_tid());

  int count = 0;
  for (auto it = a.tree->pre_order_with_subtrees(a.tree->get_root(), true).begin();
       it != a.tree->pre_order_with_subtrees(a.tree->get_root(), true).end();
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

  std::vector<hhds::Tree_pos> main_nodes{main_tree.tree->get_root()};
  std::vector<hhds::Tree_pos> sub1_nodes{sub_tree1.tree->get_root()};
  std::vector<hhds::Tree_pos> sub2_nodes{sub_tree2.tree->get_root()};
  std::vector<hhds::Tree_pos> sub3_nodes{sub_tree3.tree->get_root()};

  for (int i = 0; i < 100; ++i) {
    auto parent = main_nodes[i / 3];
    main_nodes.push_back(main_tree.tree->add_child(parent));
  }

  auto sub1_parent = sub_tree1.tree->get_root();
  for (int i = 0; i < 50; ++i) {
    sub1_nodes.push_back(sub_tree1.tree->add_child(sub1_parent));
  }

  for (int i = 0; i < 32; ++i) {
    auto parent = sub2_nodes[i];
    sub2_nodes.push_back(sub_tree2.tree->add_child(parent));
    sub2_nodes.push_back(sub_tree2.tree->add_child(parent));
  }

  auto current_pos = sub_tree3.tree->get_root();
  for (int i = 0; i < 100; ++i) {
    auto new_node = sub_tree3.tree->add_child(current_pos);
    sub3_nodes.push_back(new_node);
    current_pos = new_node;
  }

  main_tree.tree->set_subnode(main_nodes[0], sub_tree1.tio->get_tid());
  sub_tree1.tree->set_subnode(sub1_nodes[0], sub_tree2.tio->get_tid());
  sub_tree2.tree->set_subnode(sub2_nodes[0], sub_tree3.tio->get_tid());

  bool deleted = forest->delete_tree(sub_tree3.tio->get_tid());
  EXPECT_FALSE(deleted);

  int node_count = 0;
  for (auto it = main_tree.tree->pre_order_with_subtrees(main_tree.tree->get_root(), true).begin();
       it != main_tree.tree->pre_order_with_subtrees(main_tree.tree->get_root(), true).end();
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

  EXPECT_EQ(forest->get_tree(t2.tio->get_tid()).get_root(), hhds::ROOT);
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

