// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <benchmark/benchmark.h>
#include <iostream>
#include <vector>
#include <random>
#include <TreeDS/tree>

#include "../tree.hpp"
#include "../lhtree.hpp"

// Function to benchmark HHDS tree generation
void test_rand_tree_hhds_tree(std::default_random_engine& generator) {
    hhds::tree<int> my_tree;
    std::uniform_int_distribution<int> rootDataDist(1, 100);
    std::uniform_int_distribution<int> numChildrenDist(4, 16); // Adjust based on the level
    std::uniform_int_distribution<int> leafChildrenDist(4, 8);
    std::uniform_int_distribution<int> nodeDataDist(1, 100); // Random data for nodes

    // Create root node
    int rootData = rootDataDist(generator);
    hhds::Tree_pos root = my_tree.add_root(rootData);

    // Generate random tree
    // Level 1
    int numChildrenLevel1 = numChildrenDist(generator);
    std::vector<hhds::Tree_pos> level1Children;
    for (int i = 0; i < numChildrenLevel1; ++i) {
        int data = nodeDataDist(generator);
        hhds::Tree_pos child = my_tree.add_child(root, data);
        level1Children.push_back(child);
    }

    // Level 2
    std::vector<hhds::Tree_pos> level2Children;
    for (hhds::Tree_pos parent : level1Children) {
        int numChildrenLevel2 = numChildrenDist(generator);
        for (int j = 0; j < numChildrenLevel2; ++j) {
            int data = nodeDataDist(generator);
            hhds::Tree_pos child = my_tree.add_child(parent, data);
            level2Children.push_back(child);
        }
    }

    // Level 3 (leaves)
    for (hhds::Tree_pos parent : level2Children) {
        int numChildrenLevel3 = leafChildrenDist(generator);
        for (int k = 0; k < numChildrenLevel3; ++k) {
            int data = nodeDataDist(generator);
            my_tree.add_child(parent, data);
        }
    }
}

void test_rand_tree_lh_tree() {
    // Implementation for testing random tree insertion for lh tree
}

void test_rand_tree_generic_tree() {
    // Implementation for testing random tree insertion for generic tree
}


// Benchmark function for test_rand_tree_hhds_tree
static void BM_TestRandTreeHHDS(benchmark::State& state) {
    std::default_random_engine generator(42); // Fixed seed for reproducibility
    for (auto _ : state) {
        test_rand_tree_hhds_tree(generator);
    }
}

// Benchmark function for test_rand_tree_lh_tree
static void BM_TestRandTreeLH(benchmark::State& state) {
    for (auto _ : state) {
        test_rand_tree_lh_tree();
    }
}

// Benchmark function for test_rand_tree_generic_tree
static void BM_TestRandTreeGeneric(benchmark::State& state) {
    for (auto _ : state) {
        test_rand_tree_generic_tree();
    }
}

// Register the benchmarks
BENCHMARK(BM_TestRandTreeHHDS);
BENCHMARK(BM_TestRandTreeLH);
BENCHMARK(BM_TestRandTreeGeneric);

// Run the benchmarks
BENCHMARK_MAIN();
