#include "final_bench_common.hpp"
#include "graph.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace {

using hhds_final_bench::Args;
using hhds_final_bench::Clock;
using hhds_final_bench::Edge;
using hhds_final_bench::Result;
using hhds_final_bench::keep_alive;
using hhds_final_bench::make_edges_for_op;
using hhds_final_bench::ns_between;

struct FlatGraph {
  hhds::GraphLibrary           lib;
  std::shared_ptr<hhds::Graph> graph;
  std::vector<hhds::Node>      nodes;
  std::vector<Edge>            edges;
};

void create_empty_graph(FlatGraph& out, int nodes) {
  auto io = out.lib.create_io("top");
  out.graph = io->create_graph();
  out.nodes.reserve(static_cast<size_t>(nodes));
  for (int i = 0; i < nodes; ++i) {
    out.nodes.push_back(out.graph->create_node());
  }
}

hhds::Port_id port_for_edge(size_t edge_idx, int pins) {
  if (pins <= 1) {
    return 0;
  }
  return static_cast<hhds::Port_id>(1 + static_cast<int>(edge_idx % static_cast<size_t>(pins - 1)));
}

void connect_edges(FlatGraph& graph, const std::vector<Edge>& edges, int pins) {
  for (size_t i = 0; i < edges.size(); ++i) {
    const auto [src, dst] = edges[i];
    const auto port = port_for_edge(i, pins);
    graph.nodes[src].create_driver_pin(port).connect_sink(graph.nodes[dst].create_sink_pin(port));
  }
}

void build_graph_with_edges(FlatGraph& out, const Args& args, bool require_dag, int pins) {
  create_empty_graph(out, args.nodes);
  out.edges = make_edges_for_op(args, require_dag);
  connect_edges(out, out.edges, pins);
}

std::shared_ptr<hhds::Graph> create_hier_graph(hhds::GraphLibrary& lib, int nodes_per_subgraph, int hier_size) {
  auto sub_io = lib.create_io("sub");
  sub_io->add_input("i", 0);
  sub_io->add_output("o", 0);
  auto sub = sub_io->create_graph();

  std::vector<hhds::Node> inner;
  inner.reserve(static_cast<size_t>(nodes_per_subgraph));
  for (int i = 0; i < nodes_per_subgraph; ++i) {
    inner.push_back(sub->create_node());
  }
  if (!inner.empty()) {
    sub->get_input_pin("i").connect_sink(inner.front().create_sink_pin());
    for (size_t i = 0; i + 1 < inner.size(); ++i) {
      inner[i].create_driver_pin().connect_sink(inner[i + 1].create_sink_pin());
    }
    inner.back().create_driver_pin().connect_sink(sub->get_output_pin("o"));
  }

  auto top_io = lib.create_io("top");
  top_io->add_input("in", 0);
  top_io->add_output("out", 0);
  auto top = top_io->create_graph();

  std::vector<hhds::Node> inst;
  inst.reserve(static_cast<size_t>(hier_size));
  for (int i = 0; i < hier_size; ++i) {
    auto node = top->create_node();
    node.set_subnode(sub_io);
    inst.push_back(node);
  }
  if (!inst.empty()) {
    top->get_input_pin("in").connect_sink(inst.front().create_sink_pin("i"));
    for (size_t i = 0; i + 1 < inst.size(); ++i) {
      inst[i].create_driver_pin("o").connect_sink(inst[i + 1].create_sink_pin("i"));
    }
    inst.back().create_driver_pin("o").connect_sink(top->get_output_pin("out"));
  }

  return top;
}

Result op_add_nodes(const Args& args) {
  hhds::GraphLibrary lib;
  auto               io = lib.create_io("top");
  auto               graph = io->create_graph();

  auto    begin = Clock::now();
  int64_t items = 0;
  for (int i = 0; i < args.nodes; ++i) {
    auto node = graph->create_node();
    keep_alive(node);
    ++items;
  }
  auto end = Clock::now();
  return {ns_between(begin, end), items};
}

Result op_add_nodes_with_pins(const Args& args) {
  hhds::GraphLibrary lib;
  auto               io = lib.create_io("top");
  auto               graph = io->create_graph();
  const int          pins = std::max(1, args.pins);

  auto    begin = Clock::now();
  int64_t items = 0;
  for (int i = 0; i < args.nodes; ++i) {
    auto node = graph->create_node();
    keep_alive(node);
    ++items;
    for (int p = 0; p < pins; ++p) {
      auto driver = node.create_driver_pin(static_cast<hhds::Port_id>(p));
      auto sink = node.create_sink_pin(static_cast<hhds::Port_id>(p));
      keep_alive(driver);
      keep_alive(sink);
      items += 2;
    }
  }
  auto end = Clock::now();
  return {ns_between(begin, end), items};
}

Result op_add_pins(const Args& args) {
  FlatGraph graph;
  create_empty_graph(graph, args.nodes);
  const int pins = std::max(1, args.pins);

  auto    begin = Clock::now();
  int64_t items = 0;
  for (auto& node : graph.nodes) {
    for (int p = 0; p < pins; ++p) {
      auto driver = node.create_driver_pin(static_cast<hhds::Port_id>(p));
      auto sink = node.create_sink_pin(static_cast<hhds::Port_id>(p));
      keep_alive(driver);
      keep_alive(sink);
      items += 2;
    }
  }
  auto end = Clock::now();
  return {ns_between(begin, end), items};
}

Result op_add_edges(const Args& args) {
  FlatGraph graph;
  create_empty_graph(graph, args.nodes);
  const auto edges = make_edges_for_op(args, false);

  auto    begin = Clock::now();
  int64_t items = 0;
  for (const auto& [src, dst] : edges) {
    graph.nodes[src].create_driver_pin().connect_sink(graph.nodes[dst].create_sink_pin());
    ++items;
  }
  auto end = Clock::now();
  return {ns_between(begin, end), items};
}

Result op_delete_edges(const Args& args) {
  FlatGraph graph;
  build_graph_with_edges(graph, args, false, 1);
  const int64_t count = hhds_final_bench::half_count(graph.edges.size());

  auto    begin = Clock::now();
  int64_t items = 0;
  for (int64_t i = 0; i < count; ++i) {
    const auto [src, dst] = graph.edges[static_cast<size_t>(i)];
    graph.nodes[dst].create_sink_pin().del_sink(graph.nodes[src].create_driver_pin());
    ++items;
  }
  auto end = Clock::now();
  return {ns_between(begin, end), items};
}

Result op_delete_pins(const Args& args) {
  FlatGraph graph;
  create_empty_graph(graph, args.nodes);
  const int pins = std::max(1, args.pins);
  for (auto& node : graph.nodes) {
    for (int p = 1; p < pins; ++p) {
      keep_alive(node.create_sink_pin(static_cast<hhds::Port_id>(p)));
    }
  }

  auto    begin = Clock::now();
  int64_t items = 0;
  for (auto& node : graph.nodes) {
    for (int p = 1; p < pins; ++p) {
      node.get_sink_pin(static_cast<hhds::Port_id>(p)).del_pin();
      ++items;
    }
  }
  auto end = Clock::now();
  return {ns_between(begin, end), items};
}

Result op_delete_pins_with_edges(const Args& args) {
  FlatGraph graph;
  create_empty_graph(graph, args.nodes);
  const int pins = std::max(2, args.pins);

  for (int i = 0; i < args.nodes; ++i) {
    const int dst = (i + 1) % std::max(1, args.nodes);
    for (int p = 1; p < pins; ++p) {
      graph.nodes[i].create_driver_pin(static_cast<hhds::Port_id>(p))
          .connect_sink(graph.nodes[dst].create_sink_pin(static_cast<hhds::Port_id>(p)));
    }
  }

  auto    begin = Clock::now();
  int64_t items = 0;
  for (auto& node : graph.nodes) {
    for (int p = 1; p < pins; ++p) {
      node.get_sink_pin(static_cast<hhds::Port_id>(p)).del_pin();
      ++items;
    }
  }
  auto end = Clock::now();
  return {ns_between(begin, end), items};
}

Result op_delete_nodes_with_edges_and_pins(const Args& args) {
  FlatGraph graph;
  Args      edge_args = args;
  edge_args.scenario = "ledge_inline";
  build_graph_with_edges(graph, edge_args, false, std::max(2, args.pins));

  auto    begin = Clock::now();
  int64_t items = 0;
  for (int i = 0; i < args.nodes; i += 2) {
    graph.nodes[static_cast<size_t>(i)].del_node();
    ++items;
  }
  auto end = Clock::now();
  return {ns_between(begin, end), items};
}

Result op_lookup_nodes(const Args& args) {
  FlatGraph graph;
  create_empty_graph(graph, args.nodes);
  std::vector<hhds::Class_index> keys;
  keys.reserve(graph.nodes.size());
  for (const auto& node : graph.nodes) {
    keys.push_back(node.get_class_index());
  }

  auto    begin = Clock::now();
  int64_t items = 0;
  for (const auto& key : keys) {
    auto node = graph.graph->get_node(key);
    keep_alive(node);
    ++items;
  }
  auto end = Clock::now();
  return {ns_between(begin, end), items};
}

Result op_lookup_pins(const Args& args) {
  FlatGraph graph;
  create_empty_graph(graph, args.nodes);
  const int pins = std::max(1, args.pins);
  for (auto& node : graph.nodes) {
    for (int p = 0; p < pins; ++p) {
      keep_alive(node.create_sink_pin(static_cast<hhds::Port_id>(p)));
    }
  }

  auto    begin = Clock::now();
  int64_t items = 0;
  for (auto& node : graph.nodes) {
    for (int p = 0; p < pins; ++p) {
      auto driver = node.get_driver_pin(static_cast<hhds::Port_id>(p));
      auto sink = node.get_sink_pin(static_cast<hhds::Port_id>(p));
      keep_alive(driver);
      keep_alive(sink);
      items += 2;
    }
  }
  auto end = Clock::now();
  return {ns_between(begin, end), items};
}

Result op_lookup_edges(const Args& args) {
  FlatGraph graph;
  build_graph_with_edges(graph, args, false, 1);

  auto    begin = Clock::now();
  int64_t items = 0;
  for (const auto& node : graph.nodes) {
    auto edges = node.out_edges();
    items += static_cast<int64_t>(edges.size());
    keep_alive(edges);
  }
  auto end = Clock::now();
  return {ns_between(begin, end), items};
}

Result op_traverse_fast_class(const Args& args) {
  FlatGraph graph;
  build_graph_with_edges(graph, args, false, 1);

  auto    begin = Clock::now();
  int64_t items = 0;
  for (auto node : graph.graph->fast_class()) {
    keep_alive(node);
    ++items;
  }
  auto end = Clock::now();
  return {ns_between(begin, end), items};
}

Result op_traverse_forward_class(const Args& args) {
  FlatGraph graph;
  build_graph_with_edges(graph, args, true, 1);

  auto    begin = Clock::now();
  int64_t items = 0;
  for (auto node : graph.graph->forward_class()) {
    keep_alive(node);
    ++items;
  }
  auto end = Clock::now();
  return {ns_between(begin, end), items};
}

Result op_traverse_backward_class(const Args& args) {
  FlatGraph graph;
  build_graph_with_edges(graph, args, true, 1);

  auto    begin = Clock::now();
  int64_t items = 0;
  for (auto node : graph.graph->backward_class()) {
    keep_alive(node);
    ++items;
  }
  auto end = Clock::now();
  return {ns_between(begin, end), items};
}

Result op_traverse_fast_flat(const Args& args) {
  hhds::GraphLibrary lib;
  auto               graph = create_hier_graph(lib, args.nodes, args.hier);

  auto    begin = Clock::now();
  int64_t items = 0;
  for (auto node : graph->fast_flat()) {
    keep_alive(node);
    ++items;
  }
  auto end = Clock::now();
  return {ns_between(begin, end), items};
}

Result op_traverse_forward_flat(const Args& args) {
  hhds::GraphLibrary lib;
  auto               graph = create_hier_graph(lib, args.nodes, args.hier);

  auto    begin = Clock::now();
  int64_t items = 0;
  for (auto node : graph->forward_flat()) {
    keep_alive(node);
    ++items;
  }
  auto end = Clock::now();
  return {ns_between(begin, end), items};
}

Result op_traverse_backward_flat(const Args& args) {
  hhds::GraphLibrary lib;
  auto               graph = create_hier_graph(lib, args.nodes, args.hier);

  auto    begin = Clock::now();
  int64_t items = 0;
  for (auto node : graph->backward_flat()) {
    keep_alive(node);
    ++items;
  }
  auto end = Clock::now();
  return {ns_between(begin, end), items};
}

Result op_traverse_fast_hier(const Args& args) {
  hhds::GraphLibrary lib;
  auto               graph = create_hier_graph(lib, args.nodes, args.hier);

  auto    begin = Clock::now();
  int64_t items = 0;
  for (auto node : graph->fast_hier()) {
    keep_alive(node);
    ++items;
  }
  auto end = Clock::now();
  return {ns_between(begin, end), items};
}

Result op_traverse_forward_hier(const Args& args) {
  hhds::GraphLibrary lib;
  auto               graph = create_hier_graph(lib, args.nodes, args.hier);

  auto    begin = Clock::now();
  int64_t items = 0;
  for (auto node : graph->forward_hier()) {
    keep_alive(node);
    ++items;
  }
  auto end = Clock::now();
  return {ns_between(begin, end), items};
}

Result op_traverse_backward_hier(const Args& args) {
  hhds::GraphLibrary lib;
  auto               graph = create_hier_graph(lib, args.nodes, args.hier);

  auto    begin = Clock::now();
  int64_t items = 0;
  for (auto node : graph->backward_hier()) {
    keep_alive(node);
    ++items;
  }
  auto end = Clock::now();
  return {ns_between(begin, end), items};
}

Result op_traverse_hier_range(const Args& args) {
  hhds::GraphLibrary lib;
  auto               graph = create_hier_graph(lib, args.nodes, args.hier);

  auto    begin = Clock::now();
  int64_t items = 0;
  for (auto instance : graph->hier_range()) {
    keep_alive(instance);
    ++items;
  }
  auto end = Clock::now();
  return {ns_between(begin, end), items};
}

Result dispatch(const Args& args) {
  if (args.op == "add_nodes") return op_add_nodes(args);
  if (args.op == "add_nodes_with_pins") return op_add_nodes_with_pins(args);
  if (args.op == "add_pins") return op_add_pins(args);
  if (args.op == "add_edges") return op_add_edges(args);
  if (args.op == "delete_edges") return op_delete_edges(args);
  if (args.op == "delete_pins") return op_delete_pins(args);
  if (args.op == "delete_pins_with_edges") return op_delete_pins_with_edges(args);
  if (args.op == "delete_nodes_with_edges_and_pins") return op_delete_nodes_with_edges_and_pins(args);
  if (args.op == "lookup_nodes") return op_lookup_nodes(args);
  if (args.op == "lookup_pins") return op_lookup_pins(args);
  if (args.op == "lookup_edges") return op_lookup_edges(args);
  if (args.op == "traverse_fast_class") return op_traverse_fast_class(args);
  if (args.op == "traverse_forward_class") return op_traverse_forward_class(args);
  if (args.op == "traverse_backward_class") return op_traverse_backward_class(args);
  if (args.op == "traverse_fast_flat") return op_traverse_fast_flat(args);
  if (args.op == "traverse_forward_flat") return op_traverse_forward_flat(args);
  if (args.op == "traverse_backward_flat") return op_traverse_backward_flat(args);
  if (args.op == "traverse_fast_hier") return op_traverse_fast_hier(args);
  if (args.op == "traverse_forward_hier") return op_traverse_forward_hier(args);
  if (args.op == "traverse_backward_hier") return op_traverse_backward_hier(args);
  if (args.op == "traverse_hier_range") return op_traverse_hier_range(args);

  std::cerr << "hhds_final_bench: unsupported op " << args.op << "\n";
  std::exit(3);
}

}  // namespace

int main(int argc, char** argv) {
  const Args args = hhds_final_bench::parse_args(argc, argv, "hhds");

  if (args.header) {
    hhds_final_bench::print_header();
  }
  for (int run = 0; run < args.runs; ++run) {
    hhds_final_bench::print_row(args, run, dispatch(args));
  }
  return 0;
}

