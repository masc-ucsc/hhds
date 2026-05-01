// Benchmarks for Zhang-Shasha tree edit distance algorithm.
//
// Memory note: td and fd tables are (n+1)x(m+1) doubles.
// At n=m=2000: 2000*2000*8 = ~32MB — safe.
// At n=m=5000: ~200MB — borderline.
// At n=m=10000: ~800MB — OOM risk, avoided here.
//
// Safe limits used:
//   Balanced trees : up to 2000 nodes
//   Deep chains    : up to 1000 nodes  (worst case: n keyroots → O(n²))
//   Wide trees     : up to 5000 nodes  (few keyroots → fast)

#include <benchmark/benchmark.h>

#include <chrono>
#include <functional>
#include <memory>
#include <random>
#include <vector>

#include "tree.hpp"
#include "tree_edit_distance.hpp"

// ============================================================================
// Helper: create a balanced binary tree with roughly target_nodes nodes
// ============================================================================
static std::shared_ptr<hhds::Tree> create_balanced_tree(size_t target_nodes) {
  auto tree = hhds::Tree::create();
  auto root = tree->add_root_node();
  root.set_type(1);

  std::vector<hhds::Tree::Node_class> current_level = {root};
  size_t                              total_nodes    = 1;

  while (total_nodes < target_nodes && !current_level.empty()) {
    std::vector<hhds::Tree::Node_class> next_level;

    for (auto parent : current_level) {
      if (total_nodes >= target_nodes) {
        break;
      }

      // left child
      auto left = parent.add_child();
      left.set_type(2);
      next_level.push_back(left);
      ++total_nodes;

      if (total_nodes >= target_nodes) {
        break;
      }

      // right child — must be add_child() on parent, not append_sibling()
      auto right = parent.add_child();
      right.set_type(3);
      next_level.push_back(right);
      ++total_nodes;
    }

    current_level = std::move(next_level);
  }

  return tree;
}

// ============================================================================
// Helper: create a deep chain  root → c1 → c2 → … → c_depth
// ============================================================================
static std::shared_ptr<hhds::Tree> create_deep_chain_tree(size_t depth) {
  auto tree = hhds::Tree::create();
  auto node = tree->add_root_node();
  node.set_type(1);

  for (size_t i = 2; i <= depth; ++i) {
    auto child = node.add_child();
    child.set_type(static_cast<hhds::Type>(i % 256));
    node = child;
  }

  return tree;
}

// ============================================================================
// Helper: create a wide tree  root → [c1, c2, …, c_width]
// ============================================================================
static std::shared_ptr<hhds::Tree> create_wide_tree(size_t width) {
  auto tree = hhds::Tree::create();
  auto root = tree->add_root_node();
  root.set_type(1);

  for (size_t i = 0; i < width; ++i) {
    auto child = root.add_child();
    child.set_type(static_cast<hhds::Type>(2 + (i % 254)));
  }

  return tree;
}

// ============================================================================
// Helper: clone a tree and relabel the first `modify_count` nodes
// ============================================================================
static std::shared_ptr<hhds::Tree> create_relabeled_variant(const std::shared_ptr<hhds::Tree>& original,
                                                              int                                modify_count) {
  auto tree = hhds::Tree::create();

  std::function<void(hhds::Tree::Node_class, hhds::Tree::Node_class, int&)> clone
      = [&](hhds::Tree::Node_class src, hhds::Tree::Node_class dst, int& count) {
          auto child = src.first_child();
          while (child.is_valid()) {
            auto dst_child = dst.add_child();
            hhds::Type t   = child.get_type();
            dst_child.set_type(count++ < modify_count ? static_cast<hhds::Type>(t + 1) : t);
            clone(child, dst_child, count);
            child = child.next_sibling();
          }
        };

  auto root = tree->add_root_node();
  root.set_type(original->get_root_node().get_type());
  int count = 0;
  clone(original->get_root_node(), root, count);
  return tree;
}

// ============================================================================
// Benchmarks: Identical balanced trees
// ============================================================================

static void BM_Identical_Balanced_100(benchmark::State& state) {
  auto t1 = create_balanced_tree(100);
  auto t2 = create_balanced_tree(100);
  for (auto _ : state) {
    benchmark::DoNotOptimize(hhds::TreeEditDistance::compute(t1, t2));
  }
}
BENCHMARK(BM_Identical_Balanced_100)->Unit(benchmark::kMillisecond);

static void BM_Identical_Balanced_500(benchmark::State& state) {
  auto t1 = create_balanced_tree(500);
  auto t2 = create_balanced_tree(500);
  for (auto _ : state) {
    benchmark::DoNotOptimize(hhds::TreeEditDistance::compute(t1, t2));
  }
}
BENCHMARK(BM_Identical_Balanced_500)->Unit(benchmark::kMillisecond);

static void BM_Identical_Balanced_1K(benchmark::State& state) {
  auto t1 = create_balanced_tree(1000);
  auto t2 = create_balanced_tree(1000);
  for (auto _ : state) {
    benchmark::DoNotOptimize(hhds::TreeEditDistance::compute(t1, t2));
  }
}
BENCHMARK(BM_Identical_Balanced_1K)->Unit(benchmark::kMillisecond);

static void BM_Identical_Balanced_2K(benchmark::State& state) {
  auto t1 = create_balanced_tree(2000);
  auto t2 = create_balanced_tree(2000);
  for (auto _ : state) {
    benchmark::DoNotOptimize(hhds::TreeEditDistance::compute(t1, t2));
  }
}
BENCHMARK(BM_Identical_Balanced_2K)->Unit(benchmark::kMillisecond);

// ============================================================================
// Benchmarks: Single relabel (minimal difference)
// ============================================================================

static void BM_SingleRelabel_Balanced_1K(benchmark::State& state) {
  auto t1 = create_balanced_tree(1000);
  auto t2 = create_relabeled_variant(t1, 1);
  for (auto _ : state) {
    benchmark::DoNotOptimize(hhds::TreeEditDistance::compute(t1, t2));
  }
}
BENCHMARK(BM_SingleRelabel_Balanced_1K)->Unit(benchmark::kMillisecond);

static void BM_PartialRelabel_Balanced_1K(benchmark::State& state) {
  auto t1 = create_balanced_tree(1000);
  auto t2 = create_relabeled_variant(t1, 10);
  for (auto _ : state) {
    benchmark::DoNotOptimize(hhds::TreeEditDistance::compute(t1, t2));
  }
}
BENCHMARK(BM_PartialRelabel_Balanced_1K)->Unit(benchmark::kMillisecond);

// ============================================================================
// Benchmarks: Deep chain trees
// Worst case for Zhang-Shasha: n keyroots → O(n²) subproblems.
// Keep sizes small (≤1000).
// ============================================================================

static void BM_DeepChain_100(benchmark::State& state) {
  auto t1 = create_deep_chain_tree(100);
  auto t2 = create_deep_chain_tree(100);
  for (auto _ : state) {
    benchmark::DoNotOptimize(hhds::TreeEditDistance::compute(t1, t2));
  }
}
BENCHMARK(BM_DeepChain_100)->Unit(benchmark::kMillisecond);

static void BM_DeepChain_500(benchmark::State& state) {
  auto t1 = create_deep_chain_tree(500);
  auto t2 = create_deep_chain_tree(500);
  for (auto _ : state) {
    benchmark::DoNotOptimize(hhds::TreeEditDistance::compute(t1, t2));
  }
}
BENCHMARK(BM_DeepChain_500)->Unit(benchmark::kMillisecond);

static void BM_DeepChain_1K(benchmark::State& state) {
  auto t1 = create_deep_chain_tree(1000);
  auto t2 = create_deep_chain_tree(1000);
  for (auto _ : state) {
    benchmark::DoNotOptimize(hhds::TreeEditDistance::compute(t1, t2));
  }
}
BENCHMARK(BM_DeepChain_1K)->Unit(benchmark::kMillisecond);

// ============================================================================
// Benchmarks: Wide trees (many siblings, few keyroots → fast)
// ============================================================================

static void BM_Wide_1K(benchmark::State& state) {
  auto t1 = create_wide_tree(1000);
  auto t2 = create_wide_tree(1000);
  for (auto _ : state) {
    benchmark::DoNotOptimize(hhds::TreeEditDistance::compute(t1, t2));
  }
}
BENCHMARK(BM_Wide_1K)->Unit(benchmark::kMillisecond);

static void BM_Wide_3K(benchmark::State& state) {
  auto t1 = create_wide_tree(3000);
  auto t2 = create_wide_tree(3000);
  for (auto _ : state) {
    benchmark::DoNotOptimize(hhds::TreeEditDistance::compute(t1, t2));
  }
}
BENCHMARK(BM_Wide_3K)->Unit(benchmark::kMillisecond);

static void BM_Wide_5K(benchmark::State& state) {
  auto t1 = create_wide_tree(5000);
  auto t2 = create_wide_tree(5000);
  for (auto _ : state) {
    benchmark::DoNotOptimize(hhds::TreeEditDistance::compute(t1, t2));
  }
}
BENCHMARK(BM_Wide_5K)->Unit(benchmark::kMillisecond);

// ============================================================================
// Scaling benchmark: balanced trees, measure empirical complexity
// ============================================================================

static void BM_Scaling_Balanced(benchmark::State& state) {
  const size_t n = static_cast<size_t>(state.range(0));
  auto         t1 = create_balanced_tree(n);
  auto         t2 = create_balanced_tree(n);
  for (auto _ : state) {
    benchmark::DoNotOptimize(hhds::TreeEditDistance::compute(t1, t2));
  }
  state.SetComplexityN(static_cast<benchmark::ComplexityN>(n));
}
BENCHMARK(BM_Scaling_Balanced)
    ->RangeMultiplier(2)
    ->Range(64, 2000)
    ->Unit(benchmark::kMillisecond)
    ->Complexity(benchmark::oNSquared);

// ============================================================================
// Scaling benchmark: deep chains, expected O(n²)
// ============================================================================

static void BM_Scaling_DeepChain(benchmark::State& state) {
  const size_t n  = static_cast<size_t>(state.range(0));
  auto         t1 = create_deep_chain_tree(n);
  auto         t2 = create_deep_chain_tree(n);
  for (auto _ : state) {
    benchmark::DoNotOptimize(hhds::TreeEditDistance::compute(t1, t2));
  }
  state.SetComplexityN(static_cast<benchmark::ComplexityN>(n));
}
BENCHMARK(BM_Scaling_DeepChain)
    ->RangeMultiplier(2)
    ->Range(64, 1000)
    ->Unit(benchmark::kMillisecond)
    ->Complexity(benchmark::oNSquared);

BENCHMARK_MAIN();