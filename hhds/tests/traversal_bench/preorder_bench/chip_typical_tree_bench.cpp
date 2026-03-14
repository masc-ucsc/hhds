#include <benchmark/benchmark.h>

#include <chrono>
#include <random>
#include <vector>

#include "lhtree.hpp"
#include "tests/tree_test_utils.hpp"
#include "tree.hpp"

auto                       now          = std::chrono::high_resolution_clock::now();
auto                       microseconds = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();
std::default_random_engine generator(microseconds);

int generate_random_int(std::default_random_engine& generator, int min, int max) {
  std::uniform_int_distribution<int> distribution(min, max);
  return distribution(generator);
}

void build_hhds_tree(hhds::Tree& tree, std::vector<int>& values, int depth_val) {
  auto current_level = std::vector<hhds::Tree::Node_class>{hhds_test::add_root(tree, values, 0)};
  int  id            = 1;
  for (int depth = 0; depth < depth_val; ++depth) {
    std::vector<hhds::Tree::Node_class> next_level;
    for (auto node : current_level) {
      const int num_children = generate_random_int(generator, 1, 7);
      for (int i = 0; i < num_children; ++i) {
        next_level.push_back(hhds_test::add_child(tree, values, node, id++));
      }
    }
    current_level = next_level;
  }
}

void build_lh_tree(lh::tree<int>& lh_tree, int depth_val) {
  lh_tree.set_root(0);
  std::vector<lh::Tree_index> current_level{lh::Tree_index(0, 0)};
  int                         id = 1;
  for (int depth = 0; depth < depth_val; ++depth) {
    std::vector<lh::Tree_index> next_level;
    for (auto node : current_level) {
      const int num_children = generate_random_int(generator, 1, 7);
      for (int i = 0; i < num_children; ++i) {
        next_level.push_back(lh_tree.add_child(node, id++));
      }
    }
    current_level = next_level;
  }
}

void preorder_traversal_hhds(hhds::Tree& tree) {
  int cnt = 0;
  for (auto node : tree.pre_order()) {
    benchmark::DoNotOptimize(node);
    ++cnt;
  }
  benchmark::DoNotOptimize(cnt);
}

void preorder_traversal_lhtree(lh::tree<int>& tree) {
  auto                                                 root_index = lh::Tree_index(0, 0);
  typename lh::tree<int>::Tree_depth_preorder_iterator it(root_index, &tree);
  int                                                  cnt = 0;
  for (auto node_it = it.begin(); node_it != it.end(); ++node_it) {
    ++cnt;
  }
  benchmark::DoNotOptimize(cnt);
}

#define DEFINE_TRAVERSAL_CHIP_BENCH(NAME, COUNT)                 \
  void test_chip_typical_tree_##NAME##_hhds(benchmark::State& s) { \
    hhds::Tree      tree;                                        \
    std::vector<int> values;                                     \
    build_hhds_tree(tree, values, COUNT);                        \
    for (auto _ : s) {                                           \
      preorder_traversal_hhds(tree);                             \
    }                                                            \
  }                                                              \
  void test_chip_typical_tree_##NAME##_lh(benchmark::State& s) { \
    lh::tree<int> tree;                                          \
    build_lh_tree(tree, COUNT);                                  \
    for (auto _ : s) {                                           \
      preorder_traversal_lhtree(tree);                           \
    }                                                            \
  }

DEFINE_TRAVERSAL_CHIP_BENCH(1, 1)
DEFINE_TRAVERSAL_CHIP_BENCH(2, 2)
DEFINE_TRAVERSAL_CHIP_BENCH(3, 3)
DEFINE_TRAVERSAL_CHIP_BENCH(4, 4)
DEFINE_TRAVERSAL_CHIP_BENCH(5, 5)
DEFINE_TRAVERSAL_CHIP_BENCH(6, 6)
DEFINE_TRAVERSAL_CHIP_BENCH(7, 7)
DEFINE_TRAVERSAL_CHIP_BENCH(8, 8)

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
BENCHMARK(test_chip_typical_tree_7_hhds);
BENCHMARK(test_chip_typical_tree_7_lh);
BENCHMARK(test_chip_typical_tree_8_hhds);
BENCHMARK(test_chip_typical_tree_8_lh);

BENCHMARK_MAIN();
