// test.cpp
#include "tree.hpp"
#include <iostream>
#include <stdexcept>

int main() {
    // Create a tree of integers
    hhds::tree<int> my_tree;

    // Add root node with data 10
    hhds::Tree_pos root = my_tree.add_root(10);
    std::cout << "Root node added with data: " << my_tree.get_data(root) << std::endl;

    // Add initial children to root node
    hhds::Tree_pos child1 = my_tree.add_child(root, 20);
    hhds::Tree_pos child2 = my_tree.add_child(root, 30);
    hhds::Tree_pos child3 = my_tree.add_child(root, 40);

    std::cout << "Initial children added to root." << std::endl;

    // Add a million children to root
    const int num_children = 18;
    for (int i = 0; i < num_children; ++i) {
        my_tree.add_child(root, i);
    }
    std::cout << "Added " << num_children << " children to the root." << std::endl;
    my_tree.print_tree();

    // // Verify the structure of the tree by checking a few nodes
    // try {
    //     std::cout << "Root node data: " << my_tree.get_data(root) << std::endl;
    //     std::cout << "First child of root data: " << my_tree.get_data(my_tree.get_first_child(root)) << std::endl;
    //     std::cout << "Last child of root data: " << my_tree.get_data(my_tree.get_last_child(root)) << std::endl;
    // } catch (const std::out_of_range& e) {
    //     std::cerr << "Error: " << e.what() << std::endl;
    // }

    // Find child3 using next_sibling calls
    hhds::Tree_pos current_sibling = my_tree.get_first_child(root);
    std::cout << "First child of root: " << my_tree.get_data(current_sibling) << " at " << current_sibling << std::endl;
    for (int i = 1; i < 3; ++i) {
        break;
        current_sibling = my_tree.get_sibling_next(current_sibling);
    }


    // Add a million children to child3 of root
    // for (int i = 0; i < num_children; ++i) {
    //     my_tree.add_child(current_sibling, i + num_children);
    // }
    // std::cout << "Added " << num_children << " children to child3 of the root." << std::endl;

    // // Verify the structure of the tree by checking a few nodes
    // try {
    //     std::cout << "Child3 data: " << my_tree.get_data(current_sibling) << std::endl;
    //     std::cout << "First child of child3 data: " << my_tree.get_data(my_tree.get_first_child(current_sibling)) << std::endl;
    //     std::cout << "Last child of child3 data: " << my_tree.get_data(my_tree.get_last_child(current_sibling)) << std::endl;
    // } catch (const std::out_of_range& e) {
    //     std::cerr << "Error: " << e.what() << std::endl;
    // }

    // // Add a million children to child2 of root
    // current_sibling = my_tree.get_first_child(root);
    // current_sibling = my_tree.get_sibling_next(current_sibling);

    // for (int i = 0; i < num_children; ++i) {
    //     my_tree.add_child(current_sibling, i + 2 * num_children);
    // }
    // std::cout << "Added " << num_children << " children to child2 of the root." << std::endl;

    // // Verify the structure of the tree by checking a few nodes
    // try {
    //     std::cout << "Child2 data: " << my_tree.get_data(current_sibling) << std::endl;
    //     std::cout << "First child of child2 data: " << my_tree.get_data(my_tree.get_first_child(current_sibling)) << std::endl;
    //     std::cout << "Last child of child2 data: " << my_tree.get_data(my_tree.get_last_child(current_sibling)) << std::endl;
    // } catch (const std::out_of_range& e) {
    //     std::cerr << "Error: " << e.what() << std::endl;
    // }

    return 0;
}
