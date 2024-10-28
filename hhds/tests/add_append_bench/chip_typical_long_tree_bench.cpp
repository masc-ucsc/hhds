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

void build_hhds_tree(hhds::tree<int>& hhds_tree, int depth_val) {
    auto hhds_root = hhds_tree.add_root(0);

    std::vector<hhds::Tree_pos> hhds_current_level{hhds_root};

    int id = 1;
    for (int depth = 0; depth < depth_val; ++depth) {
        std::vector<hhds::Tree_pos> hhds_next_level;
        std::vector<std::vector<int>> level_data;

        for (auto hhds_node : hhds_current_level) {
            int num_children = generate_random_int(generator, 1, 20);
            std::vector<int> children_data;

            for (int i = 0; i < num_children; ++i) {
                int data = id++;
                auto added = hhds_tree.add_child(hhds_node, data);

                hhds_next_level.push_back(added);
                children_data.push_back(data);
            }
            level_data.push_back(children_data);
        }

        hhds_current_level = hhds_next_level;
    }
}

void build_lh_tree(lh::tree<int>& lh_tree, int depth_val) {
    lh_tree.set_root(0);

    std::vector<lh::Tree_index> lh_current_level{lh::Tree_index(0, 0)};

    int id = 1;
    for (int depth = 0; depth < depth_val; ++depth) {
        std::vector<lh::Tree_index> lh_next_level;
        std::vector<std::vector<int>> level_data;

        for (auto lh_node : lh_current_level) {
            int num_children = generate_random_int(generator, 1, 20);
            std::vector<int> children_data;

            for (int i = 0; i < num_children; ++i) {
                int data = id++;
                auto added = lh_tree.add_child(lh_node, data);

                lh_next_level.push_back(added);
                children_data.push_back(data);
            }
            level_data.push_back(children_data);
        }

        lh_current_level = lh_next_level;
    }
}

// Tree that is 1 nodes chip_typical
void test_chip_typical_long_tree_1_hhds(benchmark::State& state) {
    int depth_val = 1;
    for (auto _ : state) {
        hhds::tree<int> hhds_tree;
        build_hhds_tree(hhds_tree, depth_val);
    }
}
void test_chip_typical_long_tree_1_lh(benchmark::State& state) {
    int depth_val = 1;
    for (auto _ : state) {
        lh::tree<int> lh_tree;
        build_lh_tree(lh_tree, depth_val);
    }
}

// Tree that is 2 nodes chip_typical
void test_chip_typical_long_tree_2_hhds(benchmark::State& state) {
    int depth_val = 2;
    for (auto _ : state) {
        hhds::tree<int> hhds_tree;
        build_hhds_tree(hhds_tree, depth_val);
    }
}
void test_chip_typical_long_tree_2_lh(benchmark::State& state) {
    int depth_val = 2;
    for (auto _ : state) {
        lh::tree<int> lh_tree;
        build_lh_tree(lh_tree, depth_val);
    }
}

// Tree that is 3 nodes chip_typical
void test_chip_typical_long_tree_3_hhds(benchmark::State& state) {
    int depth_val = 3;
    for (auto _ : state) {
        hhds::tree<int> hhds_tree;
        build_hhds_tree(hhds_tree, depth_val);
    }
}
void test_chip_typical_long_tree_3_lh(benchmark::State& state) {
    int depth_val = 3;
    for (auto _ : state) {
        lh::tree<int> lh_tree;
        build_lh_tree(lh_tree, depth_val);
    }
}

// Tree that is 4 nodes chip_typical
void test_chip_typical_long_tree_4_hhds(benchmark::State& state) {
    int depth_val = 4;
    for (auto _ : state) {
        hhds::tree<int> hhds_tree;
        build_hhds_tree(hhds_tree, depth_val);
    }
}
void test_chip_typical_long_tree_4_lh(benchmark::State& state) {
    int depth_val = 4;
    for (auto _ : state) {
        lh::tree<int> lh_tree;
        build_lh_tree(lh_tree, depth_val);
    }
}

// Tree that is 5 nodes chip_typical
void test_chip_typical_long_tree_5_hhds(benchmark::State& state) {
    int depth_val = 5;
    for (auto _ : state) {
        hhds::tree<int> hhds_tree;
        build_hhds_tree(hhds_tree, depth_val);
    }
}
void test_chip_typical_long_tree_5_lh(benchmark::State& state) {
    int depth_val = 5;
    for (auto _ : state) {
        lh::tree<int> lh_tree;
        build_lh_tree(lh_tree, depth_val);
    }
}

// Tree that is 6 nodes chip_typical
void test_chip_typical_long_tree_6_hhds(benchmark::State& state) {
    int depth_val = 6;
    for (auto _ : state) {
        hhds::tree<int> hhds_tree;
        build_hhds_tree(hhds_tree, depth_val);
    }
}
void test_chip_typical_long_tree_6_lh(benchmark::State& state) {
    int depth_val = 6;
    for (auto _ : state) {
        lh::tree<int> lh_tree;
        build_lh_tree(lh_tree, depth_val);
    }
}

// Tree that is 7 nodes chip_typical
void test_chip_typical_long_tree_7_hhds(benchmark::State& state) {
    int depth_val = 7;
    for (auto _ : state) {
        hhds::tree<int> hhds_tree;
        build_hhds_tree(hhds_tree, depth_val);
    }
}
void test_chip_typical_long_tree_7_lh(benchmark::State& state) {
    int depth_val = 7;
    for (auto _ : state) {
        lh::tree<int> lh_tree;
        build_lh_tree(lh_tree, depth_val);
    }
}

// Tree that is 8 nodes chip_typical
void test_chip_typical_long_tree_8_hhds(benchmark::State& state) {
    int depth_val = 8;
    for (auto _ : state) {
        hhds::tree<int> hhds_tree;
        build_hhds_tree(hhds_tree, depth_val);
    }
}
void test_chip_typical_long_tree_8_lh(benchmark::State& state) {
    int depth_val = 8;
    for (auto _ : state) {
        lh::tree<int> lh_tree;
        build_lh_tree(lh_tree, depth_val);
    }
}

// Benchmark registration
BENCHMARK(test_chip_typical_long_tree_1_hhds);
BENCHMARK(test_chip_typical_long_tree_1_lh);
BENCHMARK(test_chip_typical_long_tree_2_hhds);
BENCHMARK(test_chip_typical_long_tree_2_lh);
BENCHMARK(test_chip_typical_long_tree_3_hhds);
BENCHMARK(test_chip_typical_long_tree_3_lh);
BENCHMARK(test_chip_typical_long_tree_4_hhds);
BENCHMARK(test_chip_typical_long_tree_4_lh);
BENCHMARK(test_chip_typical_long_tree_5_hhds);
BENCHMARK(test_chip_typical_long_tree_5_lh);
// BENCHMARK(test_chip_typical_long_tree_6_hhds)->Iterations(3);
// BENCHMARK(test_chip_typical_long_tree_6_lh)->Iterations(3);
// BENCHMARK(test_chip_typical_long_tree_7_hhds)->Iterations(4);
// BENCHMARK(test_chip_typical_long_tree_7_lh)->Iterations(4);
// BENCHMARK(test_chip_typical_long_tree_8_hhds);
// BENCHMARK(test_chip_typical_long_tree_8_lh);

// Run the benchmarks
BENCHMARK_MAIN();
