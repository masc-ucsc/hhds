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
  main_tree.add_child(main_tree.get_root(), 3);  // No need to store child2
  
  sub_tree.add_child(sub_tree.get_root(), 11);
  sub_tree.add_child(sub_tree.get_root(), 12);
  
  // add subtree reference
  main_tree.add_subtree_ref(child1, sub_tree_ref);
  
  // test traversal with subtree following
  int count = 0;
  std::vector<int> visited_values;
  
  auto range = main_tree.pre_order(main_tree.get_root(), true);
  for (auto it = range.begin(); it != range.end(); ++it) {
    visited_values.push_back(it.get_data());
    count++;
  }
  
  // should visit: 1, 2, 10, 11, 12, 3
  I(count == 6, "Pre-order traversal with subtrees should visit 6 nodes");
  
  std::vector<int> expected = {1, 2, 10, 11, 12, 3};
  I(visited_values == expected, 
    "Pre-order traversal with subtrees should visit nodes in the correct order");
}

void test_complex_forest_operations() {
  hhds::Forest<int> forest;
  
  // create multiple trees
  auto main_tree_ref = forest.create_tree(1);
  auto sub_tree1_ref = forest.create_tree(1000);
  auto sub_tree2_ref = forest.create_tree(2000);
  auto sub_tree3_ref = forest.create_tree(3000);
  
  auto& main_tree = forest.get_tree(main_tree_ref);
  auto& sub_tree1 = forest.get_tree(sub_tree1_ref);
  auto& sub_tree2 = forest.get_tree(sub_tree2_ref);
  auto& sub_tree3 = forest.get_tree(sub_tree3_ref);
  
  // build tree structures
  std::vector<hhds::Tree_pos> main_nodes;
  std::vector<hhds::Tree_pos> sub1_nodes;
  std::vector<hhds::Tree_pos> sub2_nodes;
  std::vector<hhds::Tree_pos> sub3_nodes;
  
  // create a deep main tree with multiple branches
  main_nodes.push_back(main_tree.get_root());
  for (int i = 0; i < 100; ++i) {
    auto parent = main_nodes[i / 3];
    auto new_node = main_tree.add_child(parent, i + 2);
    main_nodes.push_back(new_node);
  }
  
  // create wide sub_tree1 with many siblings
  sub1_nodes.push_back(sub_tree1.get_root());
  auto sub1_parent = sub_tree1.get_root();
  for (int i = 0; i < 50; ++i) {
    auto new_node = sub_tree1.add_child(sub1_parent, 1100 + i);
    sub1_nodes.push_back(new_node);
  }
  
  // create balanced sub_tree2
  sub2_nodes.push_back(sub_tree2.get_root());
  for (int i = 0; i < 32; ++i) {
    auto parent = sub2_nodes[i];
    auto left = sub_tree2.add_child(parent, 2100 + i*2);
    auto right = sub_tree2.add_child(parent, 2100 + i*2 + 1);
    sub2_nodes.push_back(left);
    sub2_nodes.push_back(right);
  }
  
  // create deep chain in sub_tree3
  sub3_nodes.push_back(sub_tree3.get_root());
  auto current = sub_tree3.get_root();
  for (int i = 0; i < 100; ++i) {
    auto new_node = sub_tree3.add_child(current, 3100 + i);
    sub3_nodes.push_back(new_node);
    current = new_node;
  }
  
  // create complex reference patterns
  for (int i = 0; i < main_nodes.size(); i += 10) {
    main_tree.add_subtree_ref(main_nodes[i], sub_tree1_ref);
  }
  
  for (int i = 0; i < sub1_nodes.size(); i += 5) {
    sub_tree1.add_subtree_ref(sub1_nodes[i], sub_tree2_ref);
  }
  
  for (int i = 0; i < sub2_nodes.size(); i += 3) {
    sub_tree2.add_subtree_ref(sub2_nodes[i], sub_tree3_ref);
  }
  
  // test reference counting
  bool deleted = forest.delete_tree(sub_tree3_ref);
  I(!deleted, "Should not be able to delete heavily referenced tree");
  
  // test traversal with subtree references
  int node_count = 0;
  std::set<int> unique_values;
  
  auto range = main_tree.pre_order(main_tree.get_root(), true);
  for (auto it = range.begin(); it != range.end(); ++it) {
    node_count++;
    unique_values.insert(it.get_data());
    
    I(node_count <= 10000, "Possible infinite loop in traversal");
  }
  
  I(node_count > 500, "Should visit at least 500 nodes in large tree traversal");
  I(unique_values.size() > 200, "Should see at least 200 unique values");
  
  // tst bulk deletions
  for (int i = main_nodes.size() - 1; i >= 0; i -= 10) {
    if (i < main_nodes.size()) {
      main_tree.delete_leaf(main_nodes[i]);
    }
  }
  
  // verify reference counts
  deleted = forest.delete_tree(sub_tree1_ref);
  I(!deleted, "Should still not be able to delete referenced tree");
}

void test_edge_cases() {
  hhds::Forest<int> forest;
  
  // test creation of more trees
  std::vector<hhds::Tree_pos> tree_refs;
  for (int i = 0; i < 5; ++i) {
    auto ref = forest.create_tree(i);
    tree_refs.push_back(ref);
    
    auto& tree = forest.get_tree(ref);
    I(tree.get_data(tree.get_root()) == i, "Tree creation verification failed");
  }
  
  // create chain of references with two children per tree
  for (size_t i = 0; i < tree_refs.size() - 1; ++i) {
    auto& current_tree = forest.get_tree(tree_refs[i]);
    auto root = current_tree.get_root();
    I(current_tree.get_data(root) == i, "Root data verification failed");
    
    for (int j = 0; j < 2; ++j) {
      auto child = current_tree.add_child(root, i * 10 + j);
      I(current_tree.get_data(child) == i * 10 + j, "Child creation verification failed");
      I(current_tree.is_leaf(child), "New node should be a leaf");
      
      if (j == 0) {
        current_tree.add_subtree_ref(child, tree_refs[(i + 1) % tree_refs.size()]);
      }
    }
  }
  
  // create single circular reference
  auto& last_tree = forest.get_tree(tree_refs.back());
  auto last_child = last_tree.add_child(last_tree.get_root(), 100);
  I(last_tree.get_data(last_child) == 100, "Last child creation verification failed");
  last_tree.add_subtree_ref(last_child, tree_refs[0]);
  
  // add a few more leaf nodes to existing trees
  for (size_t i = 0; i < tree_refs.size(); i += 2) {  // Every other tree
    auto& tree = forest.get_tree(tree_refs[i]);
    tree.add_child(tree.get_root(), i * 20);
  }
  
  // test traversal
  int count = 0;
  std::set<int> unique_values;
  auto& first_tree = forest.get_tree(tree_refs[0]);
  
  for (auto it = first_tree.pre_order(first_tree.get_root(), true).begin(); 
       it != first_tree.pre_order(first_tree.get_root(), true).end(); ++it) {
    count++;
    unique_values.insert(it.get_data());
    I(count <= 32, "Traversal count exceeded expected maximum");
  }
  
  I(count > 5, "Should visit at least 5 nodes in traversal");
  I(unique_values.size() > 3, "Should see at least 3 unique values");
  
  // try to delete trees
  for (auto ref : tree_refs) {
    bool deleted = forest.delete_tree(ref);
    I(!deleted, "Should not be able to delete referenced trees");
  }
}

int main() {
  std::cout << "\nStarting forest correctness tests...\n\n";
  
  test_basic_forest_operations();
  std::cout << "Basic forest operations test passed\n";
  
  test_subtree_references();
  std::cout << "Subtree references test passed\n";
  
  test_tree_traversal_with_subtrees();
  std::cout << "Tree traversal with subtrees test passed\n";
  
  std::cout << "\nStarting large-scale tests...\n";
  
  test_complex_forest_operations();
  std::cout << "Complex forest operations test passed\n";
  
  test_edge_cases();
  std::cout << "Edge cases test passed\n";
  
  std::cout << "\nAll forest correctness tests passed successfully!\n";
  
  return 0;
}
