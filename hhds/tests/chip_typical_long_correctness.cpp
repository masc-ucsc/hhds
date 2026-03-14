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

inline void postorder_values(hhds::Tree& tree, const std::vector<int>& values, std::vector<int>& out) {
  for (auto node : tree.post_order()) {
    out.push_back(get_value(values, node));
  }
}

}  // namespace hhds_test
#endif

std::vector<std::vector<int>> hhds_sibling_data;
std::vector<std::vector<int>> lh_sibling_data;

int generate_random_int(std::default_random_engine& generator, int min, int max) {
  std::uniform_int_distribution<int> distribution(min, max);
  return distribution(generator);
}

void preorder_traversal_hhds(hhds::Tree& tree, const std::vector<int>& values, std::vector<int>& result) {
  hhds_sibling_data.clear();
  for (auto node : tree.pre_order()) {
    result.push_back(hhds_test::get_value(values, node));
    std::vector<int> sibling_data;
    for (auto sibling : tree.sibling_order(node)) {
      sibling_data.push_back(hhds_test::get_value(values, sibling));
    }
    hhds_sibling_data.push_back(sibling_data);
  }
}

void postorder_traversal_hhds(hhds::Tree& tree, const std::vector<int>& values, std::vector<int>& result) {
  hhds_test::postorder_values(tree, values, result);
}

void preorder_traversal_lhtree(lh::tree<int>& tree, std::vector<int>& result) {
  lh_sibling_data.clear();
  auto                                                 root_index = lh::Tree_index(0, 0);
  typename lh::tree<int>::Tree_depth_preorder_iterator it(root_index, &tree);
  for (auto node_it = it.begin(); node_it != it.end(); ++node_it) {
    result.push_back(tree.get_data(*node_it));

    std::vector<int>                     sibling_data;
    lh::tree<int>::Tree_sibling_iterator sib_it(*node_it, &tree);
    for (auto sib = sib_it.begin(); sib != sib_it.end(); ++sib) {
      sibling_data.push_back(tree.get_data(*sib));
    }
    lh_sibling_data.push_back(sibling_data);
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

void test_chip_tree() {
  std::default_random_engine generator(42);

  hhds::Tree      hhds_tree;
  std::vector<int> hhds_values;
  lh::tree<int>   lh_tree;

  auto hhds_root = hhds_test::add_root(hhds_tree, hhds_values, 0);
  lh_tree.set_root(0);

  std::vector<hhds::Tree::Node_class> hhds_current_level{hhds_root};
  std::vector<lh::Tree_index>         lh_current_level{lh::Tree_index(0, 0)};

  int id = 1;
  for (int depth = 0; depth < 6; ++depth) {
    std::vector<hhds::Tree::Node_class> hhds_next_level;
    std::vector<lh::Tree_index>         lh_next_level;
    std::vector<std::vector<int>>       level_data;

    for (auto hhds_node : hhds_current_level) {
      const int        num_children = generate_random_int(generator, 2, 20);
      std::vector<int> children_data;

      for (int i = 0; i < num_children; ++i) {
        const int data  = id++;
        const auto added = hhds_test::add_child(hhds_tree, hhds_values, hhds_node, data);

        hhds_next_level.push_back(added);
        children_data.push_back(data);
      }
      level_data.push_back(children_data);
    }

    for (size_t i = 0; i < lh_current_level.size(); ++i) {
      for (int data : level_data[i]) {
        lh_next_level.push_back(lh_tree.add_child(lh_current_level[i], data));
      }
    }

    hhds_current_level = hhds_next_level;
    lh_current_level   = lh_next_level;
  }

  std::vector<int> hhds_preorder, lh_preorder, hhds_postorder, lh_postorder;
  preorder_traversal_hhds(hhds_tree, hhds_values, hhds_preorder);
  preorder_traversal_lhtree(lh_tree, lh_preorder);
  postorder_traversal_hhds(hhds_tree, hhds_values, hhds_postorder);
  postorder_traversal_lhtree(lh_tree, lh_postorder);

  bool sib_valid = true;
  for (size_t i = 0; i < hhds_sibling_data.size(); ++i) {
    if (i >= lh_sibling_data.size() || !compare_vectors(hhds_sibling_data[i], lh_sibling_data[i])) {
      std::cerr << "Sibling data mismatch in test_chip_tree" << std::endl;
      sib_valid = false;
    }
  }

  if (sib_valid) {
    std::cout << "Sibling data match in test_chip_tree" << std::endl;
  }

  if (!compare_vectors(hhds_preorder, lh_preorder)) {
    std::cerr << "Preorder traversal mismatch in test_chip_tree" << std::endl;
  } else {
    std::cout << "Preorder traversal match in test_chip_tree" << std::endl;
  }
  if (!compare_vectors(hhds_postorder, lh_postorder)) {
    std::cerr << "Postorder traversal mismatch in test_chip_tree" << std::endl;
  } else {
    std::cout << "Postorder traversal match in test_chip_tree" << std::endl;
  }
}

int main() {
  test_chip_tree();
  return 0;
}
