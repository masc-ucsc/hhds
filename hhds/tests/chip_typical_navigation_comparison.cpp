#include <benchmark/benchmark.h>
#include <iostream>
#include <vector>
#include <random>
#include <chrono>

#include "tree.hpp"

auto now = std::chrono::high_resolution_clock::now();
auto microseconds = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();
std::default_random_engine generator(microseconds);

// Utility function to generate a random int within a range
int generate_random_int(std::default_random_engine& generator, int min, int max) {
    std::uniform_int_distribution<int> distribution(min, max);
    return distribution(generator);
}

// Build chip-typical tree with HHDS (equivalent to Rust version)
void build_hhds_tree(hhds::tree<int>& tree, int depth_val) {
    auto root = tree.add_root(0);

    std::vector<hhds::Tree_pos> current_level{root};
    int id = 1;

    for (int depth = 0; depth < depth_val; ++depth) {
        std::vector<hhds::Tree_pos> next_level;

        for (auto node : current_level) {
            int num_children = generate_random_int(generator, 1, 7);
            for (int i = 0; i < num_children; ++i) {
                int data = id++;
                auto added = tree.add_child(node, data);
                next_level.push_back(added);
            }
        }

        current_level = next_level;
    }
}

// Test navigation operations on HHDS tree (equivalent to Rust version)
uint64_t test_hhds_navigation(hhds::tree<int>& tree) {
    uint64_t operation_count = 0;

    // Traverse the tree and perform navigation operations
    for (const auto& node_pos : tree.pre_order()) {
        // Test parent navigation
        auto parent = tree.get_parent(node_pos);
        if (parent > 0) { operation_count++; }

        // Test child navigation
        auto first_child = tree.get_first_child(node_pos);
        if (first_child != hhds::INVALID) { operation_count++; }

        auto last_child = tree.get_last_child(node_pos);
        if (last_child != hhds::INVALID) { operation_count++; }

        // Test sibling navigation
        auto next_sibling = tree.get_sibling_next(node_pos);
        if (next_sibling != hhds::INVALID) { operation_count++; }

        auto prev_sibling = tree.get_sibling_prev(node_pos);
        if (prev_sibling != hhds::INVALID) { operation_count++; }
    }

    return operation_count;
}

// Benchmark functions for different depths
void test_chip_typical_navigation_depth_2(benchmark::State& state) {
    int depth_val = 2;
    hhds::tree<int> tree;
    build_hhds_tree(tree, depth_val);

    for (auto _ : state) {
        auto count = test_hhds_navigation(tree);
        benchmark::DoNotOptimize(count);
    }
}

void test_chip_typical_navigation_depth_3(benchmark::State& state) {
    int depth_val = 3;
    hhds::tree<int> tree;
    build_hhds_tree(tree, depth_val);

    for (auto _ : state) {
        auto count = test_hhds_navigation(tree);
        benchmark::DoNotOptimize(count);
    }
}

void test_chip_typical_navigation_depth_4(benchmark::State& state) {
    int depth_val = 4;
    hhds::tree<int> tree;
    build_hhds_tree(tree, depth_val);

    for (auto _ : state) {
        auto count = test_hhds_navigation(tree);
        benchmark::DoNotOptimize(count);
    }
}

void test_chip_typical_navigation_depth_5(benchmark::State& state) {
    int depth_val = 5;
    hhds::tree<int> tree;
    build_hhds_tree(tree, depth_val);

    for (auto _ : state) {
        auto count = test_hhds_navigation(tree);
        benchmark::DoNotOptimize(count);
    }
}

// Benchmark registration
BENCHMARK(test_chip_typical_navigation_depth_2);
BENCHMARK(test_chip_typical_navigation_depth_3);
BENCHMARK(test_chip_typical_navigation_depth_4);
BENCHMARK(test_chip_typical_navigation_depth_5);

// Run the benchmarks
BENCHMARK_MAIN();