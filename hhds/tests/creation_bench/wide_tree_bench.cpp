#include <benchmark/benchmark.h>

#include <chrono>
#include <random>

#include "lhtree.hpp"
#include "tree.hpp"

auto                       now          = std::chrono::high_resolution_clock::now();
auto                       microseconds = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();
std::default_random_engine generator(microseconds);

int generate_random_int(std::default_random_engine& generator, int min, int max) {
  std::uniform_int_distribution<int> distribution(min, max);
  return distribution(generator);
}

void build_hhds_tree(hhds::Tree& hhds_tree, int num_nodes) {
  auto root = hhds_tree.add_root_node();

  for (int i = 0; i < num_nodes; ++i) {
    benchmark::DoNotOptimize(generate_random_int(generator, 1, 100));
    hhds_tree.add_child(root);
  }
}

void build_lh_tree(lh::tree<int>& lh_tree, int num_nodes) {
  lh_tree.set_root(generate_random_int(generator, 1, 100));
  lh::Tree_index lh_current(0, 0);

  for (int i = 0; i < num_nodes; ++i) {
    lh_tree.add_child(lh_current, generate_random_int(generator, 1, 100));
  }
}

#define HHDS_BUILD_WIDE_CASE(width)                 \
  void test_wide_tree_##width##_hhds(benchmark::State& state) { \
    for (auto _ : state) {                          \
      hhds::Tree hhds_tree;                         \
      build_hhds_tree(hhds_tree, width);            \
      benchmark::ClobberMemory();                   \
    }                                               \
  }                                                 \
  void test_wide_tree_##width##_lh(benchmark::State& state) {   \
    for (auto _ : state) {                          \
      lh::tree<int> lh_tree;                        \
      build_lh_tree(lh_tree, width);                \
      benchmark::ClobberMemory();                   \
    }                                               \
  }

HHDS_BUILD_WIDE_CASE(10)
HHDS_BUILD_WIDE_CASE(100)
HHDS_BUILD_WIDE_CASE(1000)
HHDS_BUILD_WIDE_CASE(10000)
HHDS_BUILD_WIDE_CASE(100000)
HHDS_BUILD_WIDE_CASE(1000000)
HHDS_BUILD_WIDE_CASE(10000000)

BENCHMARK(test_wide_tree_10_hhds);
// BENCHMARK(test_wide_tree_10_lh);
BENCHMARK(test_wide_tree_100_hhds);
// BENCHMARK(test_wide_tree_100_lh);
BENCHMARK(test_wide_tree_1000_hhds);
// BENCHMARK(test_wide_tree_1000_lh);
BENCHMARK(test_wide_tree_10000_hhds);
// BENCHMARK(test_wide_tree_10000_lh);
BENCHMARK(test_wide_tree_100000_hhds);
// BENCHMARK(test_wide_tree_100000_lh);
BENCHMARK(test_wide_tree_1000000_hhds);
// BENCHMARK(test_wide_tree_1000000_lh);
BENCHMARK(test_wide_tree_10000000_hhds);
// BENCHMARK(test_wide_tree_10000000_lh);

BENCHMARK_MAIN();
