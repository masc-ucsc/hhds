// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// HHDS tree public-API contract tests.
//
// These tests mirror the tree examples in sample.md. They serve two purposes:
//   1. Freeze the public API surface that downstream users depend on.
//   2. Act as a short, runnable tutorial for new users.
//
// Only hhds public types (Forest, TreeIO, Tree, Tree::Node_class, attrs) are
// exercised. Internal types (Tree_pos, Tid, raw storage) are not touched.

#include <gtest/gtest.h>

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "hhds/attr.hpp"
#include "hhds/attrs/name.hpp"
#include "hhds/tree.hpp"

namespace contract_attrs {

// Flat attribute: one value per tree node, regardless of hierarchy view.
struct loc_t {
  using value_type = uint32_t;
  using storage    = hhds::flat_storage;
};
inline constexpr loc_t loc{};

}  // namespace contract_attrs

// Sample Example 6: tree basics, attributes, traversal.
TEST(TreeApiContract, BasicsAttributesAndTraversal) {
  auto forest = hhds::Forest::create();

  // Declare a tree (name required and immutable).
  auto tio = forest->create_io("parser");
  EXPECT_EQ(tio->get_name(), "parser");

  // Create the implementation body from the declaration.
  auto t = tio->create_tree();
  EXPECT_EQ(t->get_io(), tio);

  // Build structure — add_root_node and add_child return Node_class.
  using hhds::attrs::name;
  auto root = t->add_root_node();
  root.attr(name).set("program");
  root.set_type(1);

  auto lhs = root.add_child();
  lhs.attr(name).set("assign");

  auto rhs = root.add_child();
  rhs.attr(name).set("expr");

  auto leaf = lhs.add_child();
  leaf.attr(name).set("literal");

  // Pre-order starting at root: program, assign, literal, expr.
  std::vector<std::string> pre_names;
  for (auto node : root.pre_order_class()) {
    pre_names.push_back(std::string(node.attr(name).get()));
  }
  EXPECT_EQ(pre_names, (std::vector<std::string>{"program", "assign", "literal", "expr"}));

  // Starting pre-order from a non-root node walks the subtree rooted at that
  // node and then continues with later siblings of its ancestors, stopping at
  // the tree root. Starting from lhs visits lhs, its child, then rhs.
  std::vector<std::string> sub_names;
  for (auto node : lhs.pre_order_class()) {
    sub_names.push_back(std::string(node.attr(name).get()));
  }
  EXPECT_EQ(sub_names, (std::vector<std::string>{"assign", "literal", "expr"}));

  // Post-order: literal, assign, expr, program.
  std::vector<std::string> post_names;
  for (auto node : root.post_order_class()) {
    post_names.push_back(std::string(node.attr(name).get()));
  }
  EXPECT_EQ(post_names, (std::vector<std::string>{"literal", "assign", "expr", "program"}));

  // Sibling order from lhs: assign, expr.
  std::vector<std::string> sib_names;
  for (auto node : lhs.sibling_order()) {
    sib_names.push_back(std::string(node.attr(name).get()));
  }
  EXPECT_EQ(sib_names, (std::vector<std::string>{"assign", "expr"}));
}

// Sample Example 7 (tree deletion): deleting a node removes its entire subtree
// and all associated attributes.
TEST(TreeApiContract, SubtreeDeletionRemovesChildrenAndAttributes) {
  auto forest = hhds::Forest::create();
  auto tio    = forest->create_io("ast");
  auto t      = tio->create_tree();

  using hhds::attrs::name;

  auto root = t->add_root_node();
  root.attr(name).set("program");
  auto func = root.add_child();
  func.attr(name).set("func");
  auto body = func.add_child();
  body.attr(name).set("body");
  auto ret = func.add_child();
  ret.attr(name).set("return");
  auto expr = root.add_child();
  expr.attr(name).set("expr");

  // Delete the func subtree.
  func.del_node();

  EXPECT_TRUE(func.is_invalid());
  EXPECT_TRUE(body.is_invalid());
  EXPECT_TRUE(ret.is_invalid());

  // Siblings and parent survive.
  EXPECT_TRUE(root.is_valid());
  EXPECT_TRUE(expr.is_valid());

  // Iteration skips deleted nodes.
  std::vector<std::string> visited;
  for (auto node : root.pre_order_class()) {
    EXPECT_TRUE(node.is_valid());
    visited.push_back(std::string(node.attr(name).get()));
  }
  EXPECT_EQ(visited, (std::vector<std::string>{"program", "expr"}));

  // Sibling iteration on the survivor visits only itself.
  std::vector<std::string> siblings;
  for (auto node : expr.sibling_order()) {
    siblings.push_back(std::string(node.attr(name).get()));
  }
  EXPECT_EQ(siblings, (std::vector<std::string>{"expr"}));
}

// Sample Example 7: forest with multiple trees and subnode references,
// traversal within a single tree, navigation back to the declaration.
TEST(TreeApiContract, ForestHierarchySubnodeReferences) {
  auto forest = hhds::Forest::create();

  auto expr_tio = forest->create_io("common_expr");
  auto func_tio = forest->create_io("main_func");
  EXPECT_EQ(forest->find_io("common_expr"), expr_tio);
  EXPECT_EQ(forest->find_io("main_func"), func_tio);

  using hhds::attrs::name;
  using contract_attrs::loc;

  // Reusable expression subtree.
  auto expr  = expr_tio->create_tree();
  auto eroot = expr->add_root_node();
  eroot.attr(name).set("add");
  eroot.attr(loc).set(10);
  auto elhs = eroot.add_child();
  elhs.attr(name).set("var_a");
  elhs.attr(loc).set(10);
  auto erhs = eroot.add_child();
  erhs.attr(name).set("var_b");
  erhs.attr(loc).set(10);

  // Function body that references the expression via set_subnode.
  auto func  = func_tio->create_tree();
  auto froot = func->add_root_node();
  froot.attr(name).set("func_main");
  froot.attr(loc).set(1);

  auto assign = froot.add_child();
  assign.attr(name).set("assign");
  assign.attr(loc).set(10);

  auto call = froot.add_child();
  call.set_subnode(expr_tio);
  call.attr(name).set("inline_expr");
  call.attr(loc).set(20);

  // Traversal within this tree only (does not enter the subnode).
  std::vector<std::string> class_names;
  for (auto node : froot.pre_order_class()) {
    class_names.push_back(std::string(node.attr(name).get()));
  }
  EXPECT_EQ(class_names, (std::vector<std::string>{"func_main", "assign", "inline_expr"}));

  // Tree → TreeIO → back to Tree round-trip.
  EXPECT_EQ(func->get_io(), func_tio);
  EXPECT_EQ(func_tio->get_tree(), func);
}

// Sample Example 7 (clear semantics): attr_clear empties but keeps the map
// registered; Tree::clear drops body and all attribute maps; TreeIO::clear
// makes the declaration unreachable via find_io.
TEST(TreeApiContract, ClearAndAttrClearSemantics) {
  auto forest = hhds::Forest::create();
  auto tio    = forest->create_io("clearable");
  auto t      = tio->create_tree();

  using hhds::attrs::name;
  using contract_attrs::loc;

  auto root  = t->add_root_node();
  auto child = root.add_child();
  root.attr(name).set("program");
  child.attr(loc).set(10);

  // attr_clear: entries gone, map registration remains.
  t->attr_clear(loc);
  EXPECT_TRUE(t->has_attr(loc));
  EXPECT_FALSE(child.attr(loc).has());

  // Tree::clear: body gone, attribute maps dropped.
  t->clear();
  EXPECT_FALSE(t->has_attr(name));
  EXPECT_FALSE(t->has_attr(loc));
  EXPECT_TRUE(root.is_invalid());

  // Rebuild from scratch on the same declaration.
  auto root2 = t->add_root_node();
  root2.attr(name).set("mul");
  EXPECT_EQ(root2.attr(name).get(), "mul");

  // TreeIO::clear: declaration unreachable via find_io.
  tio->clear();
  EXPECT_EQ(forest->find_io("clearable"), nullptr);
}

// Sample Example 7 (persistence): save/load round-trip preserves node
// structure, types, and attribute values.
TEST(TreeApiContract, PersistenceRoundTrip) {
  namespace fs               = std::filesystem;
  const std::string test_dir = "/tmp/hhds_contract_tree";
  fs::remove_all(test_dir);

  hhds::register_attr_tag<contract_attrs::loc_t>("contract_attrs::loc");

  using hhds::attrs::name;
  using contract_attrs::loc;

  auto forest = hhds::Forest::create();
  auto tio    = forest->create_io("saved");
  auto t      = tio->create_tree();

  auto root = t->add_root_node();
  auto c1   = t->add_child(root);
  auto c2   = t->add_child(root);
  auto gc1  = t->add_child(c1);

  root.set_type(1);
  c1.set_type(2);
  c2.set_type(3);
  gc1.set_type(4);
  root.attr(name).set("program");
  gc1.attr(loc).set(99);

  t->save_body(test_dir);
  EXPECT_TRUE(fs::exists(fs::path(test_dir) / "body.bin"));

  // Load into a fresh tree attached to the same forest.
  auto tio2 = forest->create_io("restored");
  auto t2   = tio2->create_tree();
  t2->load_body(test_dir);

  // Types and attributes survived.
  auto loaded_root = t2->get_root_node();
  auto loaded_gc1  = t2->as_class(gc1.get_current_pos());
  EXPECT_EQ(t2->get_type(loaded_root), 1);
  EXPECT_EQ(loaded_root.attr(name).get(), "program");
  EXPECT_EQ(loaded_gc1.attr(loc).get(), 99u);

  fs::remove_all(test_dir);
}
