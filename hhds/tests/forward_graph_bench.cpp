#include <benchmark/benchmark.h>

#include <memory>
#include <utility>
#include <vector>

#include "graph.hpp"

namespace {

// Flat chain: in -> n0 -> n1 -> ... -> n(N-1) -> out. Optionally add a
// back-edge n(N-1) -> n0 to close a cycle.
std::shared_ptr<hhds::Graph> build_chain(hhds::GraphLibrary& lib, std::string_view name, int n, bool close_loop) {
  auto gio = lib.create_io(name);
  gio->add_input("in", 0);
  gio->add_output("out", 0);
  auto graph = gio->create_graph();

  std::vector<hhds::Node> nodes;
  nodes.reserve(static_cast<size_t>(n));
  for (int i = 0; i < n; ++i) {
    nodes.push_back(graph->create_node());
  }

  graph->get_input_pin("in").connect_sink(nodes[0].create_sink_pin());
  for (int i = 0; i + 1 < n; ++i) {
    nodes[i].create_driver_pin().connect_sink(nodes[i + 1].create_sink_pin());
  }
  nodes[n - 1].create_driver_pin().connect_sink(graph->get_output_pin("out"));
  if (close_loop) {
    nodes[n - 1].create_driver_pin().connect_sink(nodes[0].create_sink_pin());
  }
  return graph;
}

// Hierarchical: top has K instances of one shared sub-graph. Each sub is a
// chain of M nodes (in -> n0 -> ... -> n(M-1) -> out). Top instances are
// also chained through the sub's i/o pins.
std::shared_ptr<hhds::Graph> build_hier(hhds::GraphLibrary& lib, int k_outer, int m_inner, bool inner_loop) {
  auto sub_gio = lib.create_io("sub");
  sub_gio->add_input("i", 0);
  sub_gio->add_output("o", 0);
  auto sub = sub_gio->create_graph();
  {
    std::vector<hhds::Node> inner;
    inner.reserve(static_cast<size_t>(m_inner));
    for (int i = 0; i < m_inner; ++i) {
      inner.push_back(sub->create_node());
    }
    sub->get_input_pin("i").connect_sink(inner[0].create_sink_pin());
    for (int i = 0; i + 1 < m_inner; ++i) {
      inner[i].create_driver_pin().connect_sink(inner[i + 1].create_sink_pin());
    }
    inner[m_inner - 1].create_driver_pin().connect_sink(sub->get_output_pin("o"));
    if (inner_loop) {
      inner[m_inner - 1].create_driver_pin().connect_sink(inner[0].create_sink_pin());
    }
  }

  auto top_gio = lib.create_io("top");
  top_gio->add_input("in", 0);
  top_gio->add_output("out", 0);
  auto top = top_gio->create_graph();

  std::vector<hhds::Node> inst;
  inst.reserve(static_cast<size_t>(k_outer));
  for (int i = 0; i < k_outer; ++i) {
    auto n = top->create_node();
    n.set_subnode(sub_gio);
    inst.push_back(n);
  }

  top->get_input_pin("in").connect_sink(inst[0].create_sink_pin("i"));
  for (int i = 0; i + 1 < k_outer; ++i) {
    inst[i].create_driver_pin("o").connect_sink(inst[i + 1].create_sink_pin("i"));
  }
  inst[k_outer - 1].create_driver_pin("o").connect_sink(top->get_output_pin("out"));
  return top;
}

template <typename Span>
void drain(Span&& span) {
  int cnt = 0;
  for (auto node : span) {
    benchmark::DoNotOptimize(node);
    ++cnt;
  }
  benchmark::DoNotOptimize(cnt);
}

// Each iteration rebuilds a fresh graph so the traversal cache is cold and
// the measurement reflects the actual topo-sort/ordering cost, not a warm
// cached span lookup. PauseTiming/ResumeTiming excludes construction.
#define FWD_CHAIN_BENCH(NAME, N, LOOP)                                      \
  void bench_forward_class_##NAME(benchmark::State& state) {                \
    for (auto _ : state) {                                                  \
      state.PauseTiming();                                                  \
      hhds::GraphLibrary lib;                                               \
      auto               g = build_chain(lib, "top", N, LOOP);              \
      state.ResumeTiming();                                                 \
      drain(g->forward_class());                                            \
    }                                                                       \
  }                                                                         \
  BENCHMARK(bench_forward_class_##NAME);                                    \
  void bench_fast_class_##NAME(benchmark::State& state) {                   \
    for (auto _ : state) {                                                  \
      state.PauseTiming();                                                  \
      hhds::GraphLibrary lib;                                               \
      auto               g = build_chain(lib, "top", N, LOOP);              \
      state.ResumeTiming();                                                 \
      drain(g->fast_class());                                               \
    }                                                                       \
  }                                                                         \
  BENCHMARK(bench_fast_class_##NAME);

FWD_CHAIN_BENCH(chain_1k_straight, 1000, false)
FWD_CHAIN_BENCH(chain_1k_loop, 1000, true)

#define FWD_HIER_BENCH(NAME, K, M, LOOP)                                   \
  void bench_forward_flat_##NAME(benchmark::State& state) {                \
    for (auto _ : state) {                                                 \
      state.PauseTiming();                                                 \
      hhds::GraphLibrary lib;                                              \
      auto               top = build_hier(lib, K, M, LOOP);                \
      state.ResumeTiming();                                                \
      drain(top->forward_flat());                                          \
    }                                                                      \
  }                                                                        \
  BENCHMARK(bench_forward_flat_##NAME);                                    \
  void bench_fast_flat_##NAME(benchmark::State& state) {                   \
    for (auto _ : state) {                                                 \
      state.PauseTiming();                                                 \
      hhds::GraphLibrary lib;                                              \
      auto               top = build_hier(lib, K, M, LOOP);                \
      state.ResumeTiming();                                                \
      drain(top->fast_flat());                                             \
    }                                                                      \
  }                                                                        \
  BENCHMARK(bench_fast_flat_##NAME);                                       \
  void bench_forward_hier_##NAME(benchmark::State& state) {                \
    for (auto _ : state) {                                                 \
      state.PauseTiming();                                                 \
      hhds::GraphLibrary lib;                                              \
      auto               top = build_hier(lib, K, M, LOOP);                \
      state.ResumeTiming();                                                \
      drain(top->forward_hier());                                          \
    }                                                                      \
  }                                                                        \
  BENCHMARK(bench_forward_hier_##NAME);                                    \
  void bench_fast_hier_##NAME(benchmark::State& state) {                   \
    for (auto _ : state) {                                                 \
      state.PauseTiming();                                                 \
      hhds::GraphLibrary lib;                                              \
      auto               top = build_hier(lib, K, M, LOOP);                \
      state.ResumeTiming();                                                \
      drain(top->fast_hier());                                             \
    }                                                                      \
  }                                                                        \
  BENCHMARK(bench_fast_hier_##NAME);

FWD_HIER_BENCH(hier_100x100_straight, 100, 100, false)
FWD_HIER_BENCH(hier_100x100_loop, 100, 100, true)

}  // namespace

BENCHMARK_MAIN();
