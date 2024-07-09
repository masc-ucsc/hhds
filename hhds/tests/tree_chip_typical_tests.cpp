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

// Test 1: Very Deep Tree (Tens of Millions of Nodes)
void test_deep_tree_hhds(benchmark::State& state) {
    std::default_random_engine generator(42);
    int num_nodes = 10000000; // 10 million nodes
    for (auto _ : state) {
        hhds::tree<int> my_tree;
        auto root = my_tree.add_root(generate_random_int(generator, 1, 100));
        auto current = root;
        for (int i = 0; i < num_nodes; ++i) {
            current = my_tree.add_child(current, generate_random_int(generator, 1, 100));
        }
    }
}

void test_deep_tree_lh(benchmark::State& state) {
    std::default_random_engine generator(42);
    int num_nodes = 10000000; // 10 million nodes
    for (auto _ : state) {
        lh::tree<int> my_tree;
        my_tree.set_root(generate_random_int(generator, 1, 100));
        lh::Tree_index current(0, 0);
        for (int i = 0; i < num_nodes; ++i) {
            current = my_tree.add_child(current, generate_random_int(generator, 1, 100));
        }
    }
}

// Test 2: Very Wide Tree (Tens of Millions of Nodes)
void test_wide_tree_hhds(benchmark::State& state) {
    std::default_random_engine generator(42);
    int num_children = 10000000; // 10 million children
    for (auto _ : state) {
        hhds::tree<int> my_tree;
        auto root = my_tree.add_root(generate_random_int(generator, 1, 100));
        for (int i = 0; i < num_children; ++i) {
            my_tree.add_child(root, generate_random_int(generator, 1, 100));
        }
    }
}

void test_wide_tree_lh(benchmark::State& state) {
    std::default_random_engine generator(42);
    int num_children = 10000000; // 10 million children
    for (auto _ : state) {
        lh::tree<int> my_tree;
        my_tree.set_root(generate_random_int(generator, 1, 100));
        lh::Tree_index root(0, 0);
        for (int i = 0; i < num_children; ++i) {
            my_tree.add_child(root, generate_random_int(generator, 1, 100));
        }
    }
}

// Test 3: "Chip" Typical Tree (8 Depth, 4-8 Children per Node)
void test_chip_tree_hhds(benchmark::State& state) {
    std::default_random_engine generator(42);
    for (auto _ : state) {
        hhds::tree<int> my_tree;
        auto root = my_tree.add_root(generate_random_int(generator, 1, 100));

        std::vector<hhds::Tree_pos> current_level{root};
        for (int depth = 0; depth < 8; ++depth) {
            std::vector<hhds::Tree_pos> next_level;
            for (auto node : current_level) {
                int num_children = generate_random_int(generator, 4, 8);
                for (int i = 0; i < num_children; ++i) {
                    next_level.push_back(my_tree.add_child(node, generate_random_int(generator, 1, 100)));
                }
            }
            current_level = next_level;
        }
    }
}

void test_chip_tree_lh(benchmark::State& state) {
    std::default_random_engine generator(42);
    for (auto _ : state) {
        lh::tree<int> my_tree;
        my_tree.set_root(generate_random_int(generator, 1, 100));

        std::vector<lh::Tree_index> current_level{lh::Tree_index(0, 0)};
        for (int depth = 0; depth < 8; ++depth) {
            std::vector<lh::Tree_index> next_level;
            for (auto node : current_level) {
                int num_children = generate_random_int(generator, 4, 8);
                for (int i = 0; i < num_children; ++i) {
                    next_level.push_back(my_tree.add_child(node, generate_random_int(generator, 1, 100)));
                }
            }
            current_level = next_level;
        }
    }
}

// Test 4: Append End Only
void test_append_end_only_hhds(benchmark::State& state) {
    std::default_random_engine generator(42);
    int num_nodes = 10000000; // 10 million nodes
    for (auto _ : state) {
        hhds::tree<int> my_tree;
        auto root = my_tree.add_root(generate_random_int(generator, 1, 100));
        auto current = root;
        for (int i = 0; i < num_nodes; ++i) {
            current = my_tree.add_child(current, generate_random_int(generator, 1, 100));
        }
    }
}

void test_append_end_only_lh(benchmark::State& state) {
    std::default_random_engine generator(42);
    int num_nodes = 10000000; // 10 million nodes
    for (auto _ : state) {
        lh::tree<int> my_tree;
        my_tree.set_root(generate_random_int(generator, 1, 100));
        lh::Tree_index current(0, 0);
        for (int i = 0; i < num_nodes; ++i) {
            current = my_tree.add_child(current, generate_random_int(generator, 1, 100));
        }
    }
}

// Benchmark registration
BENCHMARK(test_deep_tree_hhds);
BENCHMARK(test_deep_tree_lh);
BENCHMARK(test_wide_tree_hhds);
BENCHMARK(test_wide_tree_lh);
BENCHMARK(test_chip_tree_hhds);
BENCHMARK(test_chip_tree_lh);
BENCHMARK(test_append_end_only_hhds);
BENCHMARK(test_append_end_only_lh);

// Run the benchmarks
BENCHMARK_MAIN();
