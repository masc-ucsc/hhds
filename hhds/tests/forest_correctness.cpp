#include <iostream>
#include <vector>
#include "tree.hpp"

template <typename T>
void print_vector(const std::vector<T>& vec, const std::string& name) {
  std::cout << name << ": ";
  for (const auto& val : vec) {
    std::cout << val << " ";
  }
  std::cout << "\n";
}

// preorder traversal
void preorder_traversal_hhds(hhds::tree<int>& tree, std::vector<int>& result) {
  for (const auto& node : tree.pre_order()) {
    result.push_back(tree[node]);
  }
}

// postorder traversal
void postorder_traversal_hhds(hhds::tree<int>& tree, std::vector<int>& result) {
  for (const auto& node : tree.post_order()) {
    result.push_back(tree[node]);
  }
}

int main() {
  hhds::tree<int> test_tree;

  // create root node
  auto root = test_tree.add_root(10);
  
  // add a few children to root
  auto c1 = test_tree.add_child(root, 20);
  auto c2 = test_tree.add_child(root, 30);
  auto c3 = test_tree.add_child(root, 40);

  // add children to one of the child nodes
  auto c2_1 = test_tree.add_child(c2, 50);
  auto c2_2 = test_tree.add_child(c2, 60);

  // perform preorder traversal
  std::vector<int> preorder_result;
  preorder_traversal_hhds(test_tree, preorder_result);
  
  // perform postorder traversal
  std::vector<int> postorder_result;
  postorder_traversal_hhds(test_tree, postorder_result);
  
  // print results
  print_vector(preorder_result, "Preorder");   // expected: 10 20 30 50 60 40
  print_vector(postorder_result, "Postorder"); // expected: 20 50 60 30 40 10

  // basic check
  if (preorder_result.size() == 6 && postorder_result.size() == 6) {
    std::cout << "Simple test completed successfully.\n";
  } else {
    std::cout << "Test failed: unexpected number of nodes in traversal.\n";
  }

  return 0;
}
