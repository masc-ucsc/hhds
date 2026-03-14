#include <iostream>
#include <random>
#include <vector>

#include "lhtree.hpp"
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
  ensure_size(values, node.get_current_pos());
  values[static_cast<size_t>(node.get_current_pos())] = value;
}

inline IntNode add_root(hhds::Tree& tree, std::vector<int>& values, int value) {
  const auto node = tree.add_root_node();
  set_value(values, node, value);
  return node;
}

inline IntNode add_child(hhds::Tree& tree, std::vector<int>& values, IntNode parent, int value) {
  const auto node = tree.add_child(parent);
  set_value(values, node, value);
  return node;
}

inline int get_value(const std::vector<int>& values, IntNode node) {
  return values[static_cast<size_t>(node.get_current_pos())];
}

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

void preorder_traversal_lhtree(lh::tree<int>& tree, std::vector<int>& result) {
  auto                                                 root_index = lh::Tree_index(0, 0);
  typename lh::tree<int>::Tree_depth_preorder_iterator it(root_index, &tree);
  for (auto node_it = it.begin(); node_it != it.end(); ++node_it) {
    result.push_back(tree.get_data(*node_it));
  }
}

void postorder_traversal_lhtree(lh::tree<int>& tree, std::vector<int>& result) {
  auto                                                  root_index = lh::Tree_index(0, 0);
  typename lh::tree<int>::Tree_depth_postorder_iterator it(root_index, &tree);
  for (auto node_it = it.begin(); node_it != it.end(); ++node_it) {
    result.push_back(tree.get_data(*node_it));
  }
}

template <typename T>
bool compare_vectors(const std::vector<T>& vec1, const std::vector<T>& vec2) {
  return vec1 == vec2;
}

void test_wide_tree() {
  std::default_random_engine generator(42);
  const int                  num_children = 10'000'000;

  hhds::Tree      hhds_tree;
  std::vector<int> hhds_values;
  lh::tree<int>   lh_tree;

  auto data_to_add = generate_random_int(generator, 1, 100);
  auto hhds_root   = hhds_test::add_root(hhds_tree, hhds_values, data_to_add);
  lh_tree.set_root(data_to_add);
  lh::Tree_index lh_root(0, 0);

  for (int i = 0; i < num_children; ++i) {
    data_to_add = generate_random_int(generator, 1, 100);
    hhds_test::add_child(hhds_tree, hhds_values, hhds_root, data_to_add);
    lh_tree.add_child(lh_root, data_to_add);
  }

  std::vector<int> hhds_preorder, lh_preorder, hhds_postorder, lh_postorder;
  preorder_traversal_hhds(hhds_tree, hhds_values, hhds_preorder);
  preorder_traversal_lhtree(lh_tree, lh_preorder);
  postorder_traversal_hhds(hhds_tree, hhds_values, hhds_postorder);
  postorder_traversal_lhtree(lh_tree, lh_postorder);

  if (!compare_vectors(hhds_preorder, lh_preorder)) {
    std::cout << "Preorder traversal mismatch in test_wide_tree" << std::endl;
  } else {
    std::cout << "Preorder traversal match!" << std::endl;
  }
  if (!compare_vectors(hhds_postorder, lh_postorder)) {
    std::cout << "Postorder traversal mismatch in test_wide_tree" << std::endl;
  } else {
    std::cout << "Postorder traversal match!" << std::endl;
  }
}

int main() {
  test_wide_tree();
  return 0;
}
