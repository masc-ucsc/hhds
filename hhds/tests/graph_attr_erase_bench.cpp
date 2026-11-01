// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include <chrono>
#include <cstdio>
#include <stdexcept>
#include <string_view>
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
double single_wide(unsigned pins) {
  hhds::Graph graph;
  constexpr unsigned live_nodes = 16000;
  for (unsigned i = 0; i < live_nodes; ++i) {
    auto live = graph.create_node();
    const auto key = hhds::make_node_attr_key(live.get_class_index().value);
    for (unsigned path = 0; path < 4; ++path) {
      graph.attr_store(Color{})[hhds::Hier_attr_key{1, {{2, 16, path}}, key}] = 7;
    }
  }
  auto doomed = graph.create_node();
  std::vector<hhds::Attr_key> keys{hhds::make_node_attr_key(doomed.get_class_index().value)};
  for (unsigned port = pins; port > 0; --port) {
    auto pin = doomed.create_sink_pin(port);
    keys.push_back(hhds::make_pin_attr_key(pin.get_class_index().value & ~hhds::Pid{2}));
  }
  auto zero = doomed.create_driver_pin(0);
  keys.push_back(hhds::make_pin_attr_key(zero.get_class_index().value & ~hhds::Pid{2}));
  for (const auto key : keys) {
    for (unsigned path = 0; path < 4; ++path) {
      graph.attr_store(Color{})[hhds::Hier_attr_key{1, {{2, 16, path}}, key}] = 9;
    }
  }
  const auto start = std::chrono::steady_clock::now();
  doomed.del_node();
  const double elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
  const auto& remaining = graph.attr_store(Color{});
  if (remaining.size() != live_nodes * 4) {
    throw std::runtime_error("single-node deletion lost live attributes or retained deleted attributes");
  }
  for (const auto& [key, value] : remaining) {
    if (value != 7) throw std::runtime_error("single-node deletion left a deleted node/pin attribute");
  }
  return elapsed;
}
}  // namespace
int main(int argc, char** argv) {
  if (argc == 2 && std::string_view(argv[1]) == "wide") {
    for (const unsigned pins : {4, 32, 256}) {
      const double elapsed = single_wide(pins);
      std::printf("{\"pins\":%u,\"live_attributes\":64000,\"single_delete_ms\":%.6f}\n", pins, elapsed);
      std::fflush(stdout);
    }
    return 0;
  }

  for (const unsigned count : {4000, 8000, 16000}) {
    const double individual = run(count, false);
    const double bulk = run(count, true);
    std::printf("{\"nodes\":%u,\"individual_ms\":%.3f,\"bulk_ms\":%.3f,\"speedup\":%.3f}\n",
                count, individual, bulk, individual / bulk);
    std::fflush(stdout);
  }
}
