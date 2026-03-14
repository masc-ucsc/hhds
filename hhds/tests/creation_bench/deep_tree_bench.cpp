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
  auto current = hhds_tree.add_root_node();

  for (int i = 0; i < num_nodes; ++i) {
    benchmark::DoNotOptimize(generate_random_int(generator, 1, 100));
    current = hhds_tree.add_child(current);
  }
}

void build_lh_tree(lh::tree<int>& lh_tree, int num_nodes) {
  lh_tree.set_root(generate_random_int(generator, 1, 100));
  lh::Tree_index lh_current(0, 0);

  for (int i = 0; i < num_nodes; ++i) {
    lh_current = lh_tree.add_child(lh_current, generate_random_int(generator, 1, 100));
  }
}

#define HHDS_BUILD_DEEP_CASE(depth)                 \
  void test_deep_tree_##depth##_hhds(benchmark::State& state) { \
    for (auto _ : state) {                          \
      hhds::Tree hhds_tree;                         \
      build_hhds_tree(hhds_tree, depth);            \
      benchmark::ClobberMemory();                   \
    }                                               \
  }                                                 \
  void test_deep_tree_##depth##_lh(benchmark::State& state) {   \
    for (auto _ : state) {                          \
      lh::tree<int> lh_tree;                        \
      build_lh_tree(lh_tree, depth);                \
      benchmark::ClobberMemory();                   \
    }                                               \
  }

HHDS_BUILD_DEEP_CASE(10)
HHDS_BUILD_DEEP_CASE(100)
HHDS_BUILD_DEEP_CASE(1000)
HHDS_BUILD_DEEP_CASE(10000)
HHDS_BUILD_DEEP_CASE(100000)
HHDS_BUILD_DEEP_CASE(1000000)
HHDS_BUILD_DEEP_CASE(10000000)

BENCHMARK(test_deep_tree_10_hhds);
// BENCHMARK(test_deep_tree_10_lh);
BENCHMARK(test_deep_tree_100_hhds);
// BENCHMARK(test_deep_tree_100_lh);
BENCHMARK(test_deep_tree_1000_hhds);
// BENCHMARK(test_deep_tree_1000_lh);
BENCHMARK(test_deep_tree_10000_hhds);
// BENCHMARK(test_deep_tree_10000_lh);
