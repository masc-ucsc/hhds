#include <gtest/gtest.h>

#include <random>
#include <vector>

#include "tree.hpp"

#if __has_include("hhds/tests/tree_test_utils.hpp")
#include "hhds/tests/tree_test_utils.hpp"
#elif __has_include("tests/tree_test_utils.hpp")
#include "tests/tree_test_utils.hpp"
#else
namespace hhds_test {

using IntNode = hhds::Tree::Node_class;

inline void ensure_size(std::vector<int>& values, hhds::Tid tid) {
  if (tid >= static_cast<hhds::Tid>(values.size())) {
    values.resize(static_cast<size_t>(tid + 1));
  }
}

inline void set_value(std::vector<int>& values, IntNode node, int value) {
  ensure_size(values, node.get_debug_nid());
  values[static_cast<size_t>(node.get_debug_nid())] = value;
}

inline IntNode add_root(hhds::Tree& tree, std::vector<int>& values, int value) {
  const auto node = tree.add_root_node();
  set_value(values, node, value);
  return node;
}

inline IntNode add_child(hhds::Tree& tree, std::vector<int>& values, IntNode parent, int value) {
  const auto node = parent.add_child();
  set_value(values, node, value);
  return node;
}

inline int get_value(const std::vector<int>& values, IntNode node) { return values[static_cast<size_t>(node.get_debug_nid())]; }

inline void preorder_values(const hhds::Tree& tree, const std::vector<int>& values, std::vector<int>& out) {
  for (auto node : tree.pre_order()) {
    out.push_back(get_value(values, node));
  }
}

inline void postorder_values(hhds::Tree& tree, const std::vector<int>& values, std::vector<int>& out) {
  for (auto node : tree.post_order()) {
    out.push_back(get_value(values, node));
  }
}

}  // namespace hhds_test
#endif

int generate_random_int(std::default_random_engine& generator, int min, int max) {
  std::uniform_int_distribution<int> distribution(min, max);
  return distribution(generator);
}

void preorder_traversal_hhds(hhds::Tree& tree, const std::vector<int>& values, std::vector<int>& result) {
  hhds_test::preorder_values(tree, values, result);
}

void postorder_traversal_hhds(hhds::Tree& tree, const std::vector<int>& values, std::vector<int>& result) {
  hhds_test::postorder_values(tree, values, result);
}

TEST(TreeCorrectness, WideTreeTraversals) {
  std::default_random_engine generator(42);
  const int                  num_children = 10'000'000;

  auto             hhds_tree = hhds::Tree::create();
  std::vector<int> hhds_values;

  auto             data_to_add = generate_random_int(generator, 1, 100);
  auto             hhds_root   = hhds_test::add_root(*hhds_tree, hhds_values, data_to_add);
  std::vector<int> expected_preorder;
  std::vector<int> expected_postorder;
  expected_preorder.reserve(static_cast<size_t>(num_children) + 1);
  expected_postorder.reserve(static_cast<size_t>(num_children) + 1);
  expected_preorder.push_back(data_to_add);

  for (int i = 0; i < num_children; ++i) {
    data_to_add = generate_random_int(generator, 1, 100);
    hhds_test::add_child(*hhds_tree, hhds_values, hhds_root, data_to_add);
    expected_preorder.push_back(data_to_add);
    expected_postorder.push_back(data_to_add);
  }
  expected_postorder.push_back(hhds_test::get_value(hhds_values, hhds_root));

  std::vector<int> hhds_preorder, hhds_postorder;
  preorder_traversal_hhds(*hhds_tree, hhds_values, hhds_preorder);
  postorder_traversal_hhds(*hhds_tree, hhds_values, hhds_postorder);

  ASSERT_EQ(hhds_preorder.size(), expected_preorder.size());
  ASSERT_EQ(hhds_postorder.size(), expected_postorder.size());
  EXPECT_EQ(hhds_preorder, expected_preorder);
  EXPECT_EQ(hhds_postorder, expected_postorder);
}
