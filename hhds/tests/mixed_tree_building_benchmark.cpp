#include <benchmark/benchmark.h>

#include <chrono>
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

void build_mixed_tree(hhds::Tree& tree, int depth_val) {
  auto root = tree.add_root_node();

  std::vector<hhds::Tree::Node_class> current_level{root};

  for (int depth = 0; depth < depth_val; ++depth) {
    std::vector<hhds::Tree::Node_class> next_level;

    for (auto node : current_level) {
      int num_children = generate_random_int(generator, 4, 10);

      if (num_children > 0) {
        auto first_child = tree.add_child(node);
        next_level.push_back(first_child);

        auto current_sibling = first_child;
        for (int i = 1; i < num_children; ++i) {
          if (generate_random_int(generator, 1, 100) <= 70) {
            current_sibling = tree.add_child(node);
          } else {
            current_sibling = tree.insert_next_sibling(current_sibling);
          }
          next_level.push_back(current_sibling);
        }
      }
    }

    current_level = std::move(next_level);
  }
}

void test_mixed_tree_building_depth_4(benchmark::State& state) {
  for (auto _ : state) {
    auto tree = hhds::Tree::create();
    build_mixed_tree(*tree, 4);
    benchmark::DoNotOptimize(tree);
  }
}

void test_mixed_tree_building_depth_5(benchmark::State& state) {
  for (auto _ : state) {
    auto tree = hhds::Tree::create();
    build_mixed_tree(*tree, 5);
    benchmark::DoNotOptimize(tree);
  }
}

void test_mixed_tree_building_depth_6(benchmark::State& state) {
  for (auto _ : state) {
    auto tree = hhds::Tree::create();
    build_mixed_tree(*tree, 6);
    benchmark::DoNotOptimize(tree);
  }
}

BENCHMARK(test_mixed_tree_building_depth_4);
BENCHMARK(test_mixed_tree_building_depth_5);
BENCHMARK(test_mixed_tree_building_depth_6);

BENCHMARK_MAIN();
