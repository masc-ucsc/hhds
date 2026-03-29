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

void build_hhds_tree(hhds::Tree& tree, std::vector<int>& values, int num_nodes) {
  auto current = hhds_test::add_root(tree, values, generate_random_int(generator, 1, 100));
  for (int i = 0; i < num_nodes; ++i) {
    current = hhds_test::add_child(tree, values, current, generate_random_int(generator, 1, 100));
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

#define DEFINE_TRAVERSAL_DEEP_BENCH(NAME, COUNT)                \
  void test_deep_tree_##NAME##_hhds(benchmark::State& state) {  \
    auto            tree = hhds::Tree::create();                \
    std::vector<int> values;                                    \
    build_hhds_tree(*tree, values, COUNT);                      \
    for (auto _ : state) {                                      \
      preorder_traversal_hhds(*tree);                           \
    }                                                           \
  }

DEFINE_TRAVERSAL_DEEP_BENCH(10, 10)
DEFINE_TRAVERSAL_DEEP_BENCH(100, 100)
DEFINE_TRAVERSAL_DEEP_BENCH(1000, 1000)
DEFINE_TRAVERSAL_DEEP_BENCH(10000, 10000)
DEFINE_TRAVERSAL_DEEP_BENCH(100000, 100000)
DEFINE_TRAVERSAL_DEEP_BENCH(1000000, 1000000)
DEFINE_TRAVERSAL_DEEP_BENCH(10000000, 10000000)

BENCHMARK(test_deep_tree_10_hhds);
BENCHMARK(test_deep_tree_100_hhds);
BENCHMARK(test_deep_tree_1000_hhds);
BENCHMARK(test_deep_tree_10000_hhds);
BENCHMARK(test_deep_tree_100000_hhds);
BENCHMARK(test_deep_tree_1000000_hhds);
BENCHMARK(test_deep_tree_10000000_hhds);

BENCHMARK_MAIN();
