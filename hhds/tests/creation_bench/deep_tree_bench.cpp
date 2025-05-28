#include <benchmark/benchmark.h>
#include <iostream>
#include <vector>
#include <random>
#include <chrono>

#include "tree.hpp"
#include "lhtree.hpp"

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
    //benchmark::DoNotOptimize(); -- is this needed?
}

void build_lh_tree(lh::tree<int>& lh_tree, int num_nodes) {
    lh_tree.set_root(generate_random_int(generator, 1, 100));
    lh::Tree_index lh_current(0, 0);

    for (int i = 0; i < num_nodes; ++i) {
        lh_current = lh_tree.add_child(lh_current, generate_random_int(generator, 1, 100));
    }
}

// Tree that is 10 nodes deep
void test_deep_tree_10_hhds(benchmark::State& state) {
    int num_nodes = 10;
    for (auto _ : state) {
    hhds::tree<int> hhds_tree;
    build_hhds_tree(hhds_tree, num_nodes);
    }
}
void test_deep_tree_10_lh(benchmark::State& state) {
    int num_nodes = 10;
    lh::tree<int> lh_tree;
    for (auto _ : state) {
    build_lh_tree(lh_tree, num_nodes);
    }
}

// Tree that is 100 nodes deep
void test_deep_tree_100_hhds(benchmark::State& state) {
    int num_nodes = 100;
    hhds::tree<int> hhds_tree;
    for (auto _ : state) {
    hhds::tree<int> hhds_tree;
    build_hhds_tree(hhds_tree, num_nodes);
    }
}

void test_deep_tree_100_lh(benchmark::State& state) {
    int num_nodes = 100;
    lh::tree<int> lh_tree;
    for (auto _ : state) {
    build_lh_tree(lh_tree, num_nodes);
    }
}

// Tree that is 1000 nodes deep
void test_deep_tree_1000_hhds(benchmark::State& state) {
    hhds::tree<int> hhds_tree;
    int num_nodes = 1000;
    for (auto _ : state) {
    hhds::tree<int> hhds_tree;
    build_hhds_tree(hhds_tree, num_nodes);
    }
}

void test_deep_tree_1000_lh(benchmark::State& state) {
    lh::tree<int> lh_tree;
    int num_nodes = 1000;
    for (auto _ : state) {
    build_lh_tree(lh_tree, num_nodes);
    }
}

// Tree that is 10000 nodes deep
void test_deep_tree_10000_hhds(benchmark::State& state) {
    int num_nodes = 10000;
    hhds::tree<int> hhds_tree;
    for (auto _ : state) {
    hhds::tree<int> hhds_tree;
    build_hhds_tree(hhds_tree, num_nodes);
    }
}

void test_deep_tree_10000_lh(benchmark::State& state) {
    int num_nodes = 10000;
    lh::tree<int> lh_tree;
    for (auto _ : state) {
    build_lh_tree(lh_tree, num_nodes);
    }
}

// Tree that is 100000 nodes deep
void test_deep_tree_100000_hhds(benchmark::State& state) {
    int num_nodes = 100000;
    hhds::tree<int> hhds_tree;
    for (auto _ : state) {
    hhds::tree<int> hhds_tree;
    build_hhds_tree(hhds_tree, num_nodes);
    }
}

void test_deep_tree_100000_lh(benchmark::State& state) {
    int num_nodes = 100000;
    lh::tree<int> lh_tree;
    for (auto _ : state) {
    build_lh_tree(lh_tree, num_nodes);
    }
}

// Tree that is 1000000 nodes deep
void test_deep_tree_1000000_hhds(benchmark::State& state) {
    int num_nodes = 1000000;
    hhds::tree<int> hhds_tree;
    for (auto _ : state) {
    hhds::tree<int> hhds_tree;
    build_hhds_tree(hhds_tree, num_nodes);
    }
}

void test_deep_tree_1000000_lh(benchmark::State& state) {
    int num_nodes = 1000000;
    lh::tree<int> lh_tree;
    for (auto _ : state) {
    build_lh_tree(lh_tree, num_nodes);
    }
}

// Tree that is 10000000 nodes deep
void test_deep_tree_10000000_hhds(benchmark::State& state) {
    int num_nodes = 10000000;
    hhds::tree<int> hhds_tree;
    for (auto _ : state) {
    hhds::tree<int> hhds_tree;
    build_hhds_tree(hhds_tree, num_nodes);
    }
}

void test_deep_tree_10000000_lh(benchmark::State& state) {
    int num_nodes = 10000000;
    lh::tree<int> lh_tree;
    for (auto _ : state) {
    build_lh_tree(lh_tree, num_nodes);
    }
}

// Benchmark registration
BENCHMARK(test_deep_tree_10_hhds);
//BENCHMARK(test_deep_tree_10_lh);
BENCHMARK(test_deep_tree_100_hhds);
//BENCHMARK(test_deep_tree_100_lh);
BENCHMARK(test_deep_tree_1000_hhds);
//BENCHMARK(test_deep_tree_1000_lh);
BENCHMARK(test_deep_tree_10000_hhds);
//BENCHMARK(test_deep_tree_10000_lh);
BENCHMARK(test_deep_tree_100000_hhds);
//BENCHMARK(test_deep_tree_100000_lh);
BENCHMARK(test_deep_tree_1000000_hhds);
//BENCHMARK(test_deep_tree_1000000_lh);
BENCHMARK(test_deep_tree_10000000_hhds);
//BENCHMARK(test_deep_tree_10000000_lh);

// Run the benchmarks
BENCHMARK_MAIN();
