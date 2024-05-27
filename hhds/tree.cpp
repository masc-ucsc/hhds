// test_tree.cpp
#include "tree.hpp"
#include <iostream>

using namespace hhds;

// Function to print the status of a node
template<typename T>
void print_node_status(const Tree<T>& tree, Tree_index idx) {
    std::cout << "Node " << idx << " status:\n";
    std::cout << "  is_empty: " << tree.is_empty(idx) << "\n";
    std::cout << "  is_first_child: " << tree.is_first_child(idx) << "\n";
    std::cout << "  is_last_child: " << tree.is_last_child(idx) << "\n";
    std::cout << "  First child: " << tree.get_first_child(idx) << "\n";
    std::cout << "  Last child: " << tree.get_last_child(idx) << "\n";
    std::cout << "  Previous sibling: " << tree.get_sibling_prev(idx) << "\n";
    std::cout << "  Next sibling: " << tree.get_sibling_next(idx) << "\n";
    std::cout << "  Parent: " << tree.get_parent(idx) << "\n";
    std::cout << std::endl;
}

int main() {
    Tree<std::string> tree;

    // Add root node
    // tree.add_child(static_cast<Tree_index>(Tree_Node::SpecialIndex::INVALID), "root");
    tree.add_root("root");
    Tree_index root_idx = 0;

    // Add child nodes
    std::cout << "Adding child node 1 \n";
    tree.add_child(root_idx, "child1");
    std::cout << "Adding child node 2 \n";
    tree.add_child(root_idx, "child2");

    // Add grandchild nodes
    Tree_index child1_idx = tree.get_first_child(root_idx);
    Tree_index child2_idx = tree.get_sibling_next(child1_idx);
    std::cout << "Adding grandchild node 1 \n";
    tree.add_child(child1_idx, "grandchild1");
    std::cout << "Adding grandchild node 2 \n";
    tree.add_child(child1_idx, "grandchild2");

    std::cout << "Adding greatgrandchild node 1 \n";
    child1_idx = tree.get_last_child(child1_idx);
    tree.add_child(child2_idx, "greatgrandchild1");

    return 0;
}
