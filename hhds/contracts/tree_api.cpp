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
#include <unordered_map>
#include <unordered_set>
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

  // Starting pre-order from a non-root node walks only the subtree rooted at
  // that node.
  std::vector<std::string> sub_names;
  for (auto node : lhs.pre_order_class()) {
    sub_names.push_back(std::string(node.attr(name).get()));
  }
  EXPECT_EQ(sub_names, (std::vector<std::string>{"assign", "literal"}));

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

  using contract_attrs::loc;
  using hhds::attrs::name;

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

  using contract_attrs::loc;
  using hhds::attrs::name;

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

  using contract_attrs::loc;
  using hhds::attrs::name;

  auto forest = hhds::Forest::create();
  auto tio    = forest->create_io("saved");
  auto t      = tio->create_tree();

  auto root = t->add_root_node();
  auto c1   = root.add_child();
  auto c2   = root.add_child();
  auto gc1  = c1.add_child();

  root.set_type(1);
  c1.set_type(2);
  c2.set_type(3);
  gc1.set_type(4);
  root.attr(name).set("program");
  gc1.attr(loc).set(99);

  const auto gc1_pos = gc1.get_current_pos();

  forest->save(test_dir);
  EXPECT_TRUE(fs::exists(fs::path(test_dir) / "forest.txt"));
  EXPECT_TRUE(fs::exists(fs::path(test_dir) / "tree_0" / "body.bin"));

  // Load into a fresh forest.
  auto forest2 = hhds::Forest::create();
  forest2->load(test_dir);
  auto tio2 = forest2->find_io("saved");
  ASSERT_NE(tio2, nullptr);
  auto t2 = tio2->get_tree();
  ASSERT_NE(t2, nullptr);

  // Types and attributes survived.
  auto loaded_root = t2->get_root_node();
  auto loaded_gc1  = t2->as_class(gc1_pos);
  EXPECT_EQ(t2->get_type(loaded_root), 1);
  EXPECT_EQ(loaded_root.attr(name).get(), "program");
  EXPECT_EQ(loaded_gc1.attr(loc).get(), 99u);

  fs::remove_all(test_dir);
}

// Fixture for index-scope tests: the canonical "top tree that instantiates a
// shared bottom subtree twice" shape. Mirrors the IndexContract fixture for
// graphs in contracts/attr.cpp, but with trees.
//
//   top
//   ├── inst1 -> subnode(bottom)
//   └── inst2 -> subnode(bottom)
//
//   bottom
//   └── leaf                  (single node inside shared body)
namespace {

struct TreeIndexFixture {
  std::shared_ptr<hhds::Forest> forest = hhds::Forest::create();
  std::shared_ptr<hhds::TreeIO> bottom_io;
  std::shared_ptr<hhds::Tree>   bottom;
  hhds::Tree::Node_class        bottom_root;
  hhds::Tree::Node_class        bottom_leaf;

  std::shared_ptr<hhds::TreeIO> top_io;
  std::shared_ptr<hhds::Tree>   top;
  hhds::Tree::Node_class        top_root;
  hhds::Tree::Node_class        inst1;
  hhds::Tree::Node_class        inst2;

  TreeIndexFixture() {
    bottom_io   = forest->create_io("bottom");
    bottom      = bottom_io->create_tree();
    bottom_root = bottom->add_root_node();
    bottom_root.attr(hhds::attrs::name).set("bottom_root");
    bottom_leaf = bottom_root.add_child();
    bottom_leaf.attr(hhds::attrs::name).set("bottom_leaf");

    top_io   = forest->create_io("top");
    top      = top_io->create_tree();
    top_root = top->add_root_node();
    top_root.attr(hhds::attrs::name).set("top_root");
    inst1 = top_root.add_child();
    inst1.attr(hhds::attrs::name).set("inst1");
    inst1.set_subnode(bottom_io);
    inst2 = top_root.add_child();
    inst2.attr(hhds::attrs::name).set("inst2");
    inst2.set_subnode(bottom_io);
  }
};

}  // namespace

// pre_order_class stays inside the top tree body. The handles it yields have
// Class context, and the only index that is valid at that level is
// Tree_class_index (scoped to this single body).
TEST(TreeIndexContract, ClassOrderStaysInTopBody) {
  TreeIndexFixture f;

  std::vector<std::string>                                visited;
  std::unordered_map<hhds::Tree_class_index, std::string> by_class;
  for (auto node : f.top_root.pre_order_class()) {
    EXPECT_TRUE(node.is_class());
    visited.push_back(std::string(node.attr(hhds::attrs::name).get()));
    by_class[node.get_class_index()] = std::string(node.attr(hhds::attrs::name).get());
  }
  EXPECT_EQ(visited, (std::vector<std::string>{"top_root", "inst1", "inst2"}));
  EXPECT_EQ(by_class.size(), 3u);
}

// pre_order_flat crosses subnode references, but enters each unique subtree
// body exactly once. Both inst1 and inst2 reference "bottom" via set_subnode,
// so bottom is visited once total. Handles carry Flat context so
// get_flat_index() is the canonical map key; every handle for the same
// (body, pos) collapses to the same Tree_flat_index.
TEST(TreeIndexContract, FlatOrderEntersSharedSubtreeOnce) {
  TreeIndexFixture f;

  std::vector<std::string>                               visited;
  std::unordered_set<hhds::Tree_flat_index>              bottom_keys;
  std::unordered_map<hhds::Tree_flat_index, std::string> by_flat;
  for (auto node : f.top->pre_order_flat()) {
    EXPECT_TRUE(node.is_flat());
    visited.push_back(std::string(node.attr(hhds::attrs::name).get()));
    if (node.get_current_tid() == f.bottom->get_tid()) {
      bottom_keys.insert(node.get_flat_index());
    }
    by_flat[node.get_flat_index()] = std::string(node.attr(hhds::attrs::name).get());
  }
  // Expected order: top_root, inst1, enter bottom once (bottom_root, bottom_leaf), inst2
  EXPECT_EQ(visited, (std::vector<std::string>{"top_root", "inst1", "bottom_root", "bottom_leaf", "inst2"}));
  // Both bottom nodes land under bottom's tid exactly once.
  EXPECT_EQ(bottom_keys.size(), 2u);
  // 3 top nodes + 2 bottom nodes = 5 distinct flat keys.
  EXPECT_EQ(by_flat.size(), 5u);
}

// pre_order_hier enters the shared body once per instantiation. Two
// traversals of bottom happen (via inst1 and via inst2), and the per-visit
// Tree_hier_index distinguishes them — while Tree_flat_index collapses them.
TEST(TreeIndexContract, HierOrderDistinguishesInstantiations) {
  TreeIndexFixture f;

  std::vector<hhds::Tree::Node_class>                    bottom_leaf_visits;
  std::unordered_map<hhds::Tree_hier_index, std::string> by_hier;
  std::vector<std::string>                               visited;
  for (auto node : f.top->pre_order_hier()) {
    EXPECT_TRUE(node.is_hier());
    visited.push_back(std::string(node.attr(hhds::attrs::name).get()));
    by_hier[node.get_hier_index()] = std::string(node.attr(hhds::attrs::name).get());
    if (node.get_current_tid() == f.bottom->get_tid() && node.attr(hhds::attrs::name).get() == "bottom_leaf") {
      bottom_leaf_visits.push_back(node);
    }
  }
  // bottom subtree is entered twice, once per inst.
  EXPECT_EQ(visited,
            (std::vector<std::string>{"top_root", "inst1", "bottom_root", "bottom_leaf", "inst2", "bottom_root", "bottom_leaf"}));
  ASSERT_EQ(bottom_leaf_visits.size(), 2u);

  // Per-instance hier keys differ, even though class/flat keys collapse.
  EXPECT_EQ(bottom_leaf_visits[0].get_class_index(), bottom_leaf_visits[1].get_class_index());
  EXPECT_EQ(bottom_leaf_visits[0].get_flat_index(), bottom_leaf_visits[1].get_flat_index());
  EXPECT_NE(bottom_leaf_visits[0].get_hier_index(), bottom_leaf_visits[1].get_hier_index());

  // 3 top nodes + 2 bottom nodes * 2 instantiations = 7 distinct hier keys.
  EXPECT_EQ(by_hier.size(), 7u);
}

// post_order_flat / post_order_hier — children first, then parent. Used when
// a bottom-up rewrite must finish the subtree before touching the container
// node. Same dedup rules as the pre-order variants.
TEST(TreeIndexContract, PostOrderFlatAndHierMirrorPreOrder) {
  TreeIndexFixture f;

  std::vector<std::string> flat_visited;
  for (auto node : f.top->post_order_flat()) {
    EXPECT_TRUE(node.is_flat());
    flat_visited.push_back(std::string(node.attr(hhds::attrs::name).get()));
  }
  // bottom entered under inst1, inst2 not re-entered.
  EXPECT_EQ(flat_visited, (std::vector<std::string>{"bottom_leaf", "bottom_root", "inst1", "inst2", "top_root"}));

  std::vector<std::string> hier_visited;
  for (auto node : f.top->post_order_hier()) {
    EXPECT_TRUE(node.is_hier());
    hier_visited.push_back(std::string(node.attr(hhds::attrs::name).get()));
  }
  // bottom entered per-instance, hier_pos differs across the two visits.
  EXPECT_EQ(hier_visited,
            (std::vector<std::string>{"bottom_leaf", "bottom_root", "inst1", "bottom_leaf", "bottom_root", "inst2", "top_root"}));
}

// get_hier_index from a Class-context handle (Tree::Node_class produced by
// in-body iteration) is a contract violation — release builds skip the
// assert but the handle has no expansion tree. Documented here as a
// negative example; not exercised at runtime.
//
//   for (auto n : f.top->pre_order_class()) n.get_hier_index();  // aborts
