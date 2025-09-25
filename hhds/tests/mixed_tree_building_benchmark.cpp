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

// Build tree with mixed operations - more realistic tree construction
void build_mixed_tree(hhds::tree<int>& tree, int depth_val) {
    auto root = tree.add_root(0);

    std::vector<hhds::Tree_pos> current_level{root};
    int id = 1;

    for (int depth = 0; depth < depth_val; ++depth) {
        std::vector<hhds::Tree_pos> next_level;

        for (auto node : current_level) {
            int num_children = generate_random_int(generator, 4, 10); // 4-10 children per node

            // Add first child normally
            if (num_children > 0) {
                int data = id++;
                auto first_child = tree.add_child(node, data);
                next_level.push_back(first_child);

                // For remaining children, mix add_child and insert_next_sibling
                auto current_sibling = first_child;
                for (int i = 1; i < num_children; ++i) {
                    int operation_choice = generate_random_int(generator, 1, 100);
                    data = id++;

                    if (operation_choice <= 70) { // 70% chance: add as next child to parent
                        auto new_child = tree.add_child(node, data);
                        next_level.push_back(new_child);
                        current_sibling = new_child;
                    } else { // 30% chance: insert as next sibling to current
                        auto new_sibling = tree.insert_next_sibling(current_sibling, data);
                        next_level.push_back(new_sibling);
                        current_sibling = new_sibling;
                    }
                }
            }
        }

        current_level = next_level;
    }
}

// Benchmark functions for different depths (4-6 levels as requested)
void test_mixed_tree_building_depth_4(benchmark::State& state) {
    int depth_val = 4;
    for (auto _ : state) {
        hhds::tree<int> tree;
        build_mixed_tree(tree, depth_val);
        benchmark::DoNotOptimize(tree);
    }
}

void test_mixed_tree_building_depth_5(benchmark::State& state) {
    int depth_val = 5;
    for (auto _ : state) {
        hhds::tree<int> tree;
        build_mixed_tree(tree, depth_val);
        benchmark::DoNotOptimize(tree);
    }
}

void test_mixed_tree_building_depth_6(benchmark::State& state) {
    int depth_val = 6;
    for (auto _ : state) {
        hhds::tree<int> tree;
        build_mixed_tree(tree, depth_val);
        benchmark::DoNotOptimize(tree);
    }
}

// Benchmark registration
BENCHMARK(test_mixed_tree_building_depth_4);
BENCHMARK(test_mixed_tree_building_depth_5);
BENCHMARK(test_mixed_tree_building_depth_6);

// Run the benchmarks
BENCHMARK_MAIN();