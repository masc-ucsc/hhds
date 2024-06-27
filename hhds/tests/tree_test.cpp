// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <benchmark/benchmark.h>
#include <iostream>
#include <vector>
#include <random>
#include <TreeDS/tree> // Include the necessary headers for your tree data structures

#include "../tree.hpp"
#include "../lhtree.hpp"

// Node structure example (replace with your actual node structure)
struct Node {
    std::vector<Node> children;
};

// Function to generate a random tree with specified characteristics
Node generateRandomTree(std::default_random_engine& generator, int depth) {
    Node node;
    std::uniform_int_distribution<int> numChildrenDist(4, 100);
    std::uniform_int_distribution<int> numLeafChildrenDist(4, 8);

    if (depth > 0) {
        int numChildren = numChildrenDist(generator);
        for (int i = 0; i < numChildren; ++i) {
            node.children.push_back(generateRandomTree(generator, depth - 1));
        }
    } else {
        int numLeafChildren = numLeafChildrenDist(generator);
        for (int i = 0; i < numLeafChildren; ++i) {
            node.children.push_back(Node()); // Leaf node with children
        }
    }

    return node;
}

static void BM_InsertRandomTree(benchmark::State& state) {
    std::default_random_engine generator;
    Node randomTree = generateRandomTree(generator, 3); // Adjust depth as needed

    while (state.KeepRunning()) {
        // Insert nodes into the random tree
        randomTree.children.push_back(Node());
        benchmark::DoNotOptimize(randomTree); // Prevent compiler optimizations
    }
}

// Register the benchmark
BENCHMARK(BM_InsertRandomTree);

// Run the benchmark
BENCHMARK_MAIN();
