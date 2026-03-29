#include <benchmark/benchmark.h>

#include <chrono>
#include <random>
#include <vector>

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
      const int num_children = generate_random_int(generator, 1, 20);
      for (int i = 0; i < num_children; ++i) {
        next_level.push_back(hhds_test::add_child(tree, values, node, id++));
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

#define DEFINE_TRAVERSAL_CHIP_LONG_BENCH(NAME, COUNT)                \
  void test_chip_typical_long_tree_##NAME##_hhds(benchmark::State& s) { \
    auto            tree = hhds::Tree::create();                     \
    std::vector<int> values;                                         \
    build_hhds_tree(*tree, values, COUNT);                           \
    for (auto _ : s) {                                               \
      preorder_traversal_hhds(*tree);                                \
    }                                                                \
  }

DEFINE_TRAVERSAL_CHIP_LONG_BENCH(1, 1)
DEFINE_TRAVERSAL_CHIP_LONG_BENCH(2, 2)
DEFINE_TRAVERSAL_CHIP_LONG_BENCH(3, 3)
DEFINE_TRAVERSAL_CHIP_LONG_BENCH(4, 4)
DEFINE_TRAVERSAL_CHIP_LONG_BENCH(5, 5)
DEFINE_TRAVERSAL_CHIP_LONG_BENCH(6, 6)
DEFINE_TRAVERSAL_CHIP_LONG_BENCH(7, 7)
DEFINE_TRAVERSAL_CHIP_LONG_BENCH(8, 8)

BENCHMARK(test_chip_typical_long_tree_1_hhds);
BENCHMARK(test_chip_typical_long_tree_2_hhds);
BENCHMARK(test_chip_typical_long_tree_3_hhds);
BENCHMARK(test_chip_typical_long_tree_4_hhds);
BENCHMARK(test_chip_typical_long_tree_5_hhds);

BENCHMARK_MAIN();
