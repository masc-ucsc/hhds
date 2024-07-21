#include <iostream>
#include <vector>
#include <random>
#include <fstream>
#include <queue>
#include <TreeDS/tree>
#include "../tree.hpp"
#include "../lhtree.hpp"
#include "testlib.h" // Globally included on the device
#include <gtest/gtest.h>

// Function to generate a random tree and renumber nodes based on BFS traversal
void generate_random_tree(int n, int t, std::vector<std::pair<int, int>>& renumbered_edges) {
    std::vector<int> p(n);
    for(int i = 0; i < n; ++i)
        if (i > 0)
            p[i] = rnd.wnext(i, t);

    std::vector<int> perm(n);
    for(int i = 0; i < n; ++i)
        perm[i] = i;
    shuffle(perm.begin() + 1, perm.end());

    std::vector<std::pair<int, int>> edges;
    for (int i = 1; i < n; i++) {
        edges.push_back(std::make_pair(std::min(perm[i], perm[p[i]]),
                                       std::max(perm[i], perm[p[i]])));
    }

    // Renumber nodes based on BFS traversal
    std::vector<std::vector<int>> adj_list(n);
    for (const auto& edge : edges) {
        adj_list[edge.first].push_back(edge.second);
        adj_list[edge.second].push_back(edge.first);
    }

    for (auto& edge : edges) {
        if (edge.second < edge.first) std::swap(edge.first, edge.second);
    }

    std::vector<int> bfs_order;
    std::vector<int> visited(n, 0);
    std::queue<int> q;
    q.push(0); // assuming 0 is the root
    visited[0] = 1;

    while (!q.empty()) {
        int node = q.front();
        q.pop();
        bfs_order.push_back(node);

        for (int neighbor : adj_list[node]) {
            if (!visited[neighbor]) {
                visited[neighbor] = 1;
                q.push(neighbor);
            }
        }
    }

    std::vector<int> new_labels(n);
    for (int i = 0; i < n; ++i) {
        new_labels[bfs_order[i]] = i;
    }

    for (const auto& edge : edges) {
        renumbered_edges.push_back({new_labels[edge.first], new_labels[edge.second]});
    }

    std::sort(renumbered_edges.begin(), renumbered_edges.end(), 
                [](const std::pair<int, int>& a, const std::pair<int, int>& b) {
                    if (a.first == b.first) return a.second < b.second;
                    return a.first < b.first;
                });

    // for (auto& edge : renumbered_edges) {
    //     std::cout << edge.first << " " << edge.second << ", ";
    // }    
}

void preorder_traversal_hhds(hhds::tree<int>& tree, std::vector<int>& result) {
    for (auto it = tree.pre_order_begin(); it != tree.pre_order_end(); ++it) {
        result.push_back(*it);
    }
}

void preorder_traversal_lhtree(lh::tree<int>& tree, std::vector<int>& result) {
    // Assuming the tree class has a method to get the root index
    auto root_index = lh::Tree_index(0, 0);
    
    // Using the Tree_depth_preorder_iterator provided by lhtree
    typename lh::tree<int>::Tree_depth_preorder_iterator it(root_index, &tree);
    
    for (auto node_it = it.begin(); node_it != it.end(); ++node_it) {
        // Assuming tree has a method to get the value at a given index
        result.push_back(tree.get_data(*node_it));
    }
}

// void postorder_traversal_hhds(hhds::tree<int>& tree, std::vector<int>& result) {
//     for (auto it = tree.post_order_begin(); it != tree.post_order_end(); ++it) {
//         result.push_back(*it);
//     }
// }

// void postorder_traversal_lhtree(lh::tree<int>& tree, std::vector<int>& result) {
//     lh::Tree_index root_index = lh::Tree_index(0, 0); // Assuming the root is at level 0, position 0
//     auto it = tree.get_depth_postorder_iterator(root_index).begin();
//     auto end = tree.get_depth_postorder_iterator(root_index).end();
//     for (; it != end; ++it) {
//         result.push_back(tree.get_data(*it));
//     }
// }

TEST(TreeTest, RandomTreeTest) {
    const int num_tests = 1;
    const int num_vertices = 100;
    const int t = 0;

    for (int test = 0; test < num_tests; ++test) {
        std::vector<std::pair<int, int>> edges;
        generate_random_tree(num_vertices, t, edges);

        hhds::tree<int> hhds_tree;
        lh::tree<int> lh_tree;

        std::default_random_engine generator(test);
        std::uniform_int_distribution<int> dataDist(1, 100);

        hhds_tree.add_root(0);
        lh::Tree_index root_index = lh::Tree_index(0, 0); // Assuming the root is at level 0, position 0
        lh_tree.set_root(0);

        // Print the edges
        // for (auto& edge : edges) {
        //     std::cout << edge.first << " " << edge.second << std::endl;
        // }

        for (const auto& edge : edges) {
            int parent = edge.first;
            int child = edge.second;
            hhds_tree.add_child(parent, child);
            // lh_tree.add_child(parent, child);
            break;
        }

        std::vector<int> hhds_preorder_result;
        std::vector<int> lh_preorder_result;
        std::vector<int> hhds_postorder_result;
        std::vector<int> lh_postorder_result;

        preorder_traversal_hhds(hhds_tree, hhds_preorder_result);
        preorder_traversal_lhtree(lh_tree, lh_preorder_result);
        // postorder_traversal_hhds(hhds_tree, hhds_postorder_result);
        // postorder_traversal_lhtree(lh_tree, lh_postorder_result);

        EXPECT_EQ(hhds_preorder_result, lh_preorder_result) << "Preorder traversal test case " << test << " failed.";
        // EXPECT_EQ(hhds_postorder_result, lh_postorder_result) << "Postorder traversal test case " << test << " failed.";
        
        if (hhds_preorder_result != lh_preorder_result || hhds_postorder_result != lh_postorder_result) {
            std::ofstream out("failing_test_case.txt");
            out << num_vertices << " " << t << std::endl;
            for (auto& edge : edges) {
                out << edge.first + 1 << " " << edge.second + 1 << std::endl;
            }
            out.close();
            std::cerr << "Test case " << test << " failed. Written to failing_test_case.txt" << std::endl;
            break;
        }
    }
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    registerGen(argc, argv, 1); // Adjust if necessary
    return RUN_ALL_TESTS();
}
