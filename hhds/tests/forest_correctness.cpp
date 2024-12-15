// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "hhds/tree.hpp"
#include "iassert.hpp"

#include <iostream>

void test_basic_forest_operations() {
  hhds::Forest<int> forest;
  
  // test creating trees
  auto tree1_ref = forest.create_tree(1);
  auto tree2_ref = forest.create_tree(2);
  
  // verify tree references are negative and different
  I(tree1_ref < 0, "Tree reference should be negative");
  I(tree2_ref < 0, "Tree reference should be negative");
  I(tree1_ref != tree2_ref, "Tree references should be different");

  // get trees and verify root values
  auto& tree1 = forest.get_tree(tree1_ref);
  auto& tree2 = forest.get_tree(tree2_ref);
  
  I(tree1.get_data(tree1.get_root()) == 1, "Tree1 root should have value 1");
  I(tree2.get_data(tree2.get_root()) == 2, "Tree2 root should have value 2");
}

void test_subtree_references() {
  hhds::Forest<int> forest;
  
  // create two trees
  auto main_tree_ref = forest.create_tree(1);
  auto sub_tree_ref = forest.create_tree(2);
  
  auto& main_tree = forest.get_tree(main_tree_ref);
  auto& sub_tree = forest.get_tree(sub_tree_ref);
  
  // add some nodes to both trees
  auto child1 = main_tree.add_child(main_tree.get_root(), 10);
  auto child2 = main_tree.add_child(main_tree.get_root(), 11);
  
  auto sub_child = sub_tree.add_child(sub_tree.get_root(), 20);
  
  // add subtree reference
  main_tree.add_subtree_ref(child1, sub_tree_ref);
  
  // verify reference counting - should return false and not delete
  bool deleted = forest.delete_tree(sub_tree_ref);
  I(!deleted, "Should not be able to delete tree with references");
  
  // tree should still be accessible
  auto& still_there = forest.get_tree(sub_tree_ref);
  I(still_there.get_data(still_there.get_root()) == 2, "Referenced tree should still exist");
  
  // remove reference by deleting referencing node
  main_tree.delete_leaf(child1);
  
  // now should be able to delete subtree
  deleted = forest.delete_tree(sub_tree_ref);
  I(deleted, "Should be able to delete tree with no references");
  
  // verify tree is deleted by checking if get_tree throws
  bool caught_exception = false;
  try {
    forest.get_tree(sub_tree_ref);
  } catch (const std::runtime_error&) {
    caught_exception = true;
  }
  I(caught_exception, "Should not be able to access deleted tree");
}

void test_tree_traversal_with_subtrees() {
  hhds::Forest<int> forest;
  
  // create main tree and subtree
  auto main_tree_ref = forest.create_tree(1);
  auto sub_tree_ref = forest.create_tree(10);
  
  auto& main_tree = forest.get_tree(main_tree_ref);
  auto& sub_tree = forest.get_tree(sub_tree_ref);
  
  // build trees
  auto child1 = main_tree.add_child(main_tree.get_root(), 2);
  auto child2 = main_tree.add_child(main_tree.get_root(), 3);
  
  sub_tree.add_child(sub_tree.get_root(), 11);
  sub_tree.add_child(sub_tree.get_root(), 12);
  
  // add subtree reference
  main_tree.add_subtree_ref(child1, sub_tree_ref);
  
  // test traversal with subtree following
  int count = 0;
  for (auto node : main_tree.pre_order(main_tree.get_root(), true)) {
    count++;
  }
  
  // should visit: 1, 2, 10, 11, 12, 3
  I(count == 6, "Pre-order traversal with subtrees should visit 6 nodes");
}

int main() {
  test_basic_forest_operations();
  std::cout << "Basic forest operations test passed\n";
  
  test_subtree_references();
  std::cout << "Subtree references test passed\n";
  
  test_tree_traversal_with_subtrees();
  std::cout << "Tree traversal with subtrees test passed\n";
  
  return 0;
}
