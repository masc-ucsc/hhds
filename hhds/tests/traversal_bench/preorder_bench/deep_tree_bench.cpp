#include <benchmark/benchmark.h>
#include <iostream>
#include <vector>
#include <random>
#include <chrono>

#include "../../../tree.hpp"
#include "../../../lhtree.hpp"

auto now = std::chrono::high_resolution_clock::now();
auto microseconds = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();
std::default_random_engine generator(microseconds);

// Utility function to generate a random int within a range
int generate_random_int(std::default_random_engine& generator, int min, int max) {
    std::uniform_int_distribution<int> distribution(min, max);
    return distribution(generator);
}

void build_hhds_tree(hhds::tree<int>& hhds_tree, int num_nodes) {
    auto hhds_root = hhds_tree.add_root(generate_random_int(generator, 1, 100));
    auto hhds_current = hhds_root;

    for (int i = 0; i < num_nodes; ++i) {
        hhds_current = hhds_tree.add_child(hhds_current, generate_random_int(generator, 1, 100));
    }
}

void build_lh_tree(lh::tree<int>& lh_tree, int num_nodes) {
    lh_tree.set_root(generate_random_int(generator, 1, 100));
    lh::Tree_index lh_current(0, 0);

    for (int i = 0; i < num_nodes; ++i) {
        lh_current = lh_tree.add_child(lh_current, generate_random_int(generator, 1, 100));
    }
}

// Preorder traversal for hhds::tree
void preorder_traversal_hhds(hhds::tree<int>& tree) {
    int cnt = 0;
    for (const auto& node : tree.pre_order()) {
        // result.push_back(tree[node]);
        cnt++;
    }
    benchmark::DoNotOptimize(cnt);
}
// void preorder_traversal_hhds(hhds::tree<int>& tree, std::vector<int>& result, 
//                              std::vector<std::vector<int>>& hhds_sibling_data) {
//     for (const auto& node : tree.pre_order()) {
//         result.push_back(tree[node]);

//         // Use the sibling order iterator here
//         std::vector<int> sibling_data;
//         for (const auto& sibling : tree.sibling_order(node)) {
//             sibling_data.push_back(tree[sibling]);
//         }
//         hhds_sibling_data.push_back(sibling_data);
//     }
// }

// Preorder traversal for lh::tree
void preorder_traversal_lhtree(lh::tree<int>& tree) {
    auto root_index = lh::Tree_index(0, 0);
    int cnt = 0;
    typename lh::tree<int>::Tree_depth_preorder_iterator it(root_index, &tree);
    for (auto node_it = it.begin(); node_it != it.end(); ++node_it) {
        // result.push_back(tree.get_data(*node_it));
        cnt++;
    }
    benchmark::DoNotOptimize(cnt);
}
// void preorder_traversal_lhtree(lh::tree<int>& tree, std::vector<int>& result,
//                                std::vector<std::vector<int>>& lh_sibling_data) {
//     auto root_index = lh::Tree_index(0, 0);
//     typename lh::tree<int>::Tree_depth_preorder_iterator it(root_index, &tree);
//     for (auto node_it = it.begin(); node_it != it.end(); ++node_it) {
//         result.push_back(tree.get_data(*node_it));

//         // Use the sibling order iterator here
//         std::vector<int> sibling_data;
//         lh::tree<int>::Tree_sibling_iterator sib_it(*node_it, &tree);
//         for (auto sib = sib_it.begin(); sib != sib_it.end(); ++sib) {
//             sibling_data.push_back(tree.get_data(*sib));
//         }
//         lh_sibling_data.push_back(sibling_data);
//     }
// }

// Postorder traversal for hhds::tree
void postorder_traversal_hhds(hhds::tree<int>& tree, std::vector<int>& result) {
    int cnt = 0;
    for (const auto& node : tree.post_order()) {
        // result.push_back(tree[node]);
        cnt++;
    }
}

// Postorder traversal for lh::tree
void postorder_traversal_lhtree(lh::tree<int>& tree, std::vector<int>& result) {
    auto root_index = lh::Tree_index(0, 0);
    typename lh::tree<int>::Tree_depth_postorder_iterator it(root_index, &tree);
    int cnt = 0;
    for (auto node_it = it.begin(); node_it != it.end(); ++node_it) {
        // result.push_back(tree.get_data(*node_it));
        cnt++;
    }
}

// Tree that is 10 nodes deep
void test_deep_tree_10_hhds(benchmark::State& state) {
    int num_nodes = 10;
    hhds::tree<int> hhds_tree;
    build_hhds_tree(hhds_tree, num_nodes);
    for (auto _ : state) {
        preorder_traversal_hhds(hhds_tree);
    }
}
void test_deep_tree_10_lh(benchmark::State& state) {
    int num_nodes = 10;
    lh::tree<int> lh_tree;
    build_lh_tree(lh_tree, num_nodes);
    for (auto _ : state) {
        preorder_traversal_lhtree(lh_tree);
    }
}

// Tree that is 100 nodes deep
void test_deep_tree_100_hhds(benchmark::State& state) {
    int num_nodes = 100;
    hhds::tree<int> hhds_tree;
    build_hhds_tree(hhds_tree, num_nodes);
    for (auto _ : state) {
        preorder_traversal_hhds(hhds_tree);
    }
}
void test_deep_tree_100_lh(benchmark::State& state) {
    int num_nodes = 100;
    lh::tree<int> lh_tree;
    build_lh_tree(lh_tree, num_nodes);
    for (auto _ : state) {
        preorder_traversal_lhtree(lh_tree);
    }
}

// Tree that is 1000 nodes deep
void test_deep_tree_1000_hhds(benchmark::State& state) {
    hhds::tree<int> hhds_tree;
    int num_nodes = 1000;
    build_hhds_tree(hhds_tree, num_nodes);
    for (auto _ : state) {
        preorder_traversal_hhds(hhds_tree);
    }
}
void test_deep_tree_1000_lh(benchmark::State& state) {
    lh::tree<int> lh_tree;
    int num_nodes = 1000;
    build_lh_tree(lh_tree, num_nodes);
    for (auto _ : state) {
        preorder_traversal_lhtree(lh_tree);
    }
}

// Tree that is 10000 nodes deep
void test_deep_tree_10000_hhds(benchmark::State& state) {
    int num_nodes = 10000;
    hhds::tree<int> hhds_tree;
    build_hhds_tree(hhds_tree, num_nodes);
    for (auto _ : state) {
        preorder_traversal_hhds(hhds_tree);
    }
}
void test_deep_tree_10000_lh(benchmark::State& state) {
    int num_nodes = 10000;
    lh::tree<int> lh_tree;
    build_lh_tree(lh_tree, num_nodes);
    for (auto _ : state) {
        preorder_traversal_lhtree(lh_tree);
    }
}

// Tree that is 100000 nodes deep
void test_deep_tree_100000_hhds(benchmark::State& state) {
    int num_nodes = 100000;
    hhds::tree<int> hhds_tree;
    build_hhds_tree(hhds_tree, num_nodes);
    for (auto _ : state) {
        preorder_traversal_hhds(hhds_tree);
    }
}
void test_deep_tree_100000_lh(benchmark::State& state) {
    int num_nodes = 100000;
    lh::tree<int> lh_tree;
    build_lh_tree(lh_tree, num_nodes);
    for (auto _ : state) {
        preorder_traversal_lhtree(lh_tree);
    }
}

// Tree that is 1000000 nodes deep
void test_deep_tree_1000000_hhds(benchmark::State& state) {
    int num_nodes = 1000000;
    hhds::tree<int> hhds_tree;
    build_hhds_tree(hhds_tree, num_nodes);
    for (auto _ : state) {
        preorder_traversal_hhds(hhds_tree);
    }
}
void test_deep_tree_1000000_lh(benchmark::State& state) {
    int num_nodes = 1000000;
    lh::tree<int> lh_tree;
    build_lh_tree(lh_tree, num_nodes);
    for (auto _ : state) {
        preorder_traversal_lhtree(lh_tree);
    }
}

// Tree that is 10000000 nodes deep
void test_deep_tree_10000000_hhds(benchmark::State& state) {
    int num_nodes = 10000000;
    hhds::tree<int> hhds_tree;
    build_hhds_tree(hhds_tree, num_nodes);
    for (auto _ : state) {
        preorder_traversal_hhds(hhds_tree);
    }
}
void test_deep_tree_10000000_lh(benchmark::State& state) {
    int num_nodes = 10000000;
    lh::tree<int> lh_tree;
    build_lh_tree(lh_tree, num_nodes);
    for (auto _ : state) {
        preorder_traversal_lhtree(lh_tree);
    }
}

// Benchmark registration
BENCHMARK(test_deep_tree_10_hhds);
BENCHMARK(test_deep_tree_10_lh);
BENCHMARK(test_deep_tree_100_hhds);
BENCHMARK(test_deep_tree_100_lh);
BENCHMARK(test_deep_tree_1000_hhds);
BENCHMARK(test_deep_tree_1000_lh);
BENCHMARK(test_deep_tree_10000_hhds);
BENCHMARK(test_deep_tree_10000_lh);
BENCHMARK(test_deep_tree_100000_hhds);
BENCHMARK(test_deep_tree_100000_lh);
BENCHMARK(test_deep_tree_1000000_hhds);
BENCHMARK(test_deep_tree_1000000_lh);
BENCHMARK(test_deep_tree_10000000_hhds);
BENCHMARK(test_deep_tree_10000000_lh);

// Run the benchmarks
BENCHMARK_MAIN();
