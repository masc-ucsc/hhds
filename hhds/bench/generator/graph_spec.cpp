#include "graph_spec.hpp"

#include <cassert>
#include <random>

namespace hhds_bench {

Topology parse_topology(std::string_view name) {
  if (name == "chain") {
    return Topology::Chain;
  }
  if (name == "fanout") {
    return Topology::Fanout;
  }
  if (name == "random_dag") {
    return Topology::RandomDag;
  }
  if (name == "eda_typical") {
    return Topology::EdaTypical;
  }
  assert(false && "unknown topology");
  return Topology::Chain;
}

std::string_view topology_name(Topology t) {
  switch (t) {
    case Topology::Chain:      return "chain";
    case Topology::Fanout:     return "fanout";
    case Topology::RandomDag:  return "random_dag";
    case Topology::EdaTypical: return "eda_typical";
  }
  return "unknown";
}

namespace {

EdgeList make_chain(const GraphSpec& spec) {
  EdgeList out;
  out.nodes = spec.nodes;
  out.edges.reserve(static_cast<size_t>(std::max(0, spec.nodes - 1)));
  for (int i = 0; i + 1 < spec.nodes; ++i) {
    out.edges.emplace_back(i, i + 1);
  }
  return out;
}

EdgeList make_fanout(const GraphSpec& spec) {
  EdgeList out;
  out.nodes = spec.nodes;
  out.edges.reserve(static_cast<size_t>(std::max(0, 2 * spec.nodes)));
  for (int i = 0; i < spec.nodes; ++i) {
    const int left  = 2 * i + 1;
    const int right = 2 * i + 2;
    if (left < spec.nodes) {
      out.edges.emplace_back(i, left);
    }
    if (right < spec.nodes) {
      out.edges.emplace_back(i, right);
    }
  }
  return out;
}

EdgeList make_random_dag(const GraphSpec& spec) {
  EdgeList out;
  out.nodes = spec.nodes;
  std::mt19937_64                 rng(spec.seed);
  std::uniform_int_distribution<> count_dist(1, std::max(1, spec.fanout_max));
  out.edges.reserve(static_cast<size_t>(spec.nodes) * static_cast<size_t>(spec.fanout_max));
  for (int i = 0; i < spec.nodes; ++i) {
    const int max_targets = std::min(spec.nodes - 1 - i, spec.fanout_max);
    if (max_targets <= 0) {
      continue;
    }
    const int                       k = std::min(count_dist(rng), max_targets);
    std::uniform_int_distribution<> target_dist(i + 1, spec.nodes - 1);
    for (int e = 0; e < k; ++e) {
      out.edges.emplace_back(i, target_dist(rng));
    }
  }
  return out;
}

EdgeList make_eda_typical(const GraphSpec& spec) {
  // Mostly-pin-0 chain backbone (90% of edges) + 10% sparse cross-edges
  // skipping ahead 2-8 nodes. Mimics LUT/gate netlist where most signals
  // are register-to-register short hops with occasional bypass paths.
  EdgeList out;
  out.nodes = spec.nodes;
  out.edges.reserve(static_cast<size_t>(spec.nodes));
  std::mt19937_64                 rng(spec.seed);
  std::uniform_int_distribution<> skip_dist(2, 8);
  std::uniform_real_distribution<> coin(0.0, 1.0);
  for (int i = 0; i + 1 < spec.nodes; ++i) {
    out.edges.emplace_back(i, i + 1);
    if (coin(rng) < 0.1) {
      const int target = i + skip_dist(rng);
      if (target < spec.nodes) {
        out.edges.emplace_back(i, target);
      }
    }
  }
  return out;
}

}  // namespace

EdgeList make_edge_list(const GraphSpec& spec) {
  switch (spec.topology) {
    case Topology::Chain:      return make_chain(spec);
    case Topology::Fanout:     return make_fanout(spec);
    case Topology::RandomDag:  return make_random_dag(spec);
    case Topology::EdaTypical: return make_eda_typical(spec);
  }
  return {};
}

PinAssignment make_pin_assignment(const GraphSpec& spec, const EdgeList& edges) {
  PinAssignment out;
  if (spec.pins_per_node <= 1) {
    return out;
  }
  out.driver_ports.reserve(edges.edges.size());
  out.sink_ports.reserve(edges.edges.size());
  std::mt19937_64                 rng(spec.seed ^ 0x9E3779B97F4A7C15ULL);
  std::uniform_int_distribution<> port_dist(0, spec.pins_per_node - 1);
  for (size_t i = 0; i < edges.edges.size(); ++i) {
    out.driver_ports.push_back(port_dist(rng));
    out.sink_ports.push_back(port_dist(rng));
  }
  return out;
}

}  // namespace hhds_bench