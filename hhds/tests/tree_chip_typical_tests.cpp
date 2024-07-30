#include <benchmark/benchmark.h>
#include <iostream>
#include <vector>
#include <random>
#include <TreeDS/tree>

#include "../tree.hpp"
#include "../lhtree.hpp"

// Utility function to generate a random int within a range
int generate_random_int(std::default_random_engine& generator, int min, int max) {
    std::uniform_int_distribution<int> distribution(min, max);
    return distribution(generator);
}

// Preorder traversal for hhds::tree
void preorder_traversal_hhds(hhds::tree<int>& tree, std::vector<int>& result) {
    for (const auto& node : tree.pre_order()) {
        result.push_back(node);
    }
}

// Postorder traversal for hhds::tree
void postorder_traversal_hhds(hhds::tree<int>& tree, std::vector<int>& result) {
    for (const auto& node : tree.post_order()) {
        result.push_back(node);
    }
}

// Preorder traversal for lh::tree
void preorder_traversal_lhtree(lh::tree<int>& tree, std::vector<int>& result) {
    auto root_index = lh::Tree_index(0, 0);
    typename lh::tree<int>::Tree_depth_preorder_iterator it(root_index, &tree);
    for (auto node_it = it.begin(); node_it != it.end(); ++node_it) {
        result.push_back(tree.get_data(*node_it));
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

// Test 1: Very Deep Tree (Tens of Millions of Nodes)
void test_deep_tree(benchmark::State& state) {
    std::default_random_engine generator(42);
    int num_nodes = 100; // 10 million nodes
    for (auto _ : state) {
        hhds::tree<int> hhds_tree;
        lh::tree<int> lh_tree;

        auto hhds_root = hhds_tree.add_root(generate_random_int(generator, 1, 100));
        lh_tree.set_root(generate_random_int(generator, 1, 100));
        lh::Tree_index lh_current(0, 0);
        auto hhds_current = hhds_root;

        for (int i = 0; i < num_nodes; ++i) {
            hhds_current = hhds_tree.add_child(hhds_current, generate_random_int(generator, 1, 100));
            lh_current = lh_tree.add_child(lh_current, generate_random_int(generator, 1, 100));
        }

        std::vector<int> hhds_preorder, lh_preorder, hhds_postorder, lh_postorder;
        preorder_traversal_hhds(hhds_tree, hhds_preorder);
        preorder_traversal_lhtree(lh_tree, lh_preorder);
        postorder_traversal_hhds(hhds_tree, hhds_postorder);
        postorder_traversal_lhtree(lh_tree, lh_postorder);

        if (!compare_vectors(hhds_preorder, lh_preorder)) {
            std::cerr << "Preorder traversal mismatch in test_deep_tree" << std::endl;
        }
        if (!compare_vectors(hhds_postorder, lh_postorder)) {
            std::cerr << "Postorder traversal mismatch in test_deep_tree" << std::endl;
        }
    }
}

// Test 2: Very Wide Tree (Tens of Millions of Nodes)
void test_wide_tree(benchmark::State& state) {
    std::default_random_engine generator(42);
    int num_children = 100; // 10 million children
    for (auto _ : state) {
        hhds::tree<int> hhds_tree;
        lh::tree<int> lh_tree;

        auto hhds_root = hhds_tree.add_root(generate_random_int(generator, 1, 100));
        lh_tree.set_root(generate_random_int(generator, 1, 100));
        lh::Tree_index lh_root(0, 0);

        for (int i = 0; i < num_children; ++i) {
            hhds_tree.add_child(hhds_root, generate_random_int(generator, 1, 100));
            lh_tree.add_child(lh_root, generate_random_int(generator, 1, 100));
        }

        std::vector<int> hhds_preorder, lh_preorder, hhds_postorder, lh_postorder;
        preorder_traversal_hhds(hhds_tree, hhds_preorder);
        preorder_traversal_lhtree(lh_tree, lh_preorder);
        postorder_traversal_hhds(hhds_tree, hhds_postorder);
        postorder_traversal_lhtree(lh_tree, lh_postorder);

        if (!compare_vectors(hhds_preorder, lh_preorder)) {
            std::cerr << "Preorder traversal mismatch in test_wide_tree" << std::endl;
        }
        if (!compare_vectors(hhds_postorder, lh_postorder)) {
            std::cerr << "Postorder traversal mismatch in test_wide_tree" << std::endl;
        }
    }
}

// Test 3: "Chip" Typical Tree (8 Depth, 4-8 Children per Node)
void test_chip_tree(benchmark::State& state) {
    std::default_random_engine generator(42);
    for (auto _ : state) {
        hhds::tree<int> hhds_tree;
        lh::tree<int> lh_tree;

        auto hhds_root = hhds_tree.add_root(generate_random_int(generator, 1, 100));
        lh_tree.set_root(generate_random_int(generator, 1, 100));

        std::vector<hhds::Tree_pos> hhds_current_level{hhds_root};
        std::vector<lh::Tree_index> lh_current_level{lh::Tree_index(0, 0)};

        for (int depth = 0; depth < 8; ++depth) {
            std::vector<hhds::Tree_pos> hhds_next_level;
            std::vector<lh::Tree_index> lh_next_level;
            for (auto hhds_node : hhds_current_level) {
                int num_children = generate_random_int(generator, 4, 8);
                for (int i = 0; i < num_children; ++i) {
                    hhds_next_level.push_back(hhds_tree.add_child(hhds_node, generate_random_int(generator, 1, 100)));
                }
            }
            for (auto lh_node : lh_current_level) {
                int num_children = generate_random_int(generator, 4, 8);
                for (int i = 0; i < num_children; ++i) {
                    lh_next_level.push_back(lh_tree.add_child(lh_node, generate_random_int(generator, 1, 100)));
                }
            }
            hhds_current_level = hhds_next_level;
            lh_current_level = lh_next_level;
        }

        std::vector<int> hhds_preorder, lh_preorder, hhds_postorder, lh_postorder;
        preorder_traversal_hhds(hhds_tree, hhds_preorder);
        preorder_traversal_lhtree(lh_tree, lh_preorder);
        postorder_traversal_hhds(hhds_tree, hhds_postorder);
        postorder_traversal_lhtree(lh_tree, lh_postorder);

        if (!compare_vectors(hhds_preorder, lh_preorder)) {
            std::cerr << "Preorder traversal mismatch in test_chip_tree" << std::endl;
        }
        if (!compare_vectors(hhds_postorder, lh_postorder)) {
            std::cerr << "Postorder traversal mismatch in test_chip_tree" << std::endl;
        }
    }
}

// Test 4: Append End Only
void test_append_end_only(benchmark::State& state) {
    std::default_random_engine generator(42);
    int num_nodes = 100; // 10 million nodes
    for (auto _ : state) {
        hhds::tree<int> hhds_tree;
        lh::tree<int> lh_tree;

        auto hhds_root = hhds_tree.add_root(generate_random_int(generator, 1, 100));
        lh_tree.set_root(generate_random_int(generator, 1, 100));
        lh::Tree_index lh_current(0, 0);
        auto hhds_current = hhds_root;

        for (int i = 0; i < num_nodes; ++i) {
            hhds_current = hhds_tree.add_child(hhds_current, generate_random_int(generator, 1, 100));
            lh_current = lh_tree.add_child(lh_current, generate_random_int(generator, 1, 100));
        }

        std::vector<int> hhds_preorder, lh_preorder, hhds_postorder, lh_postorder;
        preorder_traversal_hhds(hhds_tree, hhds_preorder);
        preorder_traversal_lhtree(lh_tree, lh_preorder);
        postorder_traversal_hhds(hhds_tree, hhds_postorder);
        postorder_traversal_lhtree(lh_tree, lh_postorder);

        for (auto node : hhds_preorder) {
            std::cout << node << ' ';
        }
        std::cout << std::endl;

        if (!compare_vectors(hhds_preorder, lh_preorder)) {
            std::cerr << "Preorder traversal mismatch in test_append_end_only" << std::endl;
        }
        if (!compare_vectors(hhds_postorder, lh_postorder)) {
            std::cerr << "Postorder traversal mismatch in test_append_end_only" << std::endl;
        }
    }
}

// Benchmark registration
BENCHMARK(test_deep_tree);
// BENCHMARK(test_wide_tree);
// BENCHMARK(test_chip_tree);
// BENCHMARK(test_append_end_only);

// Run the benchmarks
BENCHMARK_MAIN();
