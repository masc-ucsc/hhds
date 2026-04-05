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
  auto tio  = forest->create_treeio(name);
  auto tree = tio->create_tree();
  return {std::move(tio), std::move(tree)};
}

}  // namespace

TEST(TreeWrappers, NodeClassHashable) {
  auto tree = hhds::Tree::create();

  const auto root  = tree->add_root_node();
  const auto child = tree->add_child(root);

  EXPECT_TRUE(hhds::Tree::is_valid(root));
  EXPECT_TRUE(hhds::Tree::is_valid(child));
  EXPECT_NE(root, child);
  EXPECT_EQ(tree->get_parent(child), root);

  absl::flat_hash_map<hhds::Tree::Node_class, int> attrs;
  attrs[root]  = 1;
  attrs[child] = 2;

  EXPECT_EQ(attrs[root], 1);
  EXPECT_EQ(attrs[child], 2);
}

TEST(TreeWrappers, CompactConversions) {
  auto            tree = hhds::Tree::create();
  const hhds::Tid current_tid = -7;
  const hhds::Tid root_tid    = -3;

  const auto root = tree->add_root_node();
  const auto flat = hhds::to_flat(root, current_tid, root_tid);

  EXPECT_EQ(flat.get_root_tid(), root_tid);
  EXPECT_EQ(flat.get_current_tid(), current_tid);
  EXPECT_EQ(flat.get_current_pos(), root.get_current_pos());

  const auto class_from_flat = hhds::to_class(flat);
  EXPECT_EQ(class_from_flat, root);

  const hhds::Tid hier_tid = -11;
  const auto      hier     = tree->as_hier(root.get_current_pos(), current_tid, hier_tid, 42, root_tid);
  EXPECT_EQ(hier.get_root_tid(), root_tid);
  EXPECT_EQ(hier.get_current_tid(), current_tid);
  EXPECT_EQ(hier.get_hier_tid(), hier_tid);
  EXPECT_EQ(hier.get_hier_pos(), 42);
  EXPECT_EQ(hier.get_current_pos(), root.get_current_pos());

  const auto flat_from_hier  = hhds::to_flat(hier);
  const auto class_from_hier = hhds::to_class(hier);
  EXPECT_EQ(flat_from_hier, flat);
  EXPECT_EQ(class_from_hier, root);
}

TEST(TreeWrappers, ForestContextAndSubtreeRefs) {
  auto forest = hhds::Forest::create();

  const auto parent_decl = create_declared_tree(forest, "parent");
  const auto child_decl  = create_declared_tree(forest, "child");
  const auto parent_tid  = parent_decl.tio->get_tid();
  const auto child_tid   = child_decl.tio->get_tid();

  auto& parent = *parent_decl.tree;
  auto& child  = *child_decl.tree;

  const auto parent_root = parent.add_root_node();
  const auto child_root  = child.add_root_node();
  const auto ref_node    = parent.add_child(parent_root);

  parent.set_subnode(ref_node, child_tid);

  EXPECT_TRUE(parent.has_subnode(ref_node));
  EXPECT_EQ(parent.get_subnode(ref_node), child_tid);

  const auto flat = parent.as_flat(ref_node.get_current_pos(), parent_tid);
  EXPECT_EQ(flat.get_root_tid(), parent_tid);
  EXPECT_EQ(flat.get_current_tid(), parent_tid);
  EXPECT_EQ(flat.get_current_pos(), ref_node.get_current_pos());

  const auto hier = parent.as_hier(ref_node.get_current_pos(), parent_tid, child_tid, child_root.get_current_pos());
  EXPECT_EQ(hier.get_root_tid(), parent_tid);
  EXPECT_EQ(hier.get_current_tid(), parent_tid);
  EXPECT_EQ(hier.get_hier_tid(), child_tid);
  EXPECT_EQ(hier.get_hier_pos(), child_root.get_current_pos());
}

TEST(TreeWrappers, TraversalsYieldNodeClass) {
  auto tree = hhds::Tree::create();

  const auto root   = tree->add_root_node();
  const auto child1 = tree->add_child(root);
  const auto child2 = tree->add_child(root);
  const auto grand  = tree->add_child(child1);

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
  for (auto node : tree->sibling_order(child1)) {
    sibling_order.push_back(node);
  }
  ASSERT_EQ(sibling_order.size(), 2U);
  EXPECT_EQ(sibling_order[0], child1);
  EXPECT_EQ(sibling_order[1], child2);

  std::vector<hhds::Tree::Node_class> postorder;
  for (auto node : tree->post_order(root)) {
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

  const auto root   = tree->add_root_node();
  const auto child1 = tree->add_child(root);
  const auto child2 = tree->add_child(root);
  const auto grand  = tree->add_child(child1);

  auto cursor = tree->create_cursor(root);
  EXPECT_TRUE(cursor.is_root());
  EXPECT_EQ(cursor.get_current_pos(), root.get_current_pos());
  EXPECT_EQ(cursor.depth(), 0);

  EXPECT_TRUE(cursor.goto_first_child());
  EXPECT_EQ(cursor.get_current_pos(), child1.get_current_pos());
  EXPECT_EQ(cursor.depth(), 1);

  EXPECT_TRUE(cursor.goto_first_child());
  EXPECT_EQ(cursor.get_current_pos(), grand.get_current_pos());
  EXPECT_TRUE(cursor.is_leaf());
  EXPECT_EQ(cursor.depth(), 2);

  EXPECT_TRUE(cursor.goto_parent());
  EXPECT_EQ(cursor.get_current_pos(), child1.get_current_pos());
  EXPECT_EQ(cursor.depth(), 1);

  EXPECT_TRUE(cursor.goto_next_sibling());
  EXPECT_EQ(cursor.get_current_pos(), child2.get_current_pos());
  EXPECT_EQ(cursor.depth(), 1);

  EXPECT_TRUE(cursor.goto_prev_sibling());
  EXPECT_EQ(cursor.get_current_pos(), child1.get_current_pos());
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

  const auto parent_root  = parent.add_root_node();
  const auto callsite     = parent.add_child(parent_root);
  const auto parent_other = parent.add_child(parent_root);
  const auto child_root   = child.add_root_node();
  const auto child_leaf   = child.add_child(child_root);

  parent.set_subnode(callsite, child_tid);

  auto cursor = forest->create_cursor(parent_tid);
  EXPECT_TRUE(cursor.is_root());
  EXPECT_EQ(cursor.get_current_tid(), parent_tid);
  EXPECT_EQ(cursor.get_current_pos(), parent_root.get_current_pos());
  EXPECT_EQ(cursor.depth(), 0);

  EXPECT_TRUE(cursor.goto_first_child());
  EXPECT_EQ(cursor.get_current_tid(), parent_tid);
  EXPECT_EQ(cursor.get_current_pos(), callsite.get_current_pos());
  EXPECT_EQ(cursor.depth(), 1);

  EXPECT_TRUE(cursor.goto_first_child());
  EXPECT_EQ(cursor.get_current_tid(), child_tid);
  EXPECT_EQ(cursor.get_current_pos(), child_root.get_current_pos());
  EXPECT_EQ(cursor.depth(), 2);

  EXPECT_TRUE(cursor.goto_first_child());
  EXPECT_EQ(cursor.get_current_tid(), child_tid);
  EXPECT_EQ(cursor.get_current_pos(), child_leaf.get_current_pos());
  EXPECT_TRUE(cursor.is_leaf());
  EXPECT_EQ(cursor.depth(), 3);

  EXPECT_TRUE(cursor.goto_parent());
  EXPECT_EQ(cursor.get_current_tid(), child_tid);
  EXPECT_EQ(cursor.get_current_pos(), child_root.get_current_pos());
  EXPECT_EQ(cursor.depth(), 2);

  EXPECT_TRUE(cursor.goto_parent());
  EXPECT_EQ(cursor.get_current_tid(), parent_tid);
  EXPECT_EQ(cursor.get_current_pos(), callsite.get_current_pos());
  EXPECT_EQ(cursor.depth(), 1);

  EXPECT_TRUE(cursor.goto_next_sibling());
  EXPECT_EQ(cursor.get_current_tid(), parent_tid);
  EXPECT_EQ(cursor.get_current_pos(), parent_other.get_current_pos());
  EXPECT_EQ(cursor.depth(), 1);
}

TEST(TreeWrappers, GetSubsReturnsDirectSubtreeCallsites) {
  auto forest = hhds::Forest::create();

  const auto top_decl   = create_declared_tree(forest, "top");
  const auto child_decl = create_declared_tree(forest, "child");

  auto& top   = *top_decl.tree;
  auto& child = *child_decl.tree;

  const auto root    = top.add_root_node();
  const auto call1   = top.add_child(root);
  const auto regular = top.add_child(root);
  const auto call2   = top.add_child(root);

  child.add_root_node();
  top.set_subnode(call1, child_decl.tio->get_tid());
  top.set_subnode(call2, child_decl.tio->get_tid());

  const auto subs = top.get_subs();
  ASSERT_EQ(subs.size(), 2U);
  EXPECT_EQ(subs[0], call1);
  EXPECT_EQ(subs[1], call2);
  EXPECT_NE(subs[0], regular);
}

TEST(TreeWrappers, PrintUsesTypeTableAndAttributes) {
  auto tree = hhds::Tree::create();

  const auto root  = tree->add_root_node();
  const auto child = tree->add_child(root);

  tree->set_type(root, 1);
  tree->set_type(child, 2);

  hhds::Tree::PrintOptions options;
  const hhds::Type_entry   type_table[] = {
    {"invalid", hhds::Statement_class::Node},
    {"add",     hhds::Statement_class::Node},
    {"literal", hhds::Statement_class::Node},
  };
  options.type_table = type_table;
  options.attributes = {
    {"type_id", [&tree](const hhds::Tree::Node_class& node) -> std::optional<std::string> {
      return std::to_string(tree->get_type(node));
    }},
  };

  std::ostringstream os;
  tree->print(os, options);

  EXPECT_EQ(os.str(), "tree {\n"
                      "  %8  = add     @(type_id=1)\n"
                      "  %16 = literal @(type_id=2)\n"
                      "}\n");
}

TEST(TreeWrappers, PrintReturnsString) {
  auto tree = hhds::Tree::create();
  tree->set_name("mytest");

  const auto root = tree->add_root_node();
  tree->set_type(root, 0);

  const auto result = tree->print();
  EXPECT_EQ(result, "mytest {\n"
                    "  %8 = type(0)\n"
                    "}\n");
}

TEST(TreeWrappers, PrintScopeTypes) {
  auto tree = hhds::Tree::create();
  tree->set_name("scoped");

  const auto root  = tree->add_root_node();
  const auto scope = tree->add_child(root);
  const auto leaf  = tree->add_child(scope);

  tree->set_type(root, 0);
  tree->set_type(scope, 1);
  tree->set_type(leaf, 0);

  const hhds::Type_entry type_table[] = {
    {"node",    hhds::Statement_class::Node},
    {"if_taken", hhds::Statement_class::Open_call},
  };

  hhds::Tree::PrintOptions options;
  options.type_table = type_table;

  const auto result = tree->print(options);
  EXPECT_EQ(result, "scoped {\n"
                    "  %8  = node\n"
                    "  %16 = if_taken {\n"
                    "    %24 = node\n"
                    "  }\n"
                    "}\n");
}
