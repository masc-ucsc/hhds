// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// HHDS graph public-API contract tests.
//
// These tests illustrate the graph API documented in docs/iterators.md:
//   1. Freeze the public API surface that downstream users depend on.
//   2. Act as a short, runnable tutorial for new users.
//
// Only hhds public types (GraphLibrary, GraphIO, Graph, Node, Pin, attrs) are
// exercised. IDs identify expected nodes; raw storage entries are not touched.

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "hhds/attr.hpp"
#include "hhds/attrs/name.hpp"
#include "hhds/graph.hpp"
#include "hhds/node_hash.hpp"

namespace contract_attrs {

// Flat attribute: same value across hierarchy instances.
struct bits_t {
  using value_type = int;
  using storage    = hhds::flat_storage;
};
inline constexpr bits_t bits{};

// Hier attribute: different value per hierarchy instance.
struct hbits_t {
  using value_type = int;
  using storage    = hhds::hier_storage;
};
inline constexpr hbits_t hbits{};

// Flat pin attribute.
struct delay_t {
  using value_type = float;
  using storage    = hhds::flat_storage;
};
inline constexpr delay_t delay{};

}  // namespace contract_attrs

namespace {

std::vector<hhds::Nid> class_order(auto&& range) {
  std::vector<hhds::Nid> order;
  for (auto node : range) {
    order.push_back(node.get_debug_nid());
  }
  return order;
}

std::string flat_order_key(const hhds::Node& node) {
  return std::to_string(node.get_current_gid()) + ":" + std::to_string(node.get_debug_nid());
}

std::vector<std::string> flat_order(auto&& range) {
  std::vector<std::string> order;
  for (auto node : range) {
    order.push_back(flat_order_key(node));
  }
  return order;
}

std::string hier_order_key(const hhds::Occurrence_node& node) {
  return std::to_string(node.get_current_gid()) + ":" + std::to_string(node.path().hash()) + ":"
         + std::to_string(node.get_debug_nid());
}

std::vector<std::string> hier_order(auto&& range) {
  std::vector<std::string> order;
  for (auto node : range) {
    order.push_back(hier_order_key(node));
  }
  return order;
}

}  // namespace

// Sample Example 1: graph basics, flat attributes, forward traversal.
TEST(GraphApiContract, BasicsFlatAttributesForwardTraversal) {
  hhds::GraphLibrary glib;

  auto gio = glib.create_io("alu");
  EXPECT_EQ(glib.find_io("alu"), gio);

  auto g = gio->create_graph();
  EXPECT_EQ(g->get_io(), gio);
  EXPECT_EQ(gio->get_graph(), g);

  auto n1 = g->create_node();
  auto n2 = g->create_node();

  using hhds::attrs::name;
  n1.attr(name).set("adder");
  n2.attr(name).set("mux");
  EXPECT_EQ(n1.attr(name).get(), "adder");
  EXPECT_EQ(n2.attr(name).get(), "mux");

  // A body traversal returns Node_class handles with flat attributes.
  std::vector<std::string> names;
  for (auto node : g->body().nodes(hhds::Node_order::forward)) {
    EXPECT_TRUE(node.is_class());
    if (node.attr(name).has()) {
      names.push_back(std::string(node.attr(name).get()));
    }
  }
  EXPECT_EQ(names.size(), 2u);
}

TEST(GraphApiContract, TraversalScopes) {
  hhds::GraphLibrary glib;

  auto leaf_io = glib.create_io("leaf_scope");
  auto leaf    = leaf_io->create_graph();
  auto leaf_n  = leaf->create_node();

  auto top_io = glib.create_io("top_scope");
  auto top    = top_io->create_graph();
  auto inst1  = top->create_node();
  auto inst2  = top->create_node();
  inst1.set_subnode(leaf_io);
  inst2.set_subnode(leaf_io);

  bool saw_inst1 = false;
  bool saw_inst2 = false;
  for (auto node : top->body().nodes()) {
    EXPECT_TRUE(node.is_class());
    if (node == inst1) {
      saw_inst1 = true;
    }
    if (node == inst2) {
      saw_inst2 = true;
    }
  }
  EXPECT_TRUE(saw_inst1);
  EXPECT_TRUE(saw_inst2);

  size_t definition_leaf_visits = 0;
  for (auto node : top->definitions().nodes()) {
    EXPECT_TRUE(node.is_class());
    if (node.get_current_gid() == leaf->get_gid() && node.get_debug_nid() == leaf_n.get_debug_nid()) {
      ++definition_leaf_visits;
    }
  }
  EXPECT_EQ(definition_leaf_visits, 1u);

  size_t grouped_leaf_visits = 0;
  for (auto node : top->grouped_hierarchy().nodes()) {
    EXPECT_EQ(node.get_root_gid(), top->get_gid());
    if (node.get_current_gid() == leaf->get_gid() && node.get_debug_nid() == leaf_n.get_debug_nid()) {
      ++grouped_leaf_visits;
    }
  }
  EXPECT_EQ(grouped_leaf_visits, 2u);
}

// Node views visit only user nodes. The built-in singletons are reached
// directly via Graph::get_input_node() / get_output_node() / get_constant_node()
// instead. CONST is recognized purely by node identity: any driver pin on
// CONST_NODE is a constant, no attribute check required.
TEST(GraphApiContract, DefaultTraversalsSkipBuiltinNodes) {
  hhds::GraphLibrary glib;

  auto gio = glib.create_io("default_order");
  gio->add_input("in", 0);
  gio->add_output("out", 0);
  auto g = gio->create_graph();

  auto n1 = g->create_node();
  auto n2 = g->create_node();
  g->get_input_pin("in").connect_sink(n1.create_sink_pin());
  n1.create_driver_pin().connect_sink(n2.create_sink_pin());
  n2.create_driver_pin().connect_sink(g->get_output_pin("out"));

  // A constant feeding n1: created on CONST_NODE, identifiable as constant
  // by master-node identity alone.
  auto k = g->create_constant(*Dlop::create_integer(1));
  EXPECT_EQ(k.get_master_node().get_debug_nid(), hhds::Graph::CONST_NODE);
  k.connect_sink(n1.create_sink_pin(1));

  const auto forward = class_order(g->body().nodes(hhds::Node_order::forward));
  const auto storage = class_order(g->body().nodes());

  ASSERT_EQ(forward.size(), 2u);
  ASSERT_EQ(storage.size(), 2u);
  EXPECT_EQ(forward, storage);
  EXPECT_EQ(forward.front(), n1.get_debug_nid());

  for (auto nid : forward) {
    EXPECT_NE(nid, hhds::Graph::INPUT_NODE);
    EXPECT_NE(nid, hhds::Graph::OUTPUT_NODE);
    EXPECT_NE(nid, hhds::Graph::CONST_NODE);
  }
}

// Built-in node accessors are direct singletons.
// To inspect their connectivity, use out_edges() / inp_sorted_pins()
// from the returned Node_class.
TEST(GraphApiContract, BuiltinNodeAccessors) {
  hhds::GraphLibrary glib;

  auto gio = glib.create_io("builtins");
  gio->add_input("in", 0);
  gio->add_output("out", 0);
  auto g = gio->create_graph();

  EXPECT_EQ(g->get_input_node().get_debug_nid(), hhds::Graph::INPUT_NODE);
  EXPECT_EQ(g->get_output_node().get_debug_nid(), hhds::Graph::OUTPUT_NODE);
  EXPECT_EQ(g->get_constant_node().get_debug_nid(), hhds::Graph::CONST_NODE);

  auto n = g->create_node();
  g->get_input_pin("in").connect_sink(n.create_sink_pin());
  n.create_driver_pin().connect_sink(g->get_output_pin("out"));
  auto k = g->create_constant(*Dlop::create_integer(1));
  k.connect_sink(n.create_sink_pin(1));

  // Connected-pin readers include the built-in nodes and their port-0 pins.
  EXPECT_EQ(g->get_input_node().out_edges().size(), 1u);
  EXPECT_EQ(g->get_output_node().inp_sorted_pins().size(), 1u);
  EXPECT_EQ(g->get_constant_node().out_edges().size(), 1u);
}

TEST(GraphApiContract, GroupedStorageAndForwardVisitSameNodes) {
  hhds::GraphLibrary glib;

  auto leaf_io = glib.create_io("leaf_order");
  auto leaf    = leaf_io->create_graph();
  auto leaf_n1 = leaf->create_node();
  auto leaf_n2 = leaf->create_node();
  leaf_n1.create_driver_pin().connect_sink(leaf_n2.create_sink_pin());

  auto top_io = glib.create_io("top_order");
  auto top    = top_io->create_graph();
  auto inst1  = top->create_node();
  auto inst2  = top->create_node();
  inst1.set_subnode(leaf_io);
  inst2.set_subnode(leaf_io);

  EXPECT_EQ(hier_order(top->grouped_hierarchy().nodes(hhds::Node_order::forward)), hier_order(top->grouped_hierarchy().nodes()));
}

TEST(GraphApiContract, DefinitionStorageAndForwardVisitSameNodes) {
  hhds::GraphLibrary glib;

  auto leaf_io = glib.create_io("leaf_flat_order");
  auto leaf    = leaf_io->create_graph();
  auto leaf_n1 = leaf->create_node();
  auto leaf_n2 = leaf->create_node();
  leaf_n1.create_driver_pin().connect_sink(leaf_n2.create_sink_pin());

  auto top_io = glib.create_io("top_flat_order");
  auto top    = top_io->create_graph();
  auto inst1  = top->create_node();
  auto inst2  = top->create_node();
  inst1.set_subnode(leaf_io);
  inst2.set_subnode(leaf_io);

  auto forward = flat_order(top->definitions().nodes(hhds::Node_order::forward));
  auto storage = flat_order(top->definitions().nodes());
  std::sort(forward.begin(), forward.end());
  std::sort(storage.begin(), storage.end());
  EXPECT_EQ(forward, storage);
}

TEST(GraphApiContract, PinConnectPinApi) {
  hhds::GraphLibrary glib;

  auto gio = glib.create_io("pins");
  auto g   = gio->create_graph();

  auto src0 = g->create_node();
  auto dst0 = g->create_node();
  auto src1 = g->create_node();
  auto dst1 = g->create_node();

  auto src0_out = src0.create_driver_pin();
  auto dst0_in  = dst0.create_sink_pin();
  auto src1_out = src1.create_driver_pin();
  auto dst1_in  = dst1.create_sink_pin();

  dst0_in.connect_driver(src0_out);
  src1_out.connect_sink(dst1_in);

  auto dst0_inp = dst0_in.get_driver_pins();
  ASSERT_EQ(dst0_inp.size(), 1u);
  EXPECT_EQ(dst0_inp[0], src0_out);
  EXPECT_EQ(dst0_in.get_driver_pin(), src0_out);

  auto src1_outp = src1_out.out_edges();
  ASSERT_EQ(src1_outp.size(), 1u);
  // out_edges() is a lazy range (not randomly indexable); front() is the edge.
  const auto src1_edge = src1_outp.front();
  EXPECT_EQ(src1_edge.driver, src1_out);
  EXPECT_EQ(src1_edge.sink, dst1_in);
  EXPECT_TRUE(dst0_in.has_driver());
  EXPECT_TRUE(src0_out.has_sink());
  EXPECT_EQ(src0_out.get_sink_pin(), dst0_in);

  dst0_in.del_sink();
  EXPECT_FALSE(dst0_in.has_driver());
  EXPECT_TRUE(dst0_in.get_driver_pin().is_invalid());
  EXPECT_FALSE(src0_out.has_sink());
  EXPECT_TRUE(src0_out.get_sink_pin().is_invalid());

  // A driver may fan out to several sinks; the singular convenience reader
  // is only valid for zero or one sink.
  src1_out.connect_sink(dst0_in);
  EXPECT_EQ(src1.out_sorted_pins().size(), 1u);
  EXPECT_EQ(src1_out.out_edges().size(), 2u);
#ifndef NDEBUG
  EXPECT_DEATH((void)src1_out.get_sink_pin(), "more than one sink");
#else
  EXPECT_TRUE(src1_out.get_sink_pin().is_invalid());
#endif
}

TEST(GraphApiContract, SortedPinsAreConnectedOrderedViewsWithMutationSnapshots) {
  hhds::GraphLibrary           lib;
  auto                         g    = lib.create_io("pins")->create_graph();
  auto                         node = g->create_node();
  std::vector<hhds::Pin_class> upstream;
  // Insertion order does not determine traversal order. Each port is used
  // in both directions; the direction flags must not hide either handle.
  for (hhds::Port_id port : {7, 0, 3}) {
    auto driver = g->create_node().create_driver_pin();
    node.create_sink_pin(port).connect_driver(driver);
    upstream.push_back(driver);
    node.create_driver_pin(port).connect_sink(g->create_node().create_sink_pin());
  }
  auto unused_sink   = node.create_sink_pin(2);
  auto unused_driver = node.create_driver_pin(5);
  EXPECT_TRUE(unused_sink.is_valid());
  EXPECT_TRUE(unused_driver.is_valid());

  std::vector<hhds::Port_id> inputs;
  // An iterator owns its cursor even after the temporary range disappears.
  auto                       it = node.inp_sorted_pins().begin();
  for (; it != node.inp_sorted_pins().end(); ++it) {
    const auto sink = *it;
    EXPECT_TRUE(sink.is_sink());
    EXPECT_TRUE(sink.get_driver_pin().is_valid());
    inputs.push_back(sink.get_port_id());
  }
  std::vector<hhds::Port_id> outputs;
  for (auto driver : node.out_sorted_pins()) {
    EXPECT_TRUE(driver.is_driver());
    EXPECT_TRUE(driver.get_sink_pin().is_valid());
    outputs.push_back(driver.get_port_id());
  }
  EXPECT_EQ(inputs, (std::vector<hhds::Port_id>{0, 3, 7}));
  EXPECT_EQ(outputs, inputs);

  const auto sinks   = node.inp_pins_snapshot();
  const auto drivers = node.out_pins_snapshot();
  ASSERT_EQ(sinks.size(), 3u);
  ASSERT_EQ(drivers.size(), 3u);
  for (const auto& sink : sinks) {
    sink.del_sink();
  }
  EXPECT_TRUE(node.inp_sorted_pins().empty());
  EXPECT_EQ(node.out_sorted_pins().size(), 3u);
  for (const auto& driver : upstream) {
    EXPECT_FALSE(driver.has_sink());
  }
  for (const auto& driver : drivers) {
    driver.del_driver();
  }
  EXPECT_TRUE(node.out_sorted_pins().empty());
  // Snapshots retain handles, not the old connectivity.
  for (const auto& sink : sinks) {
    EXPECT_TRUE(sink.is_valid());
    EXPECT_TRUE(sink.get_driver_pin().is_invalid());
  }
}

TEST(GraphApiContract, CompactLoopCarryUsesPluralDriversAndOccurrenceBindings) {
  hhds::GraphLibrary lib;
  auto               callee = lib.create_io("loop_body");
  callee->add_input("carry", 1);
  callee->add_output("next", 2);
  auto top_io = lib.create_io("top");
  top_io->add_input("seed", 1);
  auto top       = top_io->create_graph();
  auto loop_node = top->create_node();
  loop_node.set_subnode(callee, hhds::Subnode_loop{.first = 0, .step = 1, .count = 3});
  auto carry    = loop_node.create_sink_pin("carry");
  auto feedback = loop_node.create_driver_pin("next");
  auto seed     = top->get_input_pin("seed");
  carry.connect_driver(seed);
  feedback.connect_sink(carry);
  auto group = loop_node.subnode_group();
  ASSERT_NO_THROW(group.validate());

  auto connected = loop_node.inp_sorted_pins();
  ASSERT_EQ(connected.size(), 1u);
  EXPECT_EQ(connected.front(), carry);
  const auto drivers = carry.get_driver_pins();
  ASSERT_EQ(drivers.size(), 2u);
  EXPECT_NE(std::find(drivers.begin(), drivers.end(), seed), drivers.end());
  EXPECT_NE(std::find(drivers.begin(), drivers.end(), feedback), drivers.end());
#ifndef NDEBUG
  // The singular reader cannot represent a compact carry's seed plus feedback.
  EXPECT_DEATH((void)carry.get_driver_pin(), "more than one driver");
#endif

  const auto                         body_before = class_order(top->body().nodes());
  auto                               view        = top->occurrences();
  std::vector<hhds::Occurrence_node> iterations;
  for (const auto& node : view.nodes()) {
    iterations.push_back(node);
  }
  ASSERT_EQ(iterations.size(), 3u);
  for (size_t ordinal = 0; ordinal < iterations.size(); ++ordinal) {
    auto pins = iterations[ordinal].inp_sorted_pins();
    ASSERT_EQ(pins.size(), 1u);
    auto resolved = pins.front().get_driver_pins();
    ASSERT_EQ(resolved.size(), 1u);
    const auto expected = ordinal == 0 ? view.lift(seed) : iterations[ordinal - 1].get_driver_pin(2);
    EXPECT_EQ(resolved.front().get_occurrence_index(), expected.get_occurrence_index());
  }
  EXPECT_EQ(class_order(top->body().nodes()), body_before);
  EXPECT_EQ(carry.get_driver_pins().size(), 2u);
}

// Hashes select candidates for structural comparison; equality of digests is
// not a proof of equivalence. Groups are commutative internally and distinct
// by role, with repeated operands retained.
TEST(GraphHashContract, GroupFoldPreservesRolesAndMultiplicity) {
  const std::map<int, std::vector<uint64_t>> original{
      {0, {10, 20}},
      {1,     {30}}
  };
  const std::map<int, std::vector<uint64_t>> reordered{
      {0, {20, 10}},
      {1,     {30}}
  };
  const std::map<int, std::vector<uint64_t>> swapped_roles{
      {0,     {30}},
      {1, {10, 20}}
  };
  const std::map<int, std::vector<uint64_t>> duplicated{
      {0, {10, 20, 20}},
      {1,         {30}}
  };
  EXPECT_EQ(hhds::group_fold(99, original), hhds::group_fold(99, reordered));
  EXPECT_NE(hhds::group_fold(99, original), hhds::group_fold(99, swapped_roles));
  EXPECT_NE(hhds::group_fold(99, original), hhds::group_fold(99, duplicated));
  EXPECT_NE(hhds::group_fold(99, original), hhds::group_fold(100, original));
}

// Sample Example 3: declaring custom attributes. Flat vs hier storage
// is picked at compile time via Tag::storage.
TEST(GraphApiContract, CustomAttributeDeclarations) {
  hhds::GraphLibrary glib;
  auto               gio = glib.create_io("arith");
  auto               g   = gio->create_graph();

  auto node = g->create_node();

  using contract_attrs::bits;
  using hhds::attrs::name;

  // Flat attributes work from class traversal.
  for (auto n : g->body().nodes(hhds::Node_order::forward)) {
    n.attr(name).set("adder");
    n.attr(bits).set(32);
  }

  EXPECT_EQ(node.attr(name).get(), "adder");
  EXPECT_EQ(node.attr(bits).get(), 32);

  // The graph now advertises both attribute stores.
  EXPECT_TRUE(g->has_attr(name));
  EXPECT_TRUE(g->has_attr(bits));
}

// Sample Example 4: pins, pin attributes, edge iteration.
//
// Each operand has its own sink port. Pin attributes share storage between
// the sink and driver forms of a port, so use distinct ports for distinct
// attribute values; port 0 additionally shares its storage with the node.
TEST(GraphApiContract, PinsAttributesAndEdgeIteration) {
  hhds::GraphLibrary glib;

  auto and_gio = glib.create_io("and_gate");
  and_gio->add_input("a", 1);
  and_gio->add_input("b", 2);
  and_gio->add_output("y", 3);

  auto top_gio = glib.create_io("top");
  top_gio->add_input("x", 1);
  top_gio->add_input("y", 2);
  top_gio->add_output("z", 1);

  auto g = top_gio->create_graph();

  using contract_attrs::delay;
  using hhds::attrs::name;

  auto and1 = g->create_node();
  and1.set_subnode(and_gio);
  and1.attr(name).set("and1");

  // String-form pin creation resolves through the subnode's GraphIO.
  auto and1_in  = and1.create_sink_pin("a");
  auto and1_b   = and1.create_sink_pin("b");
  auto and1_out = and1.create_driver_pin("y");

  // get_* variants retrieve already-created pins.
  EXPECT_EQ(and1.get_sink_pin("a"), and1_in);
  EXPECT_EQ(and1.get_driver_pin("y"), and1_out);

  // get_pin_name returns the declared port name.
  EXPECT_EQ(and1_in.get_pin_name(), "a");
  EXPECT_EQ(and1_out.get_pin_name(), "y");

  // Pin attributes use the same .attr() API as nodes.
  and1_in.attr(delay).set(1.5f);
  and1_out.attr(delay).set(0.3f);
  EXPECT_FLOAT_EQ(and1_in.attr(delay).get(), 1.5f);
  EXPECT_FLOAT_EQ(and1_out.attr(delay).get(), 0.3f);

  // Connect graph IO pins to the gate.
  auto g_x = g->get_input_pin("x");
  auto g_y = g->get_input_pin("y");
  auto g_z = g->get_output_pin("z");
  EXPECT_EQ(g_x.get_master_node().get_debug_nid(), hhds::Graph::INPUT_NODE);
  EXPECT_EQ(g_z.get_master_node().get_debug_nid(), hhds::Graph::OUTPUT_NODE);

  and1_in.connect_driver(g_x);
  and1_b.connect_driver(g_y);
  and1_out.connect_sink(g_z);

  EXPECT_EQ(and1_in.get_driver_pin(), g_x);
  EXPECT_EQ(and1_b.get_driver_pin(), g_y);

  auto outp = and1_out.out_edges();
  ASSERT_EQ(outp.size(), 1u);
  EXPECT_EQ(outp.front().driver, and1_out);
  EXPECT_EQ(outp.front().sink, g_z);

  // Pin iteration — enumerate pins on a node directly.
  EXPECT_EQ(and1.inp_sorted_pins().size(), 2u);
  EXPECT_EQ(and1.out_sorted_pins().size(), 1u);
}

// Sample Example 5 (subset): attributes survive save/load, and attr_clear vs
// Graph::clear have distinct semantics.
TEST(GraphApiContract, PersistenceAndClearSemantics) {
  namespace fs               = std::filesystem;
  const std::string test_dir = "/tmp/hhds_contract_graph";
  fs::remove_all(test_dir);

  hhds::register_attr_tag<contract_attrs::bits_t>("contract_attrs::bits");

  using contract_attrs::bits;
  using hhds::attrs::name;

  hhds::GraphLibrary glib;
  auto               gio = glib.create_io("alu");
  gio->add_output("y", 0);
  auto g = gio->create_graph();

  auto add = g->create_node();
  add.attr(name).set("adder");
  add.attr(bits).set(32);

  auto sub = g->create_node();
  sub.attr(name).set("subtractor");
  sub.attr(bits).set(16);

  auto add_drv = add.create_driver_pin(0);
  add_drv.connect_sink(g->get_output_pin("y"));

  const auto add_nid = add.get_debug_nid();
  const auto sub_nid = sub.get_debug_nid();

  // Save the graph library, including graph bodies and attribute maps.
  glib.save(test_dir);
  EXPECT_TRUE(fs::exists(fs::path(test_dir) / "library.txt"));
  // gids are name-hashes now, not sequential — derive the body dir from the gid.
  EXPECT_TRUE(fs::exists(fs::path(test_dir) / ("graph_" + std::to_string(gio->get_gid())) / "body.bin"));

  // Load into a fresh library.
  hhds::GraphLibrary loaded_glib;
  loaded_glib.load(test_dir);
  auto gio2 = loaded_glib.find_io("alu");
  ASSERT_NE(gio2, nullptr);
  auto g2 = gio2->get_graph();
  ASSERT_NE(g2, nullptr);

  auto loaded_add = hhds::Node(g2.get(), add_nid);
  auto loaded_sub = hhds::Node(g2.get(), sub_nid);
  EXPECT_EQ(loaded_add.attr(name).get(), "adder");
  EXPECT_EQ(loaded_add.attr(bits).get(), 32);
  EXPECT_EQ(loaded_sub.attr(name).get(), "subtractor");
  EXPECT_EQ(loaded_sub.attr(bits).get(), 16);

  // attr_clear: entries gone, map registration stays.
  g->attr_clear(bits);
  EXPECT_TRUE(g->has_attr(bits));
  EXPECT_FALSE(add.attr(bits).has());

  // Graph::clear: body + attribute maps dropped, but GraphIO survives.
  g->clear();
  EXPECT_FALSE(g->has_attr(bits));
  EXPECT_TRUE(gio->has_graph());
  EXPECT_TRUE(g->get_output_pin("y").is_valid());

  // GraphIO::clear: declaration unreachable via find_io.
  gio->clear();
  EXPECT_EQ(glib.find_io("alu"), nullptr);

  fs::remove_all(test_dir);
}

// Sample Example 6: fine-grained deletion — edge vs pin-edge vs node.
// Each gate operand uses a distinct sink port. The AND output fans out to
// both OR and XOR; XOR's second input is driven independently by graph input c.
// Covers Edge_class::del_edge(), del_sink(driver), del_sink(), del_driver(),
// and del_node(), preserving unrelated connections at each step.
TEST(GraphApiContract, FineGrainedDeletion) {
  hhds::GraphLibrary glib;

  auto and_gio = glib.create_io("and_g");
  and_gio->add_input("a", 0);
  and_gio->add_input("b", 1);
  and_gio->add_output("y", 0);
  auto or_gio = glib.create_io("or_g");
  or_gio->add_input("a", 0);
  or_gio->add_output("y", 0);
  auto xor_gio = glib.create_io("xor_g");
  xor_gio->add_input("a", 0);
  xor_gio->add_input("b", 1);
  xor_gio->add_output("y", 0);

  auto top_gio = glib.create_io("top");
  top_gio->add_input("a", 1);
  top_gio->add_input("b", 2);
  top_gio->add_input("c", 3);
  top_gio->add_output("y1", 1);
  top_gio->add_output("y2", 2);
  auto g = top_gio->create_graph();

  using hhds::attrs::name;

  auto and1 = g->create_node();
  and1.set_subnode(and_gio);
  and1.attr(name).set("and1");
  auto or1 = g->create_node();
  or1.set_subnode(or_gio);
  or1.attr(name).set("or1");
  auto xor1 = g->create_node();
  xor1.set_subnode(xor_gio);
  xor1.attr(name).set("xor1");

  auto and1_in  = and1.create_sink_pin("a");
  auto and1_b   = and1.create_sink_pin("b");
  auto and1_out = and1.create_driver_pin("y");
  auto or1_in   = or1.create_sink_pin("a");
  auto or1_out  = or1.create_driver_pin("y");
  auto xor1_in  = xor1.create_sink_pin("a");
  auto xor1_b   = xor1.create_sink_pin("b");
  auto xor1_out = xor1.create_driver_pin("y");

  auto g_a  = g->get_input_pin("a");
  auto g_b  = g->get_input_pin("b");
  auto g_c  = g->get_input_pin("c");
  auto g_y1 = g->get_output_pin("y1");
  auto g_y2 = g->get_output_pin("y2");

  // AND inputs: one driver per operand pin.
  and1_in.connect_driver(g_a);
  and1_b.connect_driver(g_b);
  // AND output fans out to OR and XOR.
  and1_out.connect_sink(or1_in);
  and1_out.connect_sink(xor1_in);
  // XOR also driven by c.
  xor1_b.connect_driver(g_c);
  // Final outputs.
  or1_out.connect_sink(g_y1);
  xor1_out.connect_sink(g_y2);

  ASSERT_EQ(and1_out.out_edges().size(), 2u);
  EXPECT_EQ(xor1_in.get_driver_pin(), and1_out);
  EXPECT_EQ(xor1_b.get_driver_pin(), g_c);

  // Snapshot a live edge view before deleting one of its edges.
  auto                                outgoing = and1_out.out_edges();
  const std::vector<hhds::Edge_class> edges(outgoing.begin(), outgoing.end());
  for (const auto& edge : edges) {
    if (edge.sink == xor1_in) {
      edge.del_edge();
    }
  }
  EXPECT_FALSE(xor1_in.has_driver());
  EXPECT_EQ(xor1_b.get_driver_pin(), g_c);
  EXPECT_EQ(and1_out.out_edges().size(), 1u);
  EXPECT_TRUE(xor1.is_valid());
  EXPECT_TRUE(xor1_in.is_valid());

  // A named driver deletion leaves the other operand connected.
  and1_in.del_sink(g_a);
  EXPECT_TRUE(and1_in.get_driver_pin().is_invalid());
  EXPECT_EQ(and1_b.get_driver_pin(), g_b);
  and1_in.connect_driver(g_a);

  // Snapshot the connected sink pins before mutating connectivity.
  auto inputs = and1.inp_pins_snapshot();
  ASSERT_EQ(inputs.size(), 2u);
  for (auto sink : inputs) {
    sink.del_sink();
    EXPECT_TRUE(sink.is_valid());
    EXPECT_TRUE(sink.get_driver_pins().empty());
  }
  EXPECT_TRUE(and1.inp_sorted_pins().empty());
  EXPECT_TRUE(and1.is_valid());

  // del_driver(): remove all edges from the driver.
  EXPECT_EQ(and1_out.out_edges().size(), 1u);
  and1_out.del_driver();
  EXPECT_EQ(and1_out.out_edges().size(), 0u);
  EXPECT_EQ(or1_in.get_driver_pins().size(), 0u);
  EXPECT_TRUE(and1.is_valid());
  EXPECT_TRUE(or1.is_valid());

  // del_node(): node + pins + connected edges gone; attributes gone.
  xor1.del_node();
  EXPECT_TRUE(xor1.is_invalid());
  EXPECT_TRUE(xor1_in.is_invalid());
  EXPECT_TRUE(xor1_b.is_invalid());
  EXPECT_TRUE(xor1_out.is_invalid());
  EXPECT_FALSE(xor1.attr(name).has());

  // Graph IO pins belong to the graph, not to xor1.
  EXPECT_TRUE(g_c.is_valid());
  EXPECT_TRUE(g_y2.is_valid());

  // Ordered body traversal skips tombstones.
  std::vector<std::string> survivors;
  for (auto node : g->body().nodes(hhds::Node_order::forward)) {
    EXPECT_TRUE(node.is_valid());
    if (node.attr(name).has()) {
      survivors.push_back(std::string(node.attr(name).get()));
    }
  }
  // xor1 is gone; and1 and or1 remain.
  EXPECT_EQ(survivors.size(), 2u);
}

// Sample Example 3 (hier storage): hier attributes are keyed by hierarchy
// occurrence path, so the same leaf node visited at two instantiations has two
// independent values.
TEST(GraphApiContract, HierAttributesAreKeyedByOccurrence) {
  hhds::GraphLibrary glib;

  auto leaf_io = glib.create_io("leaf");
  auto leaf    = leaf_io->create_graph();
  auto leaf_n  = leaf->create_node();

  auto top_io = glib.create_io("top");
  auto top    = top_io->create_graph();
  auto inst1  = top->create_node();
  auto inst2  = top->create_node();
  inst1.set_subnode(leaf_io);
  inst2.set_subnode(leaf_io);

  using contract_attrs::hbits;
  using hhds::attrs::name;

  // The grouped hierarchy enters the leaf body once per instantiation.
  std::vector<hhds::Occurrence_node> leaf_instances;
  for (auto node : top->grouped_hierarchy().nodes(hhds::Node_order::forward)) {
    if (node.get_current_gid() == leaf->get_gid() && node.get_debug_nid() == leaf_n.get_debug_nid()) {
      leaf_instances.push_back(node);
    }
  }
  ASSERT_EQ(leaf_instances.size(), 2u);

  // Flat attr is shared across instances.
  leaf_instances[0].base_node().attr(name).set("leaf_internal");
  EXPECT_EQ(leaf_instances[1].base_node().attr(name).get(), "leaf_internal");

  // Hier attr is independent per instance.
  leaf_instances[0].attr(hbits).set(11);
  leaf_instances[1].attr(hbits).set(22);
  EXPECT_EQ(leaf_instances[0].attr(hbits).get(), 11);
  EXPECT_EQ(leaf_instances[1].attr(hbits).get(), 22);
}

// A node reached via Pin_class::get_master_node() must expose the same edges as
// one yielded by body iteration — never a silently-empty range
// (small_todo.md). Build a -> b -> c, reach node b through a driver pin (not by
// iterating), and confirm inp_sorted_pins()/out_edges() still see b's fan-in/fan-out.
TEST(GraphApiContract, GetMasterNodeResolvesEdges) {
  hhds::GraphLibrary lib;
  auto               g = lib.create_io("top")->create_graph();
  auto               a = g->create_node();
  auto               b = g->create_node();
  auto               c = g->create_node();
  a.create_driver_pin(1).connect_sink(b.create_sink_pin(1));  // a -> b
  b.create_driver_pin(1).connect_sink(c.create_sink_pin(1));  // b -> c

  // The reported footgun: take a driver pin off an edge and walk ITS master node.
  auto b_via_master = c.get_sink_pin(1).get_driver_pin().get_master_node();
  EXPECT_EQ(b_via_master.get_debug_nid(), b.get_debug_nid());
  EXPECT_EQ(b_via_master.inp_sorted_pins().size(), 1u);  // a -> b
  EXPECT_EQ(b_via_master.out_edges().size(), 1u);        // b -> c
}
