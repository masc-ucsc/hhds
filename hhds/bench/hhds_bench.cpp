// Comparison bench for hhds. Same CLI and CSV schema as
// boost_bench / lgraph_bench_thesis. See bench/schema.md for the column
// contract; the run_all.sh wrapper fills in the memory/perf columns
// externally.

#include <benchmark/benchmark.h>

#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <random>
#include <string>
#include <string_view>
#include <vector>

#include "generator/graph_spec.hpp"
#include "graph.hpp"

namespace {

struct CliArgs {
  std::string         op           = "build_node";
  std::string         topology     = "chain";
  std::string         axis         = "nodes";
  int                 nodes        = 1000;
  int                 pins         = 1;
  int                 hier         = 1;
  int                 fanout_max   = 4;
  uint64_t            seed         = 0xC0FFEEULL;
  int                 runs         = 5;
  bool                emit_header  = true;  // false to append to a master CSV
  bool                verify       = false; // print visited count to stderr per run
};

// Global: set once by CLI parsing, read by each op AFTER its timer stops.
// Kept off the hot path so verification mode doesn't perturb measurements.
bool g_verify = false;

struct OpResult {
  int64_t wall_ns;
  int64_t visited;  // for build_*/add_pin/mutate/lookup: items processed; for traverse_*: nodes/instances visited
};

CliArgs parse_args(int argc, char** argv) {
  CliArgs a;
  for (int i = 1; i < argc; ++i) {
    std::string_view s = argv[i];
    auto eat = [&](std::string_view prefix, std::string& dst) -> bool {
      if (s.rfind(prefix, 0) == 0) {
        dst = std::string(s.substr(prefix.size()));
        return true;
      }
      return false;
    };
    auto eat_int = [&](std::string_view prefix, int& dst) -> bool {
      std::string buf;
      if (!eat(prefix, buf)) {
        return false;
      }
      dst = std::atoi(buf.c_str());
      return true;
    };
    auto eat_u64 = [&](std::string_view prefix, uint64_t& dst) -> bool {
      std::string buf;
      if (!eat(prefix, buf)) {
        return false;
      }
      dst = std::strtoull(buf.c_str(), nullptr, 0);
      return true;
    };
    if (eat("--op=", a.op)) continue;
    if (eat("--topology=", a.topology)) continue;
    if (eat("--axis=", a.axis)) continue;
    if (eat_int("--nodes=", a.nodes)) continue;
    if (eat_int("--pins=", a.pins)) continue;
    if (eat_int("--hier=", a.hier)) continue;
    if (eat_int("--fanout=", a.fanout_max)) continue;
    if (eat_u64("--seed=", a.seed)) continue;
    if (eat_int("--runs=", a.runs)) continue;
    if (s == "--no-header") {
      a.emit_header = false;
      continue;
    }
    if (s == "--verify") {
      a.verify = true;
      continue;
    }
    std::cerr << "hhds_bench: unknown arg: " << s << "\n";
    std::exit(2);
  }
  return a;
}

hhds_bench::GraphSpec spec_from(const CliArgs& a) {
  hhds_bench::GraphSpec s;
  s.nodes         = a.nodes;
  s.pins_per_node = a.pins;
  s.hier_size     = a.hier;
  s.fanout_max    = a.fanout_max;
  s.seed          = a.seed;
  s.topology      = hhds_bench::parse_topology(a.topology);
  return s;
}

// Materialize one (flat, single-graph) hhds Graph from an EdgeList +
// PinAssignment. Returns the graph plus the index->Node table so callers
// can do further work (mutate, lookup) without re-walking node_table.
struct BuiltGraph {
  hhds::GraphLibrary           lib;
  std::shared_ptr<hhds::Graph> graph;
  std::vector<hhds::Node>      nodes;
};

void materialize_flat(BuiltGraph& bg, const hhds_bench::GraphSpec& spec, const hhds_bench::EdgeList& edges,
                      const hhds_bench::PinAssignment& pins) {
  auto gio = bg.lib.create_io("top");
  gio->add_input("in", 0);
  gio->add_output("out", 0);
  bg.graph = gio->create_graph();

  bg.nodes.reserve(static_cast<size_t>(spec.nodes));
  for (int i = 0; i < spec.nodes; ++i) {
    bg.nodes.push_back(bg.graph->create_node());
  }

  // Edges are added even when the bench measures only build_node — callers
  // pass an empty edges.edges to skip this loop.
  for (size_t i = 0; i < edges.edges.size(); ++i) {
    const auto [src, dst] = edges.edges[i];
    if (pins.driver_ports.empty()) {
      // Pin-0 fast path: hhds avoids creating a separate pin entry.
      bg.nodes[src].create_driver_pin().connect_sink(bg.nodes[dst].create_sink_pin());
    } else {
      const auto dport = static_cast<hhds::Port_id>(pins.driver_ports[i]);
      const auto sport = static_cast<hhds::Port_id>(pins.sink_ports[i]);
      bg.nodes[src].create_driver_pin(dport).connect_sink(bg.nodes[dst].create_sink_pin(sport));
    }
  }

  // Anchor first/last to the declared graph in/out so iterators have real
  // sources/sinks. Without this, every node looks like a source to the
  // forward iterator and Pass-1 emits everything immediately.
  if (spec.nodes > 0) {
    bg.graph->get_input_pin("in").connect_sink(bg.nodes[0].create_sink_pin());
    if (!edges.edges.empty()) {
      bg.nodes[spec.nodes - 1].create_driver_pin().connect_sink(bg.graph->get_output_pin("out"));
    }
  }
}

// --- Operations ---

// Helper: compute (t1-t0) in nanoseconds.
inline int64_t ns_between(std::chrono::steady_clock::time_point t0, std::chrono::steady_clock::time_point t1) {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
}

OpResult op_build_node(const hhds_bench::GraphSpec& spec) {
  auto t0 = std::chrono::steady_clock::now();
  hhds::GraphLibrary lib;
  auto               gio = lib.create_io("top");
  auto               g   = gio->create_graph();
  int64_t            built = 0;
  for (int i = 0; i < spec.nodes; ++i) {
    auto n = g->create_node();
    benchmark::DoNotOptimize(n);
    ++built;
  }
  auto t1 = std::chrono::steady_clock::now();
  return {ns_between(t0, t1), built};
}

OpResult op_build_edges(const hhds_bench::GraphSpec& spec) {
  // Build nodes first (untimed), then time only the edge insertion phase.
  hhds_bench::EdgeList      edges = hhds_bench::make_edge_list(spec);
  hhds_bench::PinAssignment pins  = hhds_bench::make_pin_assignment(spec, edges);
  BuiltGraph                bg;
  auto                      gio = bg.lib.create_io("top");
  gio->add_input("in", 0);
  gio->add_output("out", 0);
  bg.graph = gio->create_graph();
  bg.nodes.reserve(static_cast<size_t>(spec.nodes));
  for (int i = 0; i < spec.nodes; ++i) {
    bg.nodes.push_back(bg.graph->create_node());
  }
  if (spec.nodes > 0) {
    bg.graph->get_input_pin("in").connect_sink(bg.nodes[0].create_sink_pin());
  }

  auto    t0      = std::chrono::steady_clock::now();
  int64_t added   = 0;
  for (size_t i = 0; i < edges.edges.size(); ++i) {
    const auto [src, dst] = edges.edges[i];
    if (pins.driver_ports.empty()) {
      bg.nodes[src].create_driver_pin().connect_sink(bg.nodes[dst].create_sink_pin());
    } else {
      const auto dport = static_cast<hhds::Port_id>(pins.driver_ports[i]);
      const auto sport = static_cast<hhds::Port_id>(pins.sink_ports[i]);
      bg.nodes[src].create_driver_pin(dport).connect_sink(bg.nodes[dst].create_sink_pin(sport));
    }
    ++added;
  }
  auto t1 = std::chrono::steady_clock::now();
  return {ns_between(t0, t1), added};
}

OpResult op_traverse_forward_class(const hhds_bench::GraphSpec& spec) {
  hhds_bench::EdgeList      edges = hhds_bench::make_edge_list(spec);
  hhds_bench::PinAssignment pins  = hhds_bench::make_pin_assignment(spec, edges);
  BuiltGraph                bg;
  materialize_flat(bg, spec, edges, pins);

  auto    t0    = std::chrono::steady_clock::now();
  int64_t count = 0;
  for (auto node : bg.graph->forward_class()) {
    benchmark::DoNotOptimize(node);
    ++count;
  }
  auto t1 = std::chrono::steady_clock::now();
  benchmark::DoNotOptimize(count);
  return {ns_between(t0, t1), count};
}

OpResult op_traverse_fast_class(const hhds_bench::GraphSpec& spec) {
  hhds_bench::EdgeList      edges = hhds_bench::make_edge_list(spec);
  hhds_bench::PinAssignment pins  = hhds_bench::make_pin_assignment(spec, edges);
  BuiltGraph                bg;
  materialize_flat(bg, spec, edges, pins);

  auto    t0    = std::chrono::steady_clock::now();
  int64_t count = 0;
  for (auto node : bg.graph->fast_class()) {
    benchmark::DoNotOptimize(node);
    ++count;
  }
  auto t1 = std::chrono::steady_clock::now();
  benchmark::DoNotOptimize(count);
  return {ns_between(t0, t1), count};
}

// Build top-level graph with spec.hier_size instances of one shared
// chain subgraph (spec.nodes inner nodes each). Returns the top graph;
// the GraphLibrary stays alive in `lib` (move-pinned, so callers must
// pass an existing BuiltGraph). Used by every hier traversal op so the
// build cost is identical across forward_flat/fast_flat/forward_hier/
// fast_hier/hier_range comparisons.
std::shared_ptr<hhds::Graph> materialize_hier(hhds::GraphLibrary& lib, const hhds_bench::GraphSpec& spec) {
  auto sub_gio = lib.create_io("sub");
  sub_gio->add_input("i", 0);
  sub_gio->add_output("o", 0);
  auto sub = sub_gio->create_graph();
  std::vector<hhds::Node> inner;
  inner.reserve(static_cast<size_t>(spec.nodes));
  for (int i = 0; i < spec.nodes; ++i) {
    inner.push_back(sub->create_node());
  }
  if (spec.nodes > 0) {
    sub->get_input_pin("i").connect_sink(inner[0].create_sink_pin());
    for (int i = 0; i + 1 < spec.nodes; ++i) {
      inner[i].create_driver_pin().connect_sink(inner[i + 1].create_sink_pin());
    }
    inner[spec.nodes - 1].create_driver_pin().connect_sink(sub->get_output_pin("o"));
  }

  auto top_gio = lib.create_io("top");
  top_gio->add_input("in", 0);
  top_gio->add_output("out", 0);
  auto top = top_gio->create_graph();
  std::vector<hhds::Node> inst;
  inst.reserve(static_cast<size_t>(spec.hier_size));
  for (int i = 0; i < spec.hier_size; ++i) {
    auto n = top->create_node();
    n.set_subnode(sub_gio);
    inst.push_back(n);
  }
  if (spec.hier_size > 0) {
    top->get_input_pin("in").connect_sink(inst[0].create_sink_pin("i"));
    for (int i = 0; i + 1 < spec.hier_size; ++i) {
      inst[i].create_driver_pin("o").connect_sink(inst[i + 1].create_sink_pin("i"));
    }
    inst[spec.hier_size - 1].create_driver_pin("o").connect_sink(top->get_output_pin("out"));
  }
  return top;
}

OpResult op_traverse_forward_flat(const hhds_bench::GraphSpec& spec) {
  hhds::GraphLibrary lib;
  auto               top = materialize_hier(lib, spec);

  auto    t0    = std::chrono::steady_clock::now();
  int64_t count = 0;
  for (auto node : top->forward_flat()) {
    benchmark::DoNotOptimize(node);
    ++count;
  }
  auto t1 = std::chrono::steady_clock::now();
  benchmark::DoNotOptimize(count);
  return {ns_between(t0, t1), count};
}

OpResult op_traverse_fast_flat(const hhds_bench::GraphSpec& spec) {
  hhds::GraphLibrary lib;
  auto               top = materialize_hier(lib, spec);

  auto    t0    = std::chrono::steady_clock::now();
  int64_t count = 0;
  for (auto node : top->fast_flat()) {
    benchmark::DoNotOptimize(node);
    ++count;
  }
  auto t1 = std::chrono::steady_clock::now();
  benchmark::DoNotOptimize(count);
  return {ns_between(t0, t1), count};
}

// op_add_pin: build N empty nodes (untimed), then time creating
// spec.pins_per_node driver pins + spec.pins_per_node sink pins on each
// node. Pins are created with explicit Port_id 0..K-1 so we exercise
// hhds's three storage regimes (inline -> overflow array -> emhash8 set)
// as the count crosses the 16/64 thresholds documented in graph.hpp:74.
OpResult op_add_pin(const hhds_bench::GraphSpec& spec) {
  hhds::GraphLibrary lib;
  auto               gio = lib.create_io("top");
  auto               g   = gio->create_graph();
  std::vector<hhds::Node> nodes;
  nodes.reserve(static_cast<size_t>(spec.nodes));
  for (int i = 0; i < spec.nodes; ++i) {
    nodes.push_back(g->create_node());
  }

  const int K = std::max(1, spec.pins_per_node);

  auto    t0      = std::chrono::steady_clock::now();
  int64_t pin_cnt = 0;
  for (int i = 0; i < spec.nodes; ++i) {
    for (int p = 0; p < K; ++p) {
      auto dp = nodes[i].create_driver_pin(static_cast<hhds::Port_id>(p));
      auto sp = nodes[i].create_sink_pin(static_cast<hhds::Port_id>(p));
      benchmark::DoNotOptimize(dp);
      benchmark::DoNotOptimize(sp);
      pin_cnt += 2;
    }
  }
  auto t1 = std::chrono::steady_clock::now();
  return {ns_between(t0, t1), pin_cnt};
}

OpResult op_traverse_forward_hier(const hhds_bench::GraphSpec& spec) {
  hhds::GraphLibrary lib;
  auto               top = materialize_hier(lib, spec);

  auto    t0    = std::chrono::steady_clock::now();
  int64_t count = 0;
  for (auto node : top->forward_hier()) {
    benchmark::DoNotOptimize(node);
    ++count;
  }
  auto t1 = std::chrono::steady_clock::now();
  benchmark::DoNotOptimize(count);
  return {ns_between(t0, t1), count};
}

OpResult op_traverse_fast_hier(const hhds_bench::GraphSpec& spec) {
  hhds::GraphLibrary lib;
  auto               top = materialize_hier(lib, spec);

  auto    t0    = std::chrono::steady_clock::now();
  int64_t count = 0;
  for (auto node : top->fast_hier()) {
    benchmark::DoNotOptimize(node);
    ++count;
  }
  auto t1 = std::chrono::steady_clock::now();
  benchmark::DoNotOptimize(count);
  return {ns_between(t0, t1), count};
}

// op_traverse_hier_range: instance-only walk. Yields one entry per
// submodule instance (cost proportional to hier size, not graph node
// count) — distinct from fast_hier/forward_hier which visit every graph
// node within every instance.
OpResult op_traverse_hier_range(const hhds_bench::GraphSpec& spec) {
  hhds::GraphLibrary lib;
  auto               top = materialize_hier(lib, spec);

  auto    t0    = std::chrono::steady_clock::now();
  int64_t count = 0;
  for (auto inst : top->hier_range()) {
    benchmark::DoNotOptimize(inst);
    ++count;
  }
  auto t1 = std::chrono::steady_clock::now();
  benchmark::DoNotOptimize(count);
  return {ns_between(t0, t1), count};
}

// op_mutate: build full graph (untimed), then time deletion of a 10%
// random sample of nodes. Deletion is tombstone-based in hhds — IDs are
// never reused — so this measures the per-deletion bookkeeping cost.
OpResult op_mutate(const hhds_bench::GraphSpec& spec) {
  hhds_bench::EdgeList      edges = hhds_bench::make_edge_list(spec);
  hhds_bench::PinAssignment pins  = hhds_bench::make_pin_assignment(spec, edges);
  BuiltGraph                bg;
  materialize_flat(bg, spec, edges, pins);

  // Pick a 10% deletion sample WITHOUT replacement (partial Fisher-Yates
  // shuffle). hhds asserts on double-delete, so the bench must not draw
  // the same victim twice. Skip the first/last node so the
  // connect-to-graph-IO anchors stay valid.
  const int       sample = std::max(1, spec.nodes / 10);
  const int       lo     = 1;
  const int       hi     = std::max(lo + 1, spec.nodes - 1);
  const int       pool   = hi - lo;
  std::mt19937_64 rng(spec.seed ^ 0xDEADBEEFULL);
  std::vector<int> indices;
  indices.reserve(static_cast<size_t>(pool));
  for (int i = 0; i < pool; ++i) {
    indices.push_back(lo + i);
  }
  const int picks = std::min(sample, pool);
  for (int i = 0; i < picks; ++i) {
    std::uniform_int_distribution<> swap_dist(i, pool - 1);
    std::swap(indices[i], indices[swap_dist(rng)]);
  }
  indices.resize(static_cast<size_t>(picks));

  auto    t0      = std::chrono::steady_clock::now();
  int64_t deleted = 0;
  for (int v : indices) {
    bg.nodes[v].del_node();
    ++deleted;
  }
  auto t1 = std::chrono::steady_clock::now();
  return {ns_between(t0, t1), deleted};
}

// op_lookup: build full graph (untimed), pre-pick spec.nodes random
// class indexes, then time graph->get_node(idx) calls. Sanity check —
// expected to be O(1) per call regardless of N.
OpResult op_lookup(const hhds_bench::GraphSpec& spec) {
  hhds_bench::EdgeList      edges = hhds_bench::make_edge_list(spec);
  hhds_bench::PinAssignment pins  = hhds_bench::make_pin_assignment(spec, edges);
  BuiltGraph                bg;
  materialize_flat(bg, spec, edges, pins);

  std::mt19937_64                 rng(spec.seed ^ 0xBADCAFEULL);
  std::uniform_int_distribution<> idx_dist(0, std::max(0, spec.nodes - 1));
  std::vector<hhds::Class_index>  keys;
  keys.reserve(static_cast<size_t>(spec.nodes));
  for (int i = 0; i < spec.nodes; ++i) {
    keys.push_back(bg.nodes[idx_dist(rng)].get_class_index());
  }

  auto    t0       = std::chrono::steady_clock::now();
  int64_t resolved = 0;
  for (const auto& k : keys) {
    auto n = bg.graph->get_node(k);
    benchmark::DoNotOptimize(n);
    ++resolved;
  }
  auto t1 = std::chrono::steady_clock::now();
  return {ns_between(t0, t1), resolved};
}

OpResult dispatch(const std::string& op, const hhds_bench::GraphSpec& spec) {
  if (op == "build_node")             return op_build_node(spec);
  if (op == "build_edges")            return op_build_edges(spec);
  if (op == "add_pin")                return op_add_pin(spec);
  if (op == "mutate")                 return op_mutate(spec);
  if (op == "lookup")                 return op_lookup(spec);
  // Single-graph traversals.
  if (op == "traverse_forward_class") return op_traverse_forward_class(spec);
  if (op == "traverse_fast_class")    return op_traverse_fast_class(spec);
  // Hierarchy-flattening traversals (visit every node across all instances).
  if (op == "traverse_forward_flat")  return op_traverse_forward_flat(spec);
  if (op == "traverse_fast_flat")     return op_traverse_fast_flat(spec);
  // Hierarchy-aware traversals (per-instance node visit).
  if (op == "traverse_forward_hier")  return op_traverse_forward_hier(spec);
  if (op == "traverse_fast_hier")     return op_traverse_fast_hier(spec);
  // Instance-only walk (one yield per submodule, no inner-node visit).
  if (op == "traverse_hier_range")    return op_traverse_hier_range(spec);
  std::cerr << "hhds_bench: unknown op: " << op << "\n";
  std::exit(3);
}

}  // namespace

int main(int argc, char** argv) {
  const CliArgs a    = parse_args(argc, argv);
  g_verify           = a.verify;
  const auto    spec = spec_from(a);

  if (a.emit_header) {
    std::cout << "library,op,topology,axis,size,pins_per_node,hier_size,seed,run_idx,"
              << "wall_ns,peak_rss_kb,bytes_per_node,l1_misses,llc_misses,instructions,cycles,ipc\n";
  }

  for (int run = 0; run < a.runs; ++run) {
    const OpResult r = dispatch(a.op, spec);
    std::cout << "hhds," << a.op << "," << a.topology << "," << a.axis << ","
              << a.nodes << "," << a.pins << "," << a.hier << "," << a.seed << ","
              << run << ","
              << r.wall_ns
              << ",0,0,0,0,0,0,0\n";  // wrapper rewrites these
    if (g_verify) {
      // Print to stderr so the CSV on stdout is unaffected. Run-by-run
      // counts let you confirm flat/hier traversals actually descend
      // into subnodes (visited >> top-graph-only would mean class).
      std::fprintf(stderr,
                   "verify: op=%s nodes=%d pins=%d hier=%d run=%d visited=%lld\n",
                   a.op.c_str(), a.nodes, a.pins, a.hier, run,
                   static_cast<long long>(r.visited));
    }
  }
  return 0;
}
