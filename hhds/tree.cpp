// test.cpp
#include "tree.hpp"

static_assert(sizeof(hhds::Tree_pointers)==64);

#if 0
int main() {
  using namespace hhds;

  // Create a tree of integers
  tree<int> my_tree;

  // Add root node with data 10
  Tree_pos root = my_tree.add_root(10);
  std::cout << "Root node added with data: " << my_tree.get_data(root) << std::endl;

  // Add children to root node
  Tree_pos child1 = my_tree.add_child(root, 20);
  std::cout << "Child 1 added with data: " << 20 << std::endl;

  Tree_pos child2 = my_tree.add_child(root, 30);
  Tree_pos child3 = my_tree.add_child(root, 40);
  std::cout << "Child 3 added with data: " << my_tree.get_data(child3) << std::endl;

  std::cout << "Indices of children: " << child1 << " " << child2 << " " << child3 << std::endl;
  // Check if child1 is the first child
  bool is_first = my_tree.is_first_child(child1);
  std::cout << "Is child1 the first child? " << (is_first ? "Yes" : "No") << std::endl;

  // Check if child3 is the last child
  bool is_last = my_tree.is_last_child(child3);
  std::cout << "Is child3 the last child? " << (is_last ? "Yes" : "No") << std::endl;

  // Get the next sibling of child1
  std::cout << "gottem ";
  Tree_pos next_sibling = my_tree.get_sibling_next(child1);
  std::cout << "gottem ";
  std::cout << "Next sibling of child1 has data: " << my_tree.get_data(next_sibling) << std::endl;

  // Get the previous sibling of child3
  Tree_pos prev_sibling = my_tree.get_sibling_prev(child3);
  std::cout << "Previous sibling of child3 has data: " << my_tree.get_data(prev_sibling) << std::endl;

  // Get the first child of root
  Tree_pos first_child = my_tree.get_first_child(root);
  std::cout << "First child of root has data: " << my_tree.get_data(first_child) << std::endl;

  // Get the last child of root
  Tree_pos last_child = my_tree.get_last_child(root);
  std::cout << "Last child of root has data: " << my_tree.get_data(last_child) << std::endl;

  return 0;
}
#endif
