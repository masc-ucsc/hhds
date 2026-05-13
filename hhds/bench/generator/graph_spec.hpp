#pragma once

#include <cstdint>
#include <string_view>
#include <utility>
#include <vector>

namespace hhds_bench {

enum class Topology {
  Chain,        // 0 -> 1 -> 2 -> ... -> N-1   (1 edge/node — never overflows)
  Fanout,       // Binary tree: i drives 2i+1, 2i+2 (capped at N-1) — never overflows
  RandomDag,    // Each node i has up to fanout_max edges to j > i, drawn deterministically from seed
  EdaTypical,   // Mostly-pin-0 chain backbone + sparse cross-edges; mimics typical netlist
  Dense,        // Each node connects to next 32 nodes; deprecated alias for OverflowOnly
                // with extra connectivity. Kept for back-compat with earlier sweeps.
  // --- Inline-vs-overflow comparison topologies ---
  // hhds NodeEntry has a 3-tier inline budget before overflow:
  //   slots 1-4   sedges_ packed (fastest)
  //   slots 5-7   sedges_extra packed
  //   slots 8-9   ledge0/ledge1 (long edges)
  //   slot 10+    overflow_handling() -> hashset
  // PinEntry is similar but lacks sedges_extra: tier boundary is 4 / 6 / 7+.
  SedgesOnly,    // Each node connects to next 2 nodes -> middle nodes have ~4 total edges
                 // (2 in + 2 out), all fitting in the 4-slot sedges_ packed path.
                 // Pure fastest-path benchmark.
  OverflowOnly,  // Each node connects to next 16 nodes -> middle nodes have ~32 total
                 // edges (16 in + 16 out), well past the 10-edge overflow trigger.
                 // When overflow is first triggered, hhds flushes the inline tier into
                 // the overflow set, so all edges end up in overflow with no inline
                 // content remaining — a clean "overflow-only" benchmark.
};

[[nodiscard]] Topology parse_topology(std::string_view name);
[[nodiscard]] std::string_view topology_name(Topology t);

struct GraphSpec {
  int      nodes         = 1000;
  int      pins_per_node = 1;       // 1 = pin-0 only on hhds
  int      hier_size     = 1;       // 1 = single graph; >1 = top with N submodule instances
  int      fanout_max    = 4;       // mean out-degree for RandomDag / EdaTypical
  uint64_t seed          = 0xC0FFEEULL;
  Topology topology      = Topology::Chain;
};

// Edge list in node-index space. Both endpoints are in [0, spec.nodes).
// Generated deterministically from spec.seed.
struct EdgeList {
  std::vector<std::pair<int, int>> edges;
  int nodes = 0;
};

[[nodiscard]] EdgeList make_edge_list(const GraphSpec& spec);

// Pin assignment: for each edge, which port (0..pins_per_node-1) the
// driver uses on its source node, and which port the sink uses on its
// destination node. Same seed -> identical pin assignment across libs.
//
// Returned vectors are parallel to edges. Empty if spec.pins_per_node <= 1
// (everything stays on pin 0, the default).
struct PinAssignment {
  std::vector<int> driver_ports;
  std::vector<int> sink_ports;
};

[[nodiscard]] PinAssignment make_pin_assignment(const GraphSpec& spec, const EdgeList& edges);

}  // namespace hhds_bench