// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// HHDS index-keyed map contract examples.
//
// Nodes and pins return *handles* (Node_class / Pin_class) that carry
// traversal context (graph pointer, hierarchy state, shared_ptr). Those
// handles are fine to pass around, but they are the wrong thing to use as
// a key in a user-owned std::unordered_map / absl::flat_hash_map — each
// probe would copy the shared_ptr and hash fields that are not part of
// the node's identity.
//
// The three index types in hhds/index.hpp are small, plain-data, hashable
// keys. Each one captures exactly the identity that a given traversal
// context can see:
//
//   Class_index — per-graph-body. One int: the raw nid/pid. Same for both
//                 instantiations of a re-used body (since they share it).
//   Flat_index  — library-wide. Two ints: (gid, nid). Same for both
//                 instantiations of a re-used body.
//   Hier_index  — per-instance. Two ints: (hier_pos, nid). Different for
//                 each instantiation because hier_pos differs.
//
// The graph below is the canonical "top with bottom instantiated twice"
// shape. Flat/class keys collapse both bottom instances to the same entry;
// hier keys keep them distinct.

#include "hhds/attr.hpp"

#include <gtest/gtest.h>

#include <string>
#include <unordered_map>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "hhds/attrs/name.hpp"
#include "hhds/graph.hpp"
#include "hhds/index.hpp"

namespace {

struct Fixture {
  hhds::GraphLibrary             lib;
  std::shared_ptr<hhds::GraphIO> bottom_io;
  std::shared_ptr<hhds::Graph>   bottom;
  hhds::Node                     bottom_body_node;

  std::shared_ptr<hhds::GraphIO> top_io;
  std::shared_ptr<hhds::Graph>   top;
  hhds::Node                     inst1;
  hhds::Node                     inst2;

  Fixture() {
    // Shared leaf body with a single node inside.
    bottom_io        = lib.create_io("bottom");
    bottom           = bottom_io->create_graph();
    bottom_body_node = bottom->create_node();
    bottom_body_node.attr(hhds::attrs::name).set("bottom_cell");

    // Top instantiates the same bottom body twice.
    top_io = lib.create_io("top");
    top    = top_io->create_graph();
    inst1  = top->create_node();
    inst2  = top->create_node();
    inst1.set_subnode(bottom_io);
    inst2.set_subnode(bottom_io);
    inst1.attr(hhds::attrs::name).set("inst1");
    inst2.attr(hhds::attrs::name).set("inst2");
  }
};

}  // namespace

// Class_index: the cheapest key. One integer, per graph body. A map keyed by
// Class_index must be scoped to a single graph body — keys from different
// bodies may collide.
TEST(IndexContract, ClassIndexKeysSingleGraphBody) {
  Fixture f;

  // User-owned side table keyed by Class_index (std and absl variants,
  // both hash the underlying integer).
  std::unordered_map<hhds::Class_index, int>          std_cost;
  absl::flat_hash_map<hhds::Class_index, std::string> absl_label;

  for (auto node : f.top->forward_class()) {
    if (!node.attr(hhds::attrs::name).has()) {
      continue;
    }
    std_cost[node.get_class_index()]   = 7;
    absl_label[node.get_class_index()] = std::string(node.attr(hhds::attrs::name).get());
  }

  EXPECT_EQ(std_cost.size(), 2u);
  EXPECT_EQ(std_cost[f.inst1.get_class_index()], 7);
  EXPECT_EQ(std_cost[f.inst2.get_class_index()], 7);
  EXPECT_EQ(absl_label[f.inst1.get_class_index()], "inst1");
  EXPECT_EQ(absl_label[f.inst2.get_class_index()], "inst2");

  // The two inst nodes live in the top body; bottom_body_node lives in the
  // bottom body. Their Class_index values may coincide numerically, so
  // mixing them in a single map is unsafe — enforced only by scope, not
  // by the type system.
}

// Flat_index: library-wide. Two instantiations of the same body map to the
// *same* Flat_index because they share the body's (gid, nid).
TEST(IndexContract, FlatIndexSharesKeyAcrossInstantiations) {
  Fixture f;

  std::unordered_map<hhds::Flat_index, int>          std_hits;
  absl::flat_hash_map<hhds::Flat_index, std::string> absl_trace;

  std::vector<hhds::Node> bottom_visits;
  for (auto node : f.top->forward_flat()) {
    if (node.get_current_gid() == f.bottom->get_gid() && node.get_debug_nid() == f.bottom_body_node.get_debug_nid()) {
      bottom_visits.push_back(node);
    }
    ++std_hits[node.get_flat_index()];
    absl_trace[node.get_flat_index()].append("x");
  }

  // forward_flat enters the bottom body exactly once, so only one visit.
  ASSERT_EQ(bottom_visits.size(), 1u);
  EXPECT_EQ(std_hits[bottom_visits.front().get_flat_index()], 1);

  // The bottom instance reached via forward_flat shares its Flat_index
  // with the bottom body node visited directly from inside bottom.
  bool matched = false;
  for (auto body_node : f.bottom->forward_flat()) {
    if (body_node.get_flat_index() == bottom_visits.front().get_flat_index()) {
      matched = true;
    }
  }
  EXPECT_TRUE(matched);

  // get_flat_index is illegal from class context — the handle has no gid.
  // (Release builds skip the assert; this line is here to document the
  // contract, not to be exercised at runtime.)
  //
  //   for (auto n : f.top->forward_class()) n.get_flat_index();  // aborts
}

// Hier_index: per-instance. Two instantiations of the same body get
// *different* Hier_index values because hier_pos differs between them.
TEST(IndexContract, HierIndexDistinguishesInstantiations) {
  Fixture f;

  std::unordered_map<hhds::Hier_index, int>          std_delay;
  absl::flat_hash_map<hhds::Hier_index, std::string> absl_path;

  std::vector<hhds::Node> bottom_instances;
  for (auto node : f.top->forward_hier()) {
    if (node.get_current_gid() == f.bottom->get_gid() && node.get_debug_nid() == f.bottom_body_node.get_debug_nid()) {
      bottom_instances.push_back(node);
    }
  }

  // forward_hier enters the body once per instantiation.
  ASSERT_EQ(bottom_instances.size(), 2u);
  const auto hi0 = bottom_instances[0].get_hier_index();
  const auto hi1 = bottom_instances[1].get_hier_index();
  EXPECT_NE(hi0, hi1);

  // Per-instance values survive independently in the map.
  std_delay[hi0] = 11;
  std_delay[hi1] = 22;
  absl_path[hi0] = "top/inst1/bottom_cell";
  absl_path[hi1] = "top/inst2/bottom_cell";

  EXPECT_EQ(std_delay.size(), 2u);
  EXPECT_EQ(std_delay[hi0], 11);
  EXPECT_EQ(std_delay[hi1], 22);
  EXPECT_EQ(absl_path[hi0], "top/inst1/bottom_cell");
  EXPECT_EQ(absl_path[hi1], "top/inst2/bottom_cell");

  // Flat_index collapses both instances to the same key — contrast with
  // Hier_index above.
  EXPECT_EQ(bottom_instances[0].get_flat_index(), bottom_instances[1].get_flat_index());
}

// Pins carry the same identity machinery as nodes and expose the same three
// get_*_index() accessors. Pin indexes live in the same integer space as
// node indexes (Nid encoding distinguishes them), so a single map can key
// both without collision.
TEST(IndexContract, PinIndexesUseSameKeySpaceAsNodes) {
  Fixture f;

  // Give the bottom body node a driver pin; every instantiation reaches
  // the same pin body.
  constexpr hhds::Port_id pin_port   = 1;
  auto                    bottom_out = f.bottom_body_node.create_driver_pin(pin_port);

  // Per-instance timing on each bottom_out pin.
  absl::flat_hash_map<hhds::Hier_index, float> per_inst_delay;
  std::vector<hhds::Pin>                       seen_pins;
  for (auto node : f.top->forward_hier()) {
    if (node.get_current_gid() != f.bottom->get_gid()) {
      continue;
    }
    if (node.get_debug_nid() != f.bottom_body_node.get_debug_nid()) {
      continue;
    }
    auto pin = node.get_driver_pin(pin_port);
    seen_pins.push_back(pin);
    per_inst_delay[pin.get_hier_index()] = static_cast<float>(seen_pins.size());
  }
  ASSERT_EQ(seen_pins.size(), 2u);

  // Both visits land on the same shared pin body, so class/flat keys match.
  EXPECT_EQ(seen_pins[0].get_class_index(), seen_pins[1].get_class_index());
  EXPECT_EQ(seen_pins[0].get_flat_index(), seen_pins[1].get_flat_index());

  // Hier keys differ per instantiation.
  EXPECT_NE(seen_pins[0].get_hier_index(), seen_pins[1].get_hier_index());
  EXPECT_EQ(per_inst_delay.size(), 2u);

  // A pin's class key and the master node's class key are distinct because
  // pin_pid encoding differs from raw_nid — so the same map can mix them.
  std::unordered_map<hhds::Class_index, std::string> mixed;
  mixed[f.bottom_body_node.get_class_index()] = "node";
  mixed[bottom_out.get_class_index()]         = "pin";
  EXPECT_EQ(mixed.size(), 2u);
}
