// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <iostream>
#include <set>
#include <stdexcept>
#include <vector>

#include "hhds/tree.hpp"
#include "iassert.hpp"

namespace {

hhds::Tree& create_rooted_tree(hhds::Forest& forest, hhds::Tree_pos& tree_ref) {
  tree_ref = forest.create_tree();
  auto& tree = forest.get_tree(tree_ref);
  tree.add_root();
  return tree;
}

}  // namespace

void test_basic_forest_operations() {
  hhds::Forest forest;

  hhds::Tree_pos tree1_ref;
  hhds::Tree_pos tree2_ref;
  auto&          tree1 = create_rooted_tree(forest, tree1_ref);
  auto&          tree2 = create_rooted_tree(forest, tree2_ref);

  I(tree1_ref < 0, "Tree reference should be negative");
  I(tree2_ref < 0, "Tree reference should be negative");
  I(tree1_ref != tree2_ref, "Tree references should be different");
  I(tree1.get_root() == hhds::ROOT, "Tree1 root should exist");
  I(tree2.get_root() == hhds::ROOT, "Tree2 root should exist");
}

void test_subtree_references() {
  hhds::Forest forest;

  hhds::Tree_pos main_tree_ref;
  hhds::Tree_pos sub_tree_ref;
  auto&          main_tree = create_rooted_tree(forest, main_tree_ref);
  auto&          sub_tree  = create_rooted_tree(forest, sub_tree_ref);

  auto child1 = main_tree.add_child(main_tree.get_root());
  main_tree.add_child(main_tree.get_root());
  sub_tree.add_child(sub_tree.get_root());

  main_tree.add_subtree_ref(child1, sub_tree_ref);

  bool deleted = forest.delete_tree(sub_tree_ref);
  I(!deleted, "Should not be able to delete tree with references");
  auto& still_there = forest.get_tree(sub_tree_ref);
  I(still_there.get_root() == hhds::ROOT, "Referenced tree should still exist");

  main_tree.delete_leaf(child1);

  deleted = forest.delete_tree(sub_tree_ref);
  I(deleted, "Should be able to delete tree with no references");

  bool caught_exception = false;
  try {
    forest.get_tree(sub_tree_ref);
  } catch (const std::runtime_error&) {
    caught_exception = true;
  }
  I(caught_exception, "Should not be able to access deleted tree");
}

void test_tree_traversal_with_subtrees() {
  hhds::Forest forest;

  hhds::Tree_pos main_tree_ref;
  hhds::Tree_pos sub_tree_ref;
  auto&          main_tree = create_rooted_tree(forest, main_tree_ref);
  auto&          sub_tree  = create_rooted_tree(forest, sub_tree_ref);

  auto child1 = main_tree.add_child(main_tree.get_root());
  main_tree.add_child(main_tree.get_root());
  sub_tree.add_child(sub_tree.get_root());
  sub_tree.add_child(sub_tree.get_root());

  main_tree.add_subtree_ref(child1, sub_tree_ref);

  int count = 0;
  for (auto it = main_tree.pre_order_with_subtrees(main_tree.get_root(), true).begin();
       it != main_tree.pre_order_with_subtrees(main_tree.get_root(), true).end();
       ++it) {
    ++count;
  }
  I(count == 6, "Pre-order traversal with subtrees should visit 6 nodes");

  int count_no_subtree = 0;
  for (auto it = main_tree.pre_order_with_subtrees(main_tree.get_root(), false).begin();
       it != main_tree.pre_order_with_subtrees(main_tree.get_root(), false).end();
       ++it) {
    ++count_no_subtree;
  }
  I(count_no_subtree == 3, "Traversal without subtree following should visit 3 nodes");
}

void test_cycle_traversal() {
  hhds::Forest forest;

  hhds::Tree_pos a_ref;
  hhds::Tree_pos b_ref;
  auto&          a = create_rooted_tree(forest, a_ref);
  auto&          b = create_rooted_tree(forest, b_ref);

  auto a_child_ref = a.add_child(a.get_root());
  auto b_child_ref = b.add_child(b.get_root());

  a.add_subtree_ref(a_child_ref, b_ref);
  b.add_subtree_ref(b_child_ref, a_ref);

  int count = 0;
  for (auto it = a.pre_order_with_subtrees(a.get_root(), true).begin();
       it != a.pre_order_with_subtrees(a.get_root(), true).end();
       ++it) {
    ++count;
  }

  I(count == 6, "Pre-order traversal with cyclic subtrees should visit 6 nodes");
}

void test_complex_forest_operations() {
  hhds::Forest forest;

  hhds::Tree_pos main_tree_ref;
  hhds::Tree_pos sub_tree1_ref;
  hhds::Tree_pos sub_tree2_ref;
  hhds::Tree_pos sub_tree3_ref;
  auto&          main_tree = create_rooted_tree(forest, main_tree_ref);
  auto&          sub_tree1 = create_rooted_tree(forest, sub_tree1_ref);
  auto&          sub_tree2 = create_rooted_tree(forest, sub_tree2_ref);
  auto&          sub_tree3 = create_rooted_tree(forest, sub_tree3_ref);

  std::vector<hhds::Tree_pos> main_nodes{main_tree.get_root()};
  std::vector<hhds::Tree_pos> sub1_nodes{sub_tree1.get_root()};
  std::vector<hhds::Tree_pos> sub2_nodes{sub_tree2.get_root()};
  std::vector<hhds::Tree_pos> sub3_nodes{sub_tree3.get_root()};

  for (int i = 0; i < 100; ++i) {
    auto parent = main_nodes[i / 3];
    main_nodes.push_back(main_tree.add_child(parent));
  }

  auto sub1_parent = sub_tree1.get_root();
  for (int i = 0; i < 50; ++i) {
    sub1_nodes.push_back(sub_tree1.add_child(sub1_parent));
  }

  for (int i = 0; i < 32; ++i) {
    auto parent = sub2_nodes[i];
    sub2_nodes.push_back(sub_tree2.add_child(parent));
    sub2_nodes.push_back(sub_tree2.add_child(parent));
  }

  auto current = sub_tree3.get_root();
  for (int i = 0; i < 100; ++i) {
    auto new_node = sub_tree3.add_child(current);
    sub3_nodes.push_back(new_node);
    current = new_node;
  }

  main_tree.add_subtree_ref(main_nodes[0], sub_tree1_ref);
  sub_tree1.add_subtree_ref(sub1_nodes[0], sub_tree2_ref);
  sub_tree2.add_subtree_ref(sub2_nodes[0], sub_tree3_ref);

  bool deleted = forest.delete_tree(sub_tree3_ref);
  I(!deleted, "Should not be able to delete heavily referenced tree");

  int node_count = 0;
  for (auto it = main_tree.pre_order_with_subtrees(main_tree.get_root(), true).begin();
       it != main_tree.pre_order_with_subtrees(main_tree.get_root(), true).end();
       ++it) {
    ++node_count;
    I(node_count <= 10000, "Possible infinite loop in traversal");
  }

  I(node_count >= 318, "Should visit the large structural forest");
}

void test_tombstone_deletion() {
  hhds::Forest forest;

  hhds::Tree_pos t1;
  hhds::Tree_pos t2;
  hhds::Tree_pos t3;
  create_rooted_tree(forest, t1);
  create_rooted_tree(forest, t2);
  create_rooted_tree(forest, t3);

  I(t1 == -1, "Expected t1 == -1");
  I(t2 == -2, "Expected t2 == -2");
  I(t3 == -3, "Expected t3 == -3");

  I(forest.get_tree(t2).get_root() == hhds::ROOT, "Expected tree 2 to exist");
  forest.delete_tree(t2);

  bool caught_exception = false;
  try {
    forest.get_tree(t2);
  } catch (const std::runtime_error&) {
    caught_exception = true;
  }
  I(caught_exception, "Should not be able to access deleted tree");

  hhds::Tree_pos t4;
  create_rooted_tree(forest, t4);
  I(t4 == -4, "Expected t4 == -4");
}

int main() {
  std::cout << "\n>>> Starting forest correctness tests\n";
  test_basic_forest_operations();
  std::cout << "Basic forest operations test passed\n";

  std::cout << "\n>>> Starting subtree references test\n";
  test_subtree_references();
  std::cout << "Subtree references test passed\n";

  std::cout << "\n>>> Starting tree traversal with subtrees test\n";
  test_tree_traversal_with_subtrees();
  std::cout << "Tree traversal with subtrees test passed\n";

  std::cout << "\n>>> Starting large-scale test\n";
  test_complex_forest_operations();
  std::cout << "Complex forest operations test passed\n";

  std::cout << "\n>>> Starting cycle traversal test\n";
  test_cycle_traversal();
  std::cout << "Cycle traversal test passed\n";

  std::cout << "\n>>> Starting tombstone deletion test\n";
  test_tombstone_deletion();
  std::cout << "Tombstone deletion test passed\n";

  std::cout << "\nAll forest correctness tests passed successfully!\n";
  return 0;
}
