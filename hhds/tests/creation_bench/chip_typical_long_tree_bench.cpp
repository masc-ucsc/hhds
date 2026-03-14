#include <benchmark/benchmark.h>

#include <chrono>
#include <random>
#include <vector>

#include "lhtree.hpp"
#include "tree.hpp"

auto                       now          = std::chrono::high_resolution_clock::now();
auto                       microseconds = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();
std::default_random_engine generator(microseconds);

int generate_random_int(std::default_random_engine& generator, int min, int max) {
  std::uniform_int_distribution<int> distribution(min, max);
  return distribution(generator);
}

void build_hhds_tree(hhds::Tree& hhds_tree, int depth_val) {
  std::vector<hhds::Tree::Node_class> current_level{hhds_tree.add_root_node()};

  for (int depth = 0; depth < depth_val; ++depth) {
    std::vector<hhds::Tree::Node_class> next_level;

    for (auto node : current_level) {
      int num_children = generate_random_int(generator, 1, 20);
      for (int i = 0; i < num_children; ++i) {
        benchmark::DoNotOptimize(generate_random_int(generator, 1, 100));
        next_level.push_back(hhds_tree.add_child(node));
      }
    }

    current_level = std::move(next_level);
  }
}

void build_lh_tree(lh::tree<int>& lh_tree, int depth_val) {
  lh_tree.set_root(0);
  std::vector<lh::Tree_index> current_level{lh::Tree_index(0, 0)};

  for (int depth = 0; depth < depth_val; ++depth) {
    std::vector<lh::Tree_index> next_level;

    for (auto node : current_level) {
      int num_children = generate_random_int(generator, 1, 20);
      for (int i = 0; i < num_children; ++i) {
        next_level.push_back(lh_tree.add_child(node, i + 1));
      }
    }

    current_level = std::move(next_level);
  }
}

#define HHDS_CREATE_CHIP_LONG_CASE(depth)          \
  void test_chip_typical_long_tree_##depth##_hhds(benchmark::State& state) { \
    for (auto _ : state) {                         \
      auto hhds_tree = hhds::Tree::create();       \
      build_hhds_tree(*hhds_tree, depth);          \
      benchmark::ClobberMemory();                  \
    }                                              \
  }                                                \
  void test_chip_typical_long_tree_##depth##_lh(benchmark::State& state) {   \
    for (auto _ : state) {                         \
      lh::tree<int> lh_tree;                       \
      build_lh_tree(lh_tree, depth);               \
      benchmark::ClobberMemory();                  \
    }                                              \
  }

HHDS_CREATE_CHIP_LONG_CASE(1)
HHDS_CREATE_CHIP_LONG_CASE(2)
HHDS_CREATE_CHIP_LONG_CASE(3)
HHDS_CREATE_CHIP_LONG_CASE(4)
HHDS_CREATE_CHIP_LONG_CASE(5)
HHDS_CREATE_CHIP_LONG_CASE(6)
HHDS_CREATE_CHIP_LONG_CASE(7)
HHDS_CREATE_CHIP_LONG_CASE(8)

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
BENCHMARK(test_chip_typical_long_tree_6_hhds);
BENCHMARK(test_chip_typical_long_tree_6_lh);
BENCHMARK(test_chip_typical_long_tree_7_hhds);
BENCHMARK(test_chip_typical_long_tree_7_lh);
BENCHMARK(test_chip_typical_long_tree_8_hhds);
BENCHMARK(test_chip_typical_long_tree_8_lh);
