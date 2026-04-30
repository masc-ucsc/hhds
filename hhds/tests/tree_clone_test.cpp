// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <gtest/gtest.h>

#include <filesystem>
#include <unordered_map>
#include <vector>

#include "hhds/tree.hpp"

namespace clone_test_attrs {

struct loc_t {
  using value_type = int;
  using storage    = hhds::flat_storage;
};
inline constexpr loc_t loc{};

}  // namespace clone_test_attrs

namespace {

// Walk a tree pre-order and capture (depth, name, loc) for every node so two
// trees can be structurally compared without relying on a generic operator==.
struct NodeSnapshot {
  int                        depth = 0;
  std::string                name;
  bool                       has_loc = false;
  int                        loc     = 0;
};

void collect_pre_order(const hhds::Tree::Node_class& node, int depth, std::vector<NodeSnapshot>& out) {
  if (!node.is_valid()) {
    return;
  }
  NodeSnapshot snap;
  snap.depth = depth;
  if (node.attr(hhds::attrs::name).has()) {
    snap.name = std::string(node.attr(hhds::attrs::name).get());
  }
  if (node.attr(clone_test_attrs::loc).has()) {
    snap.has_loc = true;
    snap.loc     = node.attr(clone_test_attrs::loc).get();
  }
  out.push_back(std::move(snap));

  auto child = node.first_child();
  while (child.is_valid()) {
    collect_pre_order(child, depth + 1, out);
    child = child.next_sibling();
  }
}

bool snapshots_equal(const std::vector<NodeSnapshot>& a, const std::vector<NodeSnapshot>& b) {
  if (a.size() != b.size()) {
    return false;
  }
  for (size_t i = 0; i < a.size(); ++i) {
    if (a[i].depth != b[i].depth) {
      return false;
    }
    if (a[i].name != b[i].name) {
      return false;
    }
    if (a[i].has_loc != b[i].has_loc) {
      return false;
    }
    if (a[i].has_loc && a[i].loc != b[i].loc) {
      return false;
    }
  }
  return true;
}

}  // namespace

TEST(TreeClone, ProducesUnattachedDeepCopy) {
  hhds::register_attr_tag<clone_test_attrs::loc_t>("clone_test_attrs::loc");

  auto forest = hhds::Forest::create();
  auto tio    = forest->create_io("src");
  auto src    = tio->create_tree();

  auto root = src->add_root_node();
  root.attr(hhds::attrs::name).set("root");
  root.attr(clone_test_attrs::loc).set(1);

  auto a = root.add_child();
  a.attr(hhds::attrs::name).set("a");
  a.attr(clone_test_attrs::loc).set(2);

  auto b = root.add_child();
  b.attr(hhds::attrs::name).set("b");

  auto a1 = a.add_child();
  a1.attr(hhds::attrs::name).set("a1");
  a1.attr(clone_test_attrs::loc).set(42);

  auto copy = src->clone();
  ASSERT_NE(copy, nullptr);

  // Unattached: no Forest, no TreeIO, INVALID tid.
  EXPECT_EQ(copy->get_io(), nullptr);
  EXPECT_EQ(copy->get_tid(), hhds::INVALID);
  EXPECT_EQ(forest->find_tree("src"), src);
  EXPECT_NE(forest->find_tree("src"), copy);

  std::vector<NodeSnapshot> src_snap;
  std::vector<NodeSnapshot> copy_snap;
  collect_pre_order(src->get_root_node(), 0, src_snap);
  collect_pre_order(copy->get_root_node(), 0, copy_snap);

  EXPECT_TRUE(snapshots_equal(src_snap, copy_snap));
  EXPECT_GE(src_snap.size(), 4u);
}

TEST(TreeClone, MutationsAreIndependent) {
  auto forest = hhds::Forest::create();
  auto tio    = forest->create_io("src2");
  auto src    = tio->create_tree();

  auto root = src->add_root_node();
  root.attr(hhds::attrs::name).set("root");
  auto child = root.add_child();
  child.attr(hhds::attrs::name).set("child");

  auto copy = src->clone();
  ASSERT_NE(copy, nullptr);

  // Mutate the copy.
  auto copy_root = copy->get_root_node();
  ASSERT_TRUE(copy_root.is_valid());
  auto copy_extra = copy_root.add_child();
  copy_extra.attr(hhds::attrs::name).set("extra_in_copy");

  // Source is unchanged.
  std::vector<NodeSnapshot> src_snap;
  collect_pre_order(src->get_root_node(), 0, src_snap);
  ASSERT_EQ(src_snap.size(), 2u);
  EXPECT_EQ(src_snap[0].name, "root");
  EXPECT_EQ(src_snap[1].name, "child");
}

TEST(ForestCreateTreeTemp, IsUnattachedAndNotRegistered) {
  auto forest = hhds::Forest::create();

  auto temp = forest->create_tree_temp("scratch");
  ASSERT_NE(temp, nullptr);

  // Unattached: no IO, no tid.
  EXPECT_EQ(temp->get_io(), nullptr);
  EXPECT_EQ(temp->get_tid(), hhds::INVALID);

  // Not registered with the Forest.
  EXPECT_EQ(forest->find_io("scratch"), nullptr);
  EXPECT_EQ(forest->find_tree("scratch"), nullptr);

  // Name-hint stored as cosmetic name.
  EXPECT_EQ(temp->get_name(), "scratch");

  // Mutation works on a temp tree just like any other.
  auto root = temp->add_root_node();
  ASSERT_TRUE(root.is_valid());
  root.attr(hhds::attrs::name).set("temp_root");
  EXPECT_EQ(root.attr(hhds::attrs::name).get(), "temp_root");
}

TEST(ForestCreateTreeTemp, NoNameHintLeavesDefaultName) {
  auto forest = hhds::Forest::create();

  auto temp = forest->create_tree_temp();
  ASSERT_NE(temp, nullptr);
  EXPECT_EQ(temp->get_io(), nullptr);

  // No registration regardless.
  EXPECT_EQ(forest->find_io(""), nullptr);
}

TEST(ForestCreateTreeTemp, NotSerializedBySave) {
  namespace fs = std::filesystem;

  const std::string dir = "/tmp/hhds_temp_tree_persist";
  fs::remove_all(dir);

  {
    auto forest = hhds::Forest::create();

    auto tio  = forest->create_io("named");
    auto tree = tio->create_tree();
    (void)tree->add_root_node();

    auto temp = forest->create_tree_temp("temp_only");
    (void)temp->add_root_node();

    forest->save(dir);
  }

  // Reload — only the named slot should come back.
  auto forest2 = hhds::Forest::create();
  forest2->load(dir);
  EXPECT_NE(forest2->find_io("named"), nullptr);
  EXPECT_EQ(forest2->find_io("temp_only"), nullptr);

  fs::remove_all(dir);
}
