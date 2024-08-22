#include <iostream>
#include <vector>
#include <random>

#include "../tree.hpp"
#include "../lhtree.hpp"

std::vector<std::vector<int>> hhds_sibling_data, lh_sibling_data;

// Utility function to generate a random int within a range
int generate_random_int(std::default_random_engine& generator, int min, int max) {
    std::uniform_int_distribution<int> distribution(min, max);
    return distribution(generator);
}

// Preorder traversal for hhds::tree
void preorder_traversal_hhds(hhds::tree<int>& tree, std::vector<int>& result) {
    for (const auto& node : tree.pre_order()) {
        result.push_back(tree[node]);
        // Use the sibling order iterator here
        std::vector<int> sibling_data;
        for (const auto& sibling : tree.sibling_order(node)) {
            sibling_data.push_back(tree[sibling]);
        }
        hhds_sibling_data.push_back(sibling_data);
    }
}

// Postorder traversal for hhds::tree
void postorder_traversal_hhds(hhds::tree<int>& tree, std::vector<int>& result) {
    for (const auto& node : tree.post_order()) {
        result.push_back(tree[node]);
    }
}

// Preorder traversal for lh::tree
void preorder_traversal_lhtree(lh::tree<int>& tree, std::vector<int>& result) {
    auto root_index = lh::Tree_index(0, 0);
    typename lh::tree<int>::Tree_depth_preorder_iterator it(root_index, &tree);
    for (auto node_it = it.begin(); node_it != it.end(); ++node_it) {
        result.push_back(tree.get_data(*node_it));

        // Use the sibling order iterator here
        std::vector<int> sibling_data;
        lh::tree<int>::Tree_sibling_iterator sib_it(*node_it, &tree);
        for (auto sib = sib_it.begin(); sib != sib_it.end(); ++sib) {
            sibling_data.push_back(tree.get_data(*sib));
        }
        lh_sibling_data.push_back(sibling_data);
    }
}

// Postorder traversal for lh::tree
void postorder_traversal_lhtree(lh::tree<int>& tree, std::vector<int>& result) {
    auto root_index = lh::Tree_index(0, 0);
    typename lh::tree<int>::Tree_depth_postorder_iterator it(root_index, &tree);
    for (auto node_it = it.begin(); node_it != it.end(); ++node_it) {
        result.push_back(tree.get_data(*node_it));
    }
}

// Utility function to compare two vectors
template <typename T>
bool compare_vectors(const std::vector<T>& vec1, const std::vector<T>& vec2) {
    return vec1 == vec2;
}

// Function to collect all leaf nodes from the tree
void collect_leaves_hhds(hhds::tree<int>& tree, std::vector<hhds::Tree_pos>& leaves) {
    for (const auto& node : tree.pre_order()) {
        if (tree.get_first_child(node) == hhds::INVALID) {
            leaves.push_back(node);
        }
    }
}

void collect_leaves_lhtree(lh::tree<int>& tree, std::vector<lh::Tree_index>& leaves) {
    auto root_index = lh::Tree_index(0, 0);
    typename lh::tree<int>::Tree_depth_preorder_iterator it(root_index, &tree);
    for (auto node_it = it.begin(); node_it != it.end(); ++node_it) {
        if (tree.is_leaf(*node_it)) {
            leaves.push_back(*node_it);
        }
    }
}

// Test 3: "Chip" Typical Tree (8 Depth, 4-8 Children per Node)
void test_chip_tree() {
    std::default_random_engine generator(42);

    hhds::tree<int> hhds_tree;
    lh::tree<int> lh_tree;

    auto hhds_root = hhds_tree.add_root(0);
    lh_tree.set_root(0);

    std::vector<hhds::Tree_pos> hhds_current_level{hhds_root};
    std::vector<lh::Tree_index> lh_current_level{lh::Tree_index(0, 0)};

    int id = 1;
    for (int depth = 0; depth < 7; ++depth) {
        std::vector<hhds::Tree_pos> hhds_next_level;
        std::vector<lh::Tree_index> lh_next_level;
        std::vector<std::vector<int>> level_data;

        for (auto hhds_node : hhds_current_level) {
            int num_children = generate_random_int(generator, 2, 20);
            std::vector<int> children_data;

            for (int i = 0; i < num_children; ++i) {
                int data = id++;
                auto added = hhds_tree.add_child(hhds_node, data);

                hhds_next_level.push_back(added);
                children_data.push_back(data);
            }
            level_data.push_back(children_data);
        }

        for (size_t i = 0; i < lh_current_level.size(); ++i) {
            auto lh_node = lh_current_level[i];
            for (int data : level_data[i]) {
                lh_next_level.push_back(lh_tree.add_child(lh_node, data));
            }
        }

        hhds_current_level = hhds_next_level;
        lh_current_level = lh_next_level;
    }
    std::cout << "HHDS TREE READY!!\n";


    std::vector<int> hhds_preorder, lh_preorder, hhds_postorder, lh_postorder;
    preorder_traversal_hhds(hhds_tree, hhds_preorder);
    preorder_traversal_lhtree(lh_tree, lh_preorder);
    postorder_traversal_hhds(hhds_tree, hhds_postorder);
    postorder_traversal_lhtree(lh_tree, lh_postorder);

    // std::cout << "\nHHDS preorder: ";
    // for (auto node : hhds_preorder) {
    //     std::cout << node << " ";
    // }
    // std::cout << std::endl;

    // std::cout << "\nLH preorder: ";
    // for (auto node : lh_preorder) {
    //     std::cout << node << " ";
    // }
    // std::cout << std::endl;

    bool sib_valid = true;
    for (auto i = 0; i < hhds_sibling_data.size(); ++i) {
        if (i > lh_sibling_data.size()) {
            std::cerr << "Sibling data mismatch in test_chip_tree" << std::endl;
            sib_valid = false;
        }
        if (!compare_vectors(hhds_sibling_data[i], lh_sibling_data[i])) {
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

    // std::vector<hhds::Tree_pos> hhds_leaves;
    // std::vector<lh::Tree_index> lh_leaves;
    // collect_leaves_hhds(hhds_tree, hhds_leaves);
    // collect_leaves_lhtree(lh_tree, lh_leaves);

    // // Now randomly delete_leaf from the tree, delete same leaves
    // for (auto &x : hhds_leaves) {
    //     hhds_tree.delete_leaf(x);
    // }

    // for (auto &x : lh_leaves) {
    //     lh_tree.delete_leaf(x);
    // }

    // // Do a preorder traversal again and confirm equality
    // std::vector<int> hhds_preorder_after, lh_preorder_after;
    // preorder_traversal_hhds(hhds_tree, hhds_preorder_after);
    // preorder_traversal_lhtree(lh_tree, lh_preorder_after);

    // if (!compare_vectors(hhds_preorder_after, lh_preorder_after)) {
    //     std::cerr << "Preorder traversal mismatch after deleting leaves in test_chip_tree" << std::endl;
    // } else {
    //     std::cout << "Preorder traversal match after deleting leaves in test_chip_tree" << std::endl;
    // }
}

int main() {
    test_chip_tree();
    return 0;
}
