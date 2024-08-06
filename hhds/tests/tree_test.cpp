// // This file is distributed under the BSD 3-Clause License. See LICENSE for details.

// #include <benchmark/benchmark.h>
// #include <iostream>
// #include <vector>
// #include <random>
// #include <malloc.h>
// #include <TreeDS/tree>

// #include "../tree.hpp"
// #include "../lhtree.hpp"

// size_t get_memory_usage() {
//     struct mallinfo info = mallinfo();
//     return info.uordblks; // Memory allocated in bytes
// }

// // Function to benchmark HHDS tree generation
// void test_rand_tree_hhds_tree(std::default_random_engine& generator) {
//     hhds::tree<int> my_tree;
//     std::uniform_int_distribution<int> rootDataDist(1, 100);
//     std::uniform_int_distribution<int> numChildrenDist(4, 16); // Adjust based on the level
//     std::uniform_int_distribution<int> leafChildrenDist(4, 8);
//     std::uniform_int_distribution<int> nodeDataDist(1, 100); // Random data for nodes

//     // Create root node
//     int rootData = rootDataDist(generator);
//     hhds::Tree_pos root = my_tree.add_root(rootData);

//     // Generate random tree
//     // Level 1
//     int numChildrenLevel1 = numChildrenDist(generator);
//     std::vector<hhds::Tree_pos> level1Children;
//     for (int i = 0; i < numChildrenLevel1; ++i) {
//         int data = nodeDataDist(generator);
//         hhds::Tree_pos child = my_tree.add_child(root, data);
//         level1Children.push_back(child);
//     }

//     // Level 2
//     std::vector<hhds::Tree_pos> level2Children;
//     for (hhds::Tree_pos parent : level1Children) {
//         int numChildrenLevel2 = numChildrenDist(generator);
//         for (int j = 0; j < numChildrenLevel2; ++j) {
//             int data = nodeDataDist(generator);
//             hhds::Tree_pos child = my_tree.add_child(parent, data);
//             level2Children.push_back(child);
//         }
//     }

//     // Level 3 (leaves)
//     for (hhds::Tree_pos parent : level2Children) {
//         int numChildrenLevel3 = leafChildrenDist(generator);
//         for (int k = 0; k < numChildrenLevel3; ++k) {
//             int data = nodeDataDist(generator);
//             my_tree.add_child(parent, data);
//         }
//     }
// }

// // Function to benchmark LH tree generation
// void test_rand_tree_lh_tree(std::default_random_engine& generator) {
//     lh::tree<int> my_tree;
//     std::uniform_int_distribution<int> rootDataDist(1, 100);
//     std::uniform_int_distribution<int> numChildrenDist(4, 16); // Adjust based on the level
//     std::uniform_int_distribution<int> leafChildrenDist(4, 8);
//     std::uniform_int_distribution<int> nodeDataDist(1, 100); // Random data for nodes

//     // Create root node
//     int rootData = rootDataDist(generator);
//     my_tree.set_root(rootData);
//     lh::Tree_index root(0, 0); // Assuming the root is at level 0, position 0

//     // Generate random tree
//     // Level 1
//     int numChildrenLevel1 = numChildrenDist(generator);
//     std::vector<lh::Tree_index> level1Children;
//     for (int i = 0; i < numChildrenLevel1; ++i) {
//         int data = nodeDataDist(generator);
//         lh::Tree_index child = my_tree.add_child(root, data);
//         level1Children.push_back(child);
//     }

//     // Level 2
//     std::vector<lh::Tree_index> level2Children;
//     for (lh::Tree_index parent : level1Children) {
//         int numChildrenLevel2 = numChildrenDist(generator);
//         for (int j = 0; j < numChildrenLevel2; ++j) {
//             int data = nodeDataDist(generator);
//             lh::Tree_index child = my_tree.add_child(parent, data);
//             level2Children.push_back(child);
//         }
//     }

//     // Level 3 (leaves)
//     for (lh::Tree_index parent : level2Children) {
//         int numChildrenLevel3 = leafChildrenDist(generator);
//         for (int k = 0; k < numChildrenLevel3; ++k) {
//             int data = nodeDataDist(generator);
//             my_tree.add_child(parent, data);
//         }
//     }
// }

// // template <typename T>
// // md::nary_tree<T> generate_random_generic_tree(std::default_random_engine& generator) {
// //     std::uniform_int_distribution<int> rootDataDist(1, 100);
// //     std::uniform_int_distribution<int> numChildrenDist(4, 16); // Adjust based on the level
// //     std::uniform_int_distribution<int> leafChildrenDist(4, 8);
// //     std::uniform_int_distribution<int> nodeDataDist(1, 100); // Random data for nodes

// //     md::nary_tree<T> tree;
// //     auto root = tree.insert_over(tree.begin(), rootDataDist(generator));
// //     assert(root != tree.end() && "Root node not created");

// //     // Level 1
// //     for (int i = 0; i < numChildrenDist(generator); ++i) {
// //         auto level1Child = tree.insert_over(root, nodeDataDist(generator));
// //         assert(level1Child != tree.end() && "Level 1 child not created");

// //         // Level 2
// //         for (int j = 0; j < numChildrenDist(generator); ++j) {
// //             auto level2Child = tree.insert_over(level1Child, nodeDataDist(generator));

// //             // Level 3 (leaves)
// //             for (int k = 0; k < leafChildrenDist(generator); ++k) {
// //                 tree.insert_over(level2Child, nodeDataDist(generator));
// //             }
// //         }
// //     }

// //     return tree;
// // }

// // void test_rand_tree_generic_tree(std::default_random_engine& generator) {
// //     generate_random_generic_tree<int>(generator);
// // }

// // Benchmark function for test_rand_tree_hhds_tree
// static void BM_TestRandTreeHHDS(benchmark::State& state) {
//     std::default_random_engine generator(0);
//     for (auto _ : state) {
//         // size_t before_memory = get_memory_usage();
//         test_rand_tree_hhds_tree(generator);
//         // size_t after_memory = get_memory_usage();
//         // std::cout << "Memory used (HHDS): " << after_memory - before_memory << " bytes" << std::endl;
//     }
// }

// // Benchmark function for test_rand_tree_lh_tree
// static void BM_TestRandTreeLH(benchmark::State& state) {
//     std::default_random_engine generator(0);
//     for (auto _ : state) {
//         // size_t before_memory = get_memory_usage();
//         test_rand_tree_lh_tree(generator);
//         // size_t after_memory = get_memory_usage();
//         // std::cout << "Memory used (LH) : " << after_memory - before_memory << " bytes" << std::endl;
//     }
// }

// // // Benchmark function for test_rand_tree_generic_tree
// // static void BM_TestRandTreeGeneric(benchmark::State& state) {
// //     std::default_random_engine generator(0);
// //     for (auto _ : state) {
// //         test_rand_tree_generic_tree(generator);
// //     }
// // }

// // Register the benchmarks
// BENCHMARK(BM_TestRandTreeHHDS);
// BENCHMARK(BM_TestRandTreeLH);
// // BENCHMARK(BM_TestRandTreeGeneric);

// // Run the benchmarks
// BENCHMARK_MAIN();
