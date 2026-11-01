// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include <chrono>
#include <cstdio>
#include <stdexcept>
#include <vector>

#include "hhds/graph.hpp"

namespace {
struct Color {
  using value_type = int32_t;
  using storage = hhds::hier_storage;
};
double run(unsigned count, bool bulk) {
  hhds::Graph graph;
  std::vector<hhds::Node_class> dead;
  for (unsigned i = 0; i < count; ++i) {
    auto node = graph.create_node();
    dead.push_back(node);
    const auto pin = node.create_driver_pin(1);
    const auto nk = hhds::make_node_attr_key(node.get_class_index().value);
    const auto pk = hhds::make_pin_attr_key(pin.get_class_index().value & ~hhds::Pid{2});
    for (unsigned path = 0; path < 2; ++path) {
      graph.attr_store(Color{})[hhds::Hier_attr_key{1, {{2, 16, path}}, nk}] = 1;
      graph.attr_store(Color{})[hhds::Hier_attr_key{1, {{2, 16, path}}, pk}] = 2;
    }
  }
  const auto start = std::chrono::steady_clock::now();
  if (bulk) {
    graph.delete_nodes(dead);
  } else {
    for (const auto& node : dead) {
      node.del_node();
    }
  }
  const double elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
  if (!graph.attr_store(Color{}).empty() || graph.body().nodes().begin() != graph.body().nodes().end()) {
    throw std::runtime_error("attribute deletion left live data");
  }
  return elapsed;
}
}  // namespace
int main() {
  for (const unsigned count : {4000, 8000, 16000}) {
    const double individual = run(count, false);
    const double bulk = run(count, true);
    std::printf("{\"nodes\":%u,\"individual_ms\":%.3f,\"bulk_ms\":%.3f,\"speedup\":%.3f}\n",
                count, individual, bulk, individual / bulk);
    std::fflush(stdout);
  }
}
