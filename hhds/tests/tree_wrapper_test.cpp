// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <gtest/gtest.h>

#include <memory>
#include <sstream>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "hhds/tree.hpp"

namespace {

struct DeclaredTree {
  std::shared_ptr<hhds::TreeIO> tio;
  std::shared_ptr<hhds::Tree>   tree;
};

DeclaredTree create_declared_tree(const std::shared_ptr<hhds::Forest>& forest, std::string_view name) {
  auto tio  = forest->create_io(name);
  auto tree = tio->create_tree();
  return {std::move(tio), std::move(tree)};
}

}  // namespace

TEST(TreeWrappers, NodeClassHashable) {
  auto tree = hhds::Tree::create();

  auto root  = tree->add_root_node();
  auto child = root.add_child();

  EXPECT_TRUE(root.is_valid());
  EXPECT_TRUE(child.is_valid());
  EXPECT_NE(root, child);
  EXPECT_EQ(child.parent(), root);

  absl::flat_hash_map<hhds::Tree::Node_class, int> attrs;
  attrs[root]  = 1;
  attrs[child] = 2;

  EXPECT_EQ(attrs[root], 1);
  EXPECT_EQ(attrs[child], 2);
}

TEST(TreeWrappers, ForestContextAndSubtreeRefs) {
  auto forest = hhds::Forest::create();

  const auto parent_decl = create_declared_tree(forest, "parent");
  const auto child_decl  = create_declared_tree(forest, "child");
  auto       child_tio   = child_decl.tio;

  auto& parent = *parent_decl.tree;
  auto& child  = *child_decl.tree;

  auto parent_root = parent.add_root_node();
  child.add_root_node();
  auto ref_node = parent_root.add_child();

  ref_node.set_subnode(child_tio);

  EXPECT_TRUE(ref_node.has_subnode());
  EXPECT_EQ(parent.get_subnode(ref_node), child_tio->get_tid());
}

TEST(TreeWrappers, TraversalsYieldNodeClass) {
  auto tree = hhds::Tree::create();

  auto root   = tree->add_root_node();
  auto child1 = root.add_child();
  auto child2 = root.add_child();
  auto grand  = child1.add_child();

  std::vector<hhds::Tree::Node_class> preorder;
  for (auto node : tree->pre_order()) {
    preorder.push_back(node);
  }
  ASSERT_EQ(preorder.size(), 4U);
  EXPECT_EQ(preorder[0], root);
  EXPECT_EQ(preorder[1], child1);
  EXPECT_EQ(preorder[2], grand);
  EXPECT_EQ(preorder[3], child2);

  std::vector<hhds::Tree::Node_class> sibling_order;
  for (auto node : child1.sibling_order()) {
    sibling_order.push_back(node);
  }
  ASSERT_EQ(sibling_order.size(), 2U);
  EXPECT_EQ(sibling_order[0], child1);
  EXPECT_EQ(sibling_order[1], child2);

  std::vector<hhds::Tree::Node_class> postorder;
  for (auto node : root.post_order_class()) {
    postorder.push_back(node);
  }
  ASSERT_EQ(postorder.size(), 4U);
  EXPECT_EQ(postorder[0], grand);
  EXPECT_EQ(postorder[1], child1);
  EXPECT_EQ(postorder[2], child2);
  EXPECT_EQ(postorder[3], root);
}

TEST(TreeWrappers, TreeCursorNavigatesSingleTree) {
  auto tree = hhds::Tree::create();

  auto root   = tree->add_root_node();
  auto child1 = root.add_child();
  auto child2 = root.add_child();
  auto grand  = child1.add_child();

  auto cursor = tree->create_cursor(root);
  EXPECT_TRUE(cursor.is_root());
  EXPECT_EQ(cursor.get_current_pos(), root.get_debug_nid());
  EXPECT_EQ(cursor.depth(), 0);

  EXPECT_TRUE(cursor.goto_first_child());
  EXPECT_EQ(cursor.get_current_pos(), child1.get_debug_nid());
  EXPECT_EQ(cursor.depth(), 1);

  EXPECT_TRUE(cursor.goto_first_child());
  EXPECT_EQ(cursor.get_current_pos(), grand.get_debug_nid());
  EXPECT_TRUE(cursor.is_leaf());
  EXPECT_EQ(cursor.depth(), 2);

  EXPECT_TRUE(cursor.goto_parent());
  EXPECT_EQ(cursor.get_current_pos(), child1.get_debug_nid());
  EXPECT_EQ(cursor.depth(), 1);

  EXPECT_TRUE(cursor.goto_next_sibling());
  EXPECT_EQ(cursor.get_current_pos(), child2.get_debug_nid());
  EXPECT_EQ(cursor.depth(), 1);

  EXPECT_TRUE(cursor.goto_prev_sibling());
  EXPECT_EQ(cursor.get_current_pos(), child1.get_debug_nid());
  EXPECT_FALSE(cursor.goto_prev_sibling());
}

TEST(TreeWrappers, ForestCursorFollowsSubtreeRefs) {
  auto forest = hhds::Forest::create();

  const auto parent_decl = create_declared_tree(forest, "parent");
  const auto child_decl  = create_declared_tree(forest, "child");
  const auto parent_tid  = parent_decl.tio->get_tid();
  const auto child_tid   = child_decl.tio->get_tid();

  auto& parent = *parent_decl.tree;
  auto& child  = *child_decl.tree;

  auto parent_root  = parent.add_root_node();
  auto callsite     = parent_root.add_child();
  auto parent_other = parent_root.add_child();
  auto child_root   = child.add_root_node();
  auto child_leaf   = child_root.add_child();

  callsite.set_subnode(child_decl.tio);
  (void)child_tid;

  auto cursor = forest->create_cursor(parent_tid);
  EXPECT_TRUE(cursor.is_root());
  EXPECT_EQ(cursor.get_current_tid(), parent_tid);
  EXPECT_EQ(cursor.get_current_pos(), parent_root.get_debug_nid());
  EXPECT_EQ(cursor.depth(), 0);

  EXPECT_TRUE(cursor.goto_first_child());
  EXPECT_EQ(cursor.get_current_tid(), parent_tid);
  EXPECT_EQ(cursor.get_current_pos(), callsite.get_debug_nid());
  EXPECT_EQ(cursor.depth(), 1);

  EXPECT_TRUE(cursor.goto_first_child());
  EXPECT_EQ(cursor.get_current_tid(), child_tid);
  EXPECT_EQ(cursor.get_current_pos(), child_root.get_debug_nid());
  EXPECT_EQ(cursor.depth(), 2);

  EXPECT_TRUE(cursor.goto_first_child());
  EXPECT_EQ(cursor.get_current_tid(), child_tid);
  EXPECT_EQ(cursor.get_current_pos(), child_leaf.get_debug_nid());
  EXPECT_TRUE(cursor.is_leaf());
  EXPECT_EQ(cursor.depth(), 3);

  EXPECT_TRUE(cursor.goto_parent());
  EXPECT_EQ(cursor.get_current_tid(), child_tid);
  EXPECT_EQ(cursor.get_current_pos(), child_root.get_debug_nid());
  EXPECT_EQ(cursor.depth(), 2);

  EXPECT_TRUE(cursor.goto_parent());
  EXPECT_EQ(cursor.get_current_tid(), parent_tid);
  EXPECT_EQ(cursor.get_current_pos(), callsite.get_debug_nid());
  EXPECT_EQ(cursor.depth(), 1);

  EXPECT_TRUE(cursor.goto_next_sibling());
  EXPECT_EQ(cursor.get_current_tid(), parent_tid);
  EXPECT_EQ(cursor.get_current_pos(), parent_other.get_debug_nid());
  EXPECT_EQ(cursor.depth(), 1);
}

TEST(TreeWrappers, GetSubsReturnsDirectSubtreeCallsites) {
  auto forest = hhds::Forest::create();

  const auto top_decl   = create_declared_tree(forest, "top");
  const auto child_decl = create_declared_tree(forest, "child");

  auto& top   = *top_decl.tree;
  auto& child = *child_decl.tree;

  auto root    = top.add_root_node();
  auto call1   = root.add_child();
  auto regular = root.add_child();
  auto call2   = root.add_child();

  child.add_root_node();
  call1.set_subnode(child_decl.tio);
  call2.set_subnode(child_decl.tio);

  const auto subs = top.get_subs();
  ASSERT_EQ(subs.size(), 2U);
  EXPECT_EQ(subs[0], call1);
  EXPECT_EQ(subs[1], call2);
  EXPECT_NE(subs[0], regular);
}

TEST(TreeWrappers, PrintUsesTypeTableAndAttributes) {
  auto tree = hhds::Tree::create();

  auto root  = tree->add_root_node();
  auto child = root.add_child();

  root.set_type(1);
  child.set_type(2);

  hhds::Tree::PrintOptions options;
  const hhds::Type_entry   type_table[] = {
      {"invalid", hhds::Statement_class::Node},
      {"add", hhds::Statement_class::Node},
      {"literal", hhds::Statement_class::Node},
  };
  options.type_table = type_table;
  options.attributes = {
      {"type_id",
       [](const hhds::Tree::Node_class& node) -> std::optional<std::string> { return std::to_string(node.get_type()); }},
  };

  std::ostringstream os;
  tree->print(os, options);

  EXPECT_EQ(os.str(),
            "tree {\n"
            "  %8  = add     @(type_id=1)\n"
            "  %16 = literal @(type_id=2)\n"
            "}\n");
}

TEST(TreeWrappers, PrintReturnsString) {
  auto tree = hhds::Tree::create();
  tree->set_name("mytest");

  auto root = tree->add_root_node();
  root.set_type(0);

  const auto result = tree->print();
  EXPECT_EQ(result,
            "mytest {\n"
            "  %8 = type(0)\n"
            "}\n");
}

TEST(TreeWrappers, PrintScopeTypes) {
  auto tree = hhds::Tree::create();
  tree->set_name("scoped");

  auto root  = tree->add_root_node();
  auto scope = root.add_child();
  auto leaf  = scope.add_child();

  root.set_type(0);
  scope.set_type(1);
  leaf.set_type(0);

  const hhds::Type_entry type_table[] = {
      {"node", hhds::Statement_class::Node},
      {"if_taken", hhds::Statement_class::Open_call},
  };

  hhds::Tree::PrintOptions options;
  options.type_table = type_table;

  const auto result = tree->print(options);
  EXPECT_EQ(result,
            "scoped {\n"
            "  %8  = node\n"
            "  %16 = if_taken {\n"
            "    %24 = node\n"
            "  }\n"
            "}\n");
}

TEST(TreeWrappers, PrintUsesBuiltinNameAttribute) {
  auto tree = hhds::Tree::create();
  tree->set_name("named");

  auto root  = tree->add_root_node();
  auto child = root.add_child();

  root.set_type(1);
  child.set_type(2);
  root.attr(hhds::attrs::name).set("program");
  child.attr(hhds::attrs::name).set("literal");

  const hhds::Type_entry type_table[] = {
      {"invalid", hhds::Statement_class::Node},
      {"module", hhds::Statement_class::Node},
      {"literal", hhds::Statement_class::Node},
  };

  hhds::Tree::PrintOptions options;
  options.type_table = type_table;

  EXPECT_EQ(tree->print(options),
            "named {\n"
            "  %8  = program : module\n"
            "  %16 = literal\n"
            "}\n");
}
