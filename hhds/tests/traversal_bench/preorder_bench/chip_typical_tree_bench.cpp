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

void build_hhds_tree(hhds::tree<int>& hhds_tree, int depth_val) {
    auto hhds_root = hhds_tree.add_root(0);

    std::vector<hhds::Tree_pos> hhds_current_level{hhds_root};

    int id = 1;
    for (int depth = 0; depth < depth_val; ++depth) {
        std::vector<hhds::Tree_pos> hhds_next_level;
        std::vector<std::vector<int>> level_data;

        for (auto hhds_node : hhds_current_level) {
            int num_children = generate_random_int(generator, 1, 7);
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
            int num_children = generate_random_int(generator, 1, 7);
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

// Preorder traversal for hhds::tree
void preorder_traversal_hhds(hhds::tree<int>& tree) {
    int cnt = 0;
    for (const auto& node : tree.pre_order()) {
        // result.push_back(tree[node]);
        cnt++;
    }
}

// Preorder traversal for lh::tree
void preorder_traversal_lhtree(lh::tree<int>& tree) {
    auto root_index = lh::Tree_index(0, 0);
    int cnt = 0;
    typename lh::tree<int>::Tree_depth_preorder_iterator it(root_index, &tree);
    for (auto node_it = it.begin(); node_it != it.end(); ++node_it) {
        // result.push_back(tree.get_data(*node_it));
        cnt++;
    }
}

// Tree that is 1 nodes chip_typical
void test_chip_typical_tree_1_hhds(benchmark::State& state) {
    int num_nodes = 1;
    hhds::tree<int> hhds_tree;
    build_hhds_tree(hhds_tree, num_nodes);
    for (auto _ : state) {
        preorder_traversal_hhds(hhds_tree);
    }
}
void test_chip_typical_tree_1_lh(benchmark::State& state) {
    int num_nodes = 1;
    lh::tree<int> lh_tree;
    build_lh_tree(lh_tree, num_nodes);
    for (auto _ : state) {
        preorder_traversal_lhtree(lh_tree);
    }
}

// Tree that is 2 nodes chip_typical
void test_chip_typical_tree_2_hhds(benchmark::State& state) {
    int num_nodes = 2;
    hhds::tree<int> hhds_tree;
    build_hhds_tree(hhds_tree, num_nodes);
    for (auto _ : state) {
        preorder_traversal_hhds(hhds_tree);
    }
}
void test_chip_typical_tree_2_lh(benchmark::State& state) {
    int num_nodes = 2;
    lh::tree<int> lh_tree;
    build_lh_tree(lh_tree, num_nodes);
    for (auto _ : state) {
        preorder_traversal_lhtree(lh_tree);
    }
}

// Tree that is 3 nodes chip_typical
void test_chip_typical_tree_3_hhds(benchmark::State& state) {
    int num_nodes = 3;
    hhds::tree<int> hhds_tree;
    build_hhds_tree(hhds_tree, num_nodes);
    for (auto _ : state) {
        preorder_traversal_hhds(hhds_tree);
    }
}
void test_chip_typical_tree_3_lh(benchmark::State& state) {
    int num_nodes = 3;
    lh::tree<int> lh_tree;
    build_lh_tree(lh_tree, num_nodes);
    for (auto _ : state) {
        preorder_traversal_lhtree(lh_tree);
    }
}

// Tree that is 4 nodes chip_typical
void test_chip_typical_tree_4_hhds(benchmark::State& state) {
    int num_nodes = 4;
    hhds::tree<int> hhds_tree;
    build_hhds_tree(hhds_tree, num_nodes);
    for (auto _ : state) {
        preorder_traversal_hhds(hhds_tree);
    }
}
void test_chip_typical_tree_4_lh(benchmark::State& state) {
    int num_nodes = 4;
    lh::tree<int> lh_tree;
    build_lh_tree(lh_tree, num_nodes);
    for (auto _ : state) {
        preorder_traversal_lhtree(lh_tree);
    }
}

// Tree that is 5 nodes chip_typical
void test_chip_typical_tree_5_hhds(benchmark::State& state) {
    int num_nodes = 5;
    hhds::tree<int> hhds_tree;
    build_hhds_tree(hhds_tree, num_nodes);
    for (auto _ : state) {
        preorder_traversal_hhds(hhds_tree);
    }
}
void test_chip_typical_tree_5_lh(benchmark::State& state) {
    int num_nodes = 5;
    lh::tree<int> lh_tree;
    build_lh_tree(lh_tree, num_nodes);
    for (auto _ : state) {
        preorder_traversal_lhtree(lh_tree);
    }
}

// Tree that is 6 nodes chip_typical
void test_chip_typical_tree_6_hhds(benchmark::State& state) {
    int num_nodes = 6;
    hhds::tree<int> hhds_tree;
    build_hhds_tree(hhds_tree, num_nodes);
    for (auto _ : state) {
        preorder_traversal_hhds(hhds_tree);
    }
}
void test_chip_typical_tree_6_lh(benchmark::State& state) {
    int num_nodes = 6;
    lh::tree<int> lh_tree;
    build_lh_tree(lh_tree, num_nodes);
    for (auto _ : state) {
        preorder_traversal_lhtree(lh_tree);
    }
}

// Tree that is 7 nodes chip_typical
void test_chip_typical_tree_7_hhds(benchmark::State& state) {
    int num_nodes = 7;
    hhds::tree<int> hhds_tree;
    build_hhds_tree(hhds_tree, num_nodes);
    for (auto _ : state) {
        preorder_traversal_hhds(hhds_tree);
    }
}
void test_chip_typical_tree_7_lh(benchmark::State& state) {
    int num_nodes = 7;
    lh::tree<int> lh_tree;
    build_lh_tree(lh_tree, num_nodes);
    for (auto _ : state) {
        preorder_traversal_lhtree(lh_tree);
    }
}

// Tree that is 8 nodes chip_typical
void test_chip_typical_tree_8_hhds(benchmark::State& state) {
    int num_nodes = 8;
    hhds::tree<int> hhds_tree;
    build_hhds_tree(hhds_tree, num_nodes);
    for (auto _ : state) {
        preorder_traversal_hhds(hhds_tree);
    }
}
void test_chip_typical_tree_8_lh(benchmark::State& state) {
    int num_nodes = 8;
    lh::tree<int> lh_tree;
    build_lh_tree(lh_tree, num_nodes);
    for (auto _ : state) {
        preorder_traversal_lhtree(lh_tree);
    }
}

// Benchmark registration
BENCHMARK(test_chip_typical_tree_1_hhds);
BENCHMARK(test_chip_typical_tree_1_lh);
BENCHMARK(test_chip_typical_tree_2_hhds);
BENCHMARK(test_chip_typical_tree_2_lh);
BENCHMARK(test_chip_typical_tree_3_hhds);
BENCHMARK(test_chip_typical_tree_3_lh);
BENCHMARK(test_chip_typical_tree_4_hhds);
BENCHMARK(test_chip_typical_tree_4_lh);
BENCHMARK(test_chip_typical_tree_5_hhds);
BENCHMARK(test_chip_typical_tree_5_lh);
BENCHMARK(test_chip_typical_tree_6_hhds);
BENCHMARK(test_chip_typical_tree_6_lh);
BENCHMARK(test_chip_typical_tree_7_hhds)->Iterations(5);
BENCHMARK(test_chip_typical_tree_7_lh)->Iterations(5);
BENCHMARK(test_chip_typical_tree_8_hhds)->Iterations(5);
BENCHMARK(test_chip_typical_tree_8_lh)->Iterations(5);

// Run the benchmarks
BENCHMARK_MAIN();
