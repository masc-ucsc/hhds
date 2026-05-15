#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hhds_final_bench {

using Clock = std::chrono::steady_clock;
using Edge = std::pair<int, int>;

struct Args {
  std::string library  = "unknown";
  std::string op       = "add_nodes";
  std::string scenario = "default";
  std::string x_axis   = "nodes";
  int         nodes    = 1000;
  int         pins     = 1;
  int         hier     = 100;
  int         k        = 0;
  int         runs     = 5;
  bool        header   = true;
};

struct Result {
  int64_t wall_ns = 0;
  int64_t items   = 0;
};

template <typename T>
inline void keep_alive(const T& value) {
  asm volatile("" : : "g"(&value) : "memory");
}

inline int64_t ns_between(Clock::time_point begin, Clock::time_point end) {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin).count();
}

inline int parse_int(std::string_view text) {
  return std::atoi(std::string(text).c_str());
}

inline bool eat(std::string_view arg, std::string_view prefix, std::string& out) {
  if (arg.rfind(prefix, 0) != 0) {
    return false;
  }
  out = std::string(arg.substr(prefix.size()));
  return true;
}

inline bool eat_int(std::string_view arg, std::string_view prefix, int& out) {
  if (arg.rfind(prefix, 0) != 0) {
    return false;
  }
  out = parse_int(arg.substr(prefix.size()));
  return true;
}

inline Args parse_args(int argc, char** argv, std::string library) {
  Args args;
  args.library = std::move(library);

  for (int i = 1; i < argc; ++i) {
    const std::string_view arg(argv[i]);
    if (eat(arg, "--op=", args.op)) continue;
    if (eat(arg, "--scenario=", args.scenario)) continue;
    if (eat(arg, "--x-axis=", args.x_axis)) continue;
    if (eat_int(arg, "--nodes=", args.nodes)) continue;
    if (eat_int(arg, "--pins=", args.pins)) continue;
    if (eat_int(arg, "--hier=", args.hier)) continue;
    if (eat_int(arg, "--k=", args.k)) continue;
    if (eat_int(arg, "--runs=", args.runs)) continue;
    if (arg == "--no-header") {
      args.header = false;
      continue;
    }
    std::cerr << args.library << "_final_bench: unknown arg " << arg << "\n";
    std::exit(2);
  }

  return args;
}

inline int scenario_k(const std::string& scenario, int requested_k) {
  if (requested_k > 0) {
    return requested_k;
  }
  if (scenario == "sedges_inline") {
    return 2;
  }
  if (scenario == "ledge_inline") {
    return 4;
  }
  if (scenario == "overflow") {
    return 16;
  }
  return 4;
}

inline int x_value(const Args& args) {
  if (args.x_axis == "pins") {
    return args.pins;
  }
  if (args.x_axis == "hier") {
    return args.hier;
  }
  return args.nodes;
}

inline void print_header() {
  std::cout << "library,operation,scenario,x_axis,x_value,nodes,pins_per_node,hier_size,k,run_idx,wall_ns,items\n";
}

inline void print_row(const Args& args, int run_idx, const Result& result) {
  std::cout << args.library << "," << args.op << "," << args.scenario << "," << args.x_axis << "," << x_value(args)
            << "," << args.nodes << "," << args.pins << "," << args.hier << "," << scenario_k(args.scenario, args.k)
            << "," << run_idx << "," << result.wall_ns << "," << result.items << "\n";
}

inline std::vector<Edge> make_ring_forward_edges(int nodes, int k) {
  std::vector<Edge> edges;
  if (nodes <= 1 || k <= 0) {
    return edges;
  }

  const int real_k = std::min(k, nodes - 1);
  edges.reserve(static_cast<size_t>(nodes) * static_cast<size_t>(real_k));
  for (int src = 0; src < nodes; ++src) {
    for (int step = 1; step <= real_k; ++step) {
      const int dst = (src + step) % nodes;
      edges.emplace_back(src, dst);
    }
  }
  return edges;
}

inline std::vector<Edge> make_dag_forward_edges(int nodes, int k) {
  std::vector<Edge> edges;
  if (nodes <= 1 || k <= 0) {
    return edges;
  }

  edges.reserve(static_cast<size_t>(nodes) * static_cast<size_t>(k));
  for (int src = 0; src < nodes; ++src) {
    const int last = std::min(nodes - 1, src + k);
    for (int dst = src + 1; dst <= last; ++dst) {
      edges.emplace_back(src, dst);
    }
  }
  return edges;
}

inline std::vector<Edge> make_edges_for_op(const Args& args, bool require_dag) {
  const int k = scenario_k(args.scenario, args.k);
  return require_dag ? make_dag_forward_edges(args.nodes, k) : make_ring_forward_edges(args.nodes, k);
}

inline int64_t half_count(size_t count) {
  return static_cast<int64_t>(count / 2U);
}

}  // namespace hhds_final_bench

