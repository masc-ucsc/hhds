#include <benchmark/benchmark.h>

#include <chrono>
#include <cstdint>
#include <random>
#include <vector>

#include "tree.hpp"

auto                       now          = std::chrono::high_resolution_clock::now();
auto                       microseconds = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();
std::default_random_engine generator(microseconds);

int generate_random_int(std::default_random_engine& generator, int min, int max) {
  std::uniform_int_distribution<int> distribution(min, max);
  return distribution(generator);
}

void build_hhds_tree(hhds::Tree& tree, int depth_val) {
  auto root = tree.add_root_node();

  std::vector<hhds::Tree::Node_class> current_level{root};

  for (int depth = 0; depth < depth_val; ++depth) {
    std::vector<hhds::Tree::Node_class> next_level;

    for (auto node : current_level) {
      int num_children = generate_random_int(generator, 3, 14);
      for (int i = 0; i < num_children; ++i) {
        next_level.push_back(tree.add_child(node));
      }
    }

    current_level = std::move(next_level);
  }
}

uint64_t test_hhds_navigation(const hhds::Tree& tree) {
  uint64_t operation_count = 0;

  for (auto node : tree.pre_order()) {
    if (hhds::Tree::is_valid(tree.get_parent(node))) {
      operation_count++;
    }

    if (hhds::Tree::is_valid(tree.get_first_child(node))) {
      operation_count++;
    }

    if (hhds::Tree::is_valid(tree.get_last_child(node))) {
      operation_count++;
    }

    if (hhds::Tree::is_valid(tree.get_sibling_next(node))) {
      operation_count++;
    }

    if (hhds::Tree::is_valid(tree.get_sibling_prev(node))) {
      operation_count++;
    }
  }

  return operation_count;
}

void test_chip_typical_navigation_depth_2(benchmark::State& state) {
  hhds::Tree tree;
  build_hhds_tree(tree, 2);

  for (auto _ : state) {
    benchmark::DoNotOptimize(test_hhds_navigation(tree));
  }
}

void test_chip_typical_navigation_depth_3(benchmark::State& state) {
  hhds::Tree tree;
  build_hhds_tree(tree, 3);

  for (auto _ : state) {
    benchmark::DoNotOptimize(test_hhds_navigation(tree));
  }
}

void test_chip_typical_navigation_depth_4(benchmark::State& state) {
  hhds::Tree tree;
  build_hhds_tree(tree, 4);

  for (auto _ : state) {
    benchmark::DoNotOptimize(test_hhds_navigation(tree));
  }
}

void test_chip_typical_navigation_depth_5(benchmark::State& state) {
  hhds::Tree tree;
  build_hhds_tree(tree, 5);

  for (auto _ : state) {
    benchmark::DoNotOptimize(test_hhds_navigation(tree));
  }
}

BENCHMARK(test_chip_typical_navigation_depth_2);
BENCHMARK(test_chip_typical_navigation_depth_3);
BENCHMARK(test_chip_typical_navigation_depth_4);
BENCHMARK(test_chip_typical_navigation_depth_5);

BENCHMARK_MAIN();
