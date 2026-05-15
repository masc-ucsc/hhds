#include "final_bench_common.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include <boost/graph/adjacency_list.hpp>
#include <boost/graph/topological_sort.hpp>

namespace {

using hhds_final_bench::Args;
using hhds_final_bench::Clock;
using hhds_final_bench::Edge;
using hhds_final_bench::Result;
using hhds_final_bench::keep_alive;
using hhds_final_bench::make_edges_for_op;
using hhds_final_bench::ns_between;

using Graph = boost::adjacency_list<boost::vecS, boost::vecS, boost::bidirectionalS>;
using Vertex = boost::graph_traits<Graph>::vertex_descriptor;

struct BuiltGraph {
  Graph             graph;
  std::vector<Edge> edges;
};

void add_nodes(Graph& graph, int nodes) {
  for (int i = 0; i < nodes; ++i) {
    keep_alive(boost::add_vertex(graph));
  }
}

void add_edges(Graph& graph, const std::vector<Edge>& edges) {
  for (const auto& [src, dst] : edges) {
    boost::add_edge(static_cast<Vertex>(src), static_cast<Vertex>(dst), graph);
  }
}

BuiltGraph build_graph_with_edges(const Args& args, bool require_dag) {
  BuiltGraph out;
  add_nodes(out.graph, args.nodes);
  out.edges = make_edges_for_op(args, require_dag);
  add_edges(out.graph, out.edges);
  return out;
}

[[noreturn]] void unsupported(const Args& args) {
  std::cerr << "boost_final_bench: unsupported op " << args.op << "\n";
  std::exit(2);
}

Result op_add_nodes(const Args& args) {
  Graph graph;

  auto    begin = Clock::now();
  int64_t items = 0;
  for (int i = 0; i < args.nodes; ++i) {
    keep_alive(boost::add_vertex(graph));
    ++items;
  }
  auto end = Clock::now();
  return {ns_between(begin, end), items};
}

Result op_add_edges(const Args& args) {
  Graph graph;
  add_nodes(graph, args.nodes);
  const auto edges = make_edges_for_op(args, false);

  auto    begin = Clock::now();
  int64_t items = 0;
  for (const auto& [src, dst] : edges) {
    boost::add_edge(static_cast<Vertex>(src), static_cast<Vertex>(dst), graph);
    ++items;
  }
  auto end = Clock::now();
  return {ns_between(begin, end), items};
}

Result op_delete_edges(const Args& args) {
  auto graph = build_graph_with_edges(args, false);
  const int64_t count = hhds_final_bench::half_count(graph.edges.size());

  auto    begin = Clock::now();
  int64_t items = 0;
  for (int64_t i = 0; i < count; ++i) {
    const auto [src, dst] = graph.edges[static_cast<size_t>(i)];
    boost::remove_edge(static_cast<Vertex>(src), static_cast<Vertex>(dst), graph.graph);
    ++items;
  }
  auto end = Clock::now();
  return {ns_between(begin, end), items};
}

Result op_delete_nodes_with_edges_and_pins(const Args& args) {
  Args edge_args = args;
  edge_args.scenario = "ledge_inline";
  auto graph = build_graph_with_edges(edge_args, false);

  std::vector<int> victims;
  victims.reserve(static_cast<size_t>((args.nodes + 1) / 2));
  for (int i = 0; i < args.nodes; i += 2) {
    victims.push_back(i);
  }
  std::sort(victims.begin(), victims.end(), std::greater<int>());

  auto    begin = Clock::now();
  int64_t items = 0;
  for (int victim : victims) {
    const auto vertex = static_cast<Vertex>(victim);
    boost::clear_vertex(vertex, graph.graph);
    boost::remove_vertex(vertex, graph.graph);
    ++items;
  }
  auto end = Clock::now();
  return {ns_between(begin, end), items};
}

Result op_lookup_nodes(const Args& args) {
  Graph graph;
  add_nodes(graph, args.nodes);

  auto    begin = Clock::now();
  int64_t items = 0;
  for (int i = 0; i < args.nodes; ++i) {
    auto vertex = boost::vertex(static_cast<Vertex>(i), graph);
    keep_alive(vertex);
    ++items;
  }
  auto end = Clock::now();
  return {ns_between(begin, end), items};
}

Result op_lookup_edges(const Args& args) {
  auto graph = build_graph_with_edges(args, false);

  auto    begin = Clock::now();
  int64_t items = 0;
  for (int i = 0; i < args.nodes; ++i) {
    const auto range = boost::out_edges(static_cast<Vertex>(i), graph.graph);
    for (auto it = range.first; it != range.second; ++it) {
      keep_alive(*it);
      ++items;
    }
  }
  auto end = Clock::now();
  return {ns_between(begin, end), items};
}

Result op_traverse_fast_class(const Args& args) {
  auto graph = build_graph_with_edges(args, false);

  auto    begin = Clock::now();
  int64_t items = 0;
  const auto vertices = boost::vertices(graph.graph);
  for (auto it = vertices.first; it != vertices.second; ++it) {
    keep_alive(*it);
    ++items;
  }
  auto end = Clock::now();
  return {ns_between(begin, end), items};
}

Result op_traverse_forward_class(const Args& args) {
  auto graph = build_graph_with_edges(args, true);
  std::vector<Vertex> order;
  order.reserve(static_cast<size_t>(args.nodes));

  auto begin = Clock::now();
  boost::topological_sort(graph.graph, std::back_inserter(order));
  int64_t items = 0;
  for (auto it = order.rbegin(); it != order.rend(); ++it) {
    keep_alive(*it);
    ++items;
  }
  auto end = Clock::now();
  return {ns_between(begin, end), items};
}

Result op_traverse_backward_class(const Args& args) {
  auto graph = build_graph_with_edges(args, true);
  std::vector<Vertex> order;
  order.reserve(static_cast<size_t>(args.nodes));

  auto begin = Clock::now();
  boost::topological_sort(graph.graph, std::back_inserter(order));
  int64_t items = 0;
  for (const auto vertex : order) {
    keep_alive(vertex);
    ++items;
  }
  auto end = Clock::now();
  return {ns_between(begin, end), items};
}

Result dispatch(const Args& args) {
  if (args.op == "add_nodes") return op_add_nodes(args);
  if (args.op == "add_edges") return op_add_edges(args);
  if (args.op == "delete_edges") return op_delete_edges(args);
  if (args.op == "delete_nodes_with_edges_and_pins") return op_delete_nodes_with_edges_and_pins(args);
  if (args.op == "lookup_nodes") return op_lookup_nodes(args);
  if (args.op == "lookup_edges") return op_lookup_edges(args);
  if (args.op == "traverse_fast_class") return op_traverse_fast_class(args);
  if (args.op == "traverse_forward_class") return op_traverse_forward_class(args);
  if (args.op == "traverse_backward_class") return op_traverse_backward_class(args);

  unsupported(args);
}

}  // namespace

int main(int argc, char** argv) {
  const Args args = hhds_final_bench::parse_args(argc, argv, "boost");

  if (args.header) {
    hhds_final_bench::print_header();
  }
  for (int run = 0; run < args.runs; ++run) {
    hhds_final_bench::print_row(args, run, dispatch(args));
  }
  return 0;
}

