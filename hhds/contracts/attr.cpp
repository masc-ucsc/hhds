// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// HHDS index-keyed map contract examples.
//
// Traversal handles are fine to pass around, but user-owned maps should use
// the explicit identity type matching the selected scope.
//
// The index types in hhds/index.hpp are small, plain-data, hashable keys:
//
//   Class_index — per-graph-body. One int: the raw nid/pid. Same for both
//                 instantiations of a re-used body (since they share it).
//   Definition_index — library-wide (gid, nid), shared by all instances.
//   Occurrence_index — full occurrence path plus definition identity.
//
// The graph below is the canonical "top with bottom instantiated twice"
// shape. Class/definition keys collapse both bottom instances; occurrence
// keys keep them distinct.

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

  for (auto node : f.top->body().nodes(hhds::Node_order::forward)) {
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

// Definition_index is library-wide. All instantiations of the same node share
// the same (gid, nid) identity.
TEST(IndexContract, DefinitionIndexSharesKeyAcrossInstantiations) {
  Fixture f;

  std::unordered_map<hhds::Definition_index, int>          std_hits;
  absl::flat_hash_map<hhds::Definition_index, std::string> absl_trace;

  std::vector<hhds::Node_class> bottom_visits;
  for (auto node : f.top->definitions().nodes(hhds::Node_order::forward)) {
    if (node.get_current_gid() == f.bottom->get_gid() && node.get_debug_nid() == f.bottom_body_node.get_debug_nid()) {
      bottom_visits.push_back(node);
    }
    ++std_hits[node.get_definition_index()];
    absl_trace[node.get_definition_index()].append("x");
  }

  // definitions() enters the bottom body exactly once.
  ASSERT_EQ(bottom_visits.size(), 1u);
  EXPECT_EQ(std_hits[bottom_visits.front().get_definition_index()], 1);

  // The library-wide view and the direct body view agree on definition identity.
  bool matched = false;
  for (auto body_node : f.bottom->definitions().nodes(hhds::Node_order::forward)) {
    if (body_node.get_definition_index() == bottom_visits.front().get_definition_index()) {
      matched = true;
    }
  }
  EXPECT_TRUE(matched);
}

// Occurrence_index distinguishes separate instantiations of the same body.
TEST(IndexContract, OccurrenceIndexDistinguishesInstantiations) {
  Fixture f;

  std::unordered_map<hhds::Occurrence_index, int>          std_delay;
  absl::flat_hash_map<hhds::Occurrence_index, std::string> absl_path;

  std::vector<hhds::Occurrence_node> bottom_instances;
  for (auto node : f.top->grouped_hierarchy().nodes(hhds::Node_order::forward)) {
    if (node.get_current_gid() == f.bottom->get_gid() && node.get_debug_nid() == f.bottom_body_node.get_debug_nid()) {
      bottom_instances.push_back(node);
    }
  }

  // The grouped hierarchy enters the body once per instantiation.
  ASSERT_EQ(bottom_instances.size(), 2u);
  const auto hi0 = bottom_instances[0].get_occurrence_index();
  const auto hi1 = bottom_instances[1].get_occurrence_index();
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

  // Definition identity collapses both instances.
  EXPECT_EQ(bottom_instances[0].get_definition_index(), bottom_instances[1].get_definition_index());
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
  absl::flat_hash_map<hhds::Occurrence_index, float> per_inst_delay;
  std::vector<hhds::Occurrence_pin>                  seen_pins;
  for (auto node : f.top->grouped_hierarchy().nodes(hhds::Node_order::forward)) {
    if (node.get_current_gid() != f.bottom->get_gid()) {
      continue;
    }
    if (node.get_debug_nid() != f.bottom_body_node.get_debug_nid()) {
      continue;
    }
    auto pin = node.get_driver_pin(pin_port);
    seen_pins.push_back(pin);
    per_inst_delay[pin.get_occurrence_index()] = static_cast<float>(seen_pins.size());
  }
  ASSERT_EQ(seen_pins.size(), 2u);

  // Both visits land on the same shared pin body, so class/definition keys match.
  EXPECT_EQ(seen_pins[0].get_class_index(), seen_pins[1].get_class_index());
  EXPECT_EQ(seen_pins[0].get_definition_index(), seen_pins[1].get_definition_index());

  // Occurrence keys differ per instantiation.
  EXPECT_NE(seen_pins[0].get_occurrence_index(), seen_pins[1].get_occurrence_index());
  EXPECT_EQ(per_inst_delay.size(), 2u);

  // A pin's class key and the master node's class key are distinct because
  // pin_pid encoding differs from raw_nid — so the same map can mix them.
  std::unordered_map<hhds::Class_index, std::string> mixed;
  mixed[f.bottom_body_node.get_class_index()] = "node";
  mixed[bottom_out.get_class_index()]         = "pin";
  EXPECT_EQ(mixed.size(), 2u);
}
