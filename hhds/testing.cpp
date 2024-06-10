// main.cpp
// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <iostream>
#include "tree.hpp" // Ensure this path is correct according to your project structure

int main() {
    using namespace hhds;

    // Creating a tree with integer data
    tree<int> my_tree;

    // Example usage (assuming the tree has been populated appropriately)
    Tree_pos root_index = ROOT;

    try {
        Tree_pos last_child = my_tree.get_last_child(root_index);
        if (last_child != INVALID) {
            std::cout << "Last child of root has index: " << last_child << std::endl;
        } else {
            std::cout << "Root has no children." << std::endl;
        }

        Tree_pos first_child = my_tree.get_first_child(root_index);
        if (first_child != INVALID) {
            std::cout << "First child of root has index: " << first_child << std::endl;
        } else {
            std::cout << "Root has no children." << std::endl;
        }

        bool is_last = my_tree.is_last_child(first_child);
        std::cout << "Is the first child also the last child of root? " << (is_last ? "Yes" : "No") << std::endl;

        Tree_pos next_sibling = my_tree.get_sibling_next(first_child);
        if (next_sibling != INVALID) {
            std::cout << "Next sibling of first child has index: " << next_sibling << std::endl;
        } else {
            std::cout << "First child has no next sibling." << std::endl;
        }

        Tree_pos prev_sibling = my_tree.get_sibling_prev(next_sibling);
        if (prev_sibling != INVALID) {
            std::cout << "Previous sibling of next sibling has index: " << prev_sibling << std::endl;
        } else {
            std::cout << "Next sibling has no previous sibling." << std::endl;
        }
    } catch (const std::out_of_range& e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }

    return 0;
}
