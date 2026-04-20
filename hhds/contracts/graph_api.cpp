// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// HHDS graph public-API contract tests.
//
// These tests mirror the graph examples in sample.md. They serve two purposes:
//   1. Freeze the public API surface that downstream users depend on.
//   2. Act as a short, runnable tutorial for new users.
//
// Only hhds public types (GraphLibrary, GraphIO, Graph, Node, Pin, attrs) are
// exercised. Internal types (Nid, Pid, raw storage entries) are not touched.

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "hhds/attr.hpp"
#include "hhds/attrs/name.hpp"
#include "hhds/graph.hpp"

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

std::string hier_order_key(const hhds::Node& node) {
  return std::to_string(node.get_current_gid()) + ":" + std::to_string(node.get_hier_pos()) + ":"
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

  // forward_class returns Node handles with .attr() support.
  std::vector<std::string> names;
  for (auto node : g->forward_class()) {
    EXPECT_TRUE(node.is_class());
    if (node.attr(name).has()) {
      names.push_back(std::string(node.attr(name).get()));
    }
  }
  EXPECT_EQ(names.size(), 2u);
}

TEST(GraphApiContract, FastTraversalVariants) {
  hhds::GraphLibrary glib;

  auto leaf_io = glib.create_io("leaf_fast");
  auto leaf    = leaf_io->create_graph();
  auto leaf_n  = leaf->create_node();

  auto top_io = glib.create_io("top_fast");
  auto top    = top_io->create_graph();
  auto inst1  = top->create_node();
  auto inst2  = top->create_node();
  inst1.set_subnode(leaf_io);
  inst2.set_subnode(leaf_io);

  bool saw_inst1 = false;
  bool saw_inst2 = false;
  for (auto node : top->fast_class()) {
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

  size_t fast_flat_leaf_visits = 0;
  for (auto node : top->fast_flat()) {
    EXPECT_TRUE(node.is_flat());
    if (node.get_current_gid() == leaf->get_gid() && node.get_debug_nid() == leaf_n.get_debug_nid()) {
      ++fast_flat_leaf_visits;
    }
  }
  EXPECT_EQ(fast_flat_leaf_visits, 1u);

  size_t fast_hier_leaf_visits = 0;
  for (auto node : top->fast_hier()) {
    EXPECT_TRUE(node.is_hier());
    if (node.get_current_gid() == leaf->get_gid() && node.get_debug_nid() == leaf_n.get_debug_nid()) {
      ++fast_hier_leaf_visits;
    }
  }
  EXPECT_EQ(fast_hier_leaf_visits, 2u);
}

TEST(GraphApiContract, DefaultTraversalsStartAtConstAndSkipIo) {
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

  const auto forward = class_order(g->forward_class());
  const auto fast    = class_order(g->fast_class());

  ASSERT_GE(forward.size(), 1u);
  ASSERT_GE(fast.size(), 1u);
  EXPECT_EQ(forward.front(), hhds::Graph::CONST_NODE);
  EXPECT_EQ(fast.front(), hhds::Graph::CONST_NODE);
  EXPECT_EQ(forward, fast);

  for (auto nid : forward) {
    EXPECT_NE(nid, hhds::Graph::INPUT_NODE);
    EXPECT_NE(nid, hhds::Graph::OUTPUT_NODE);
  }
}

TEST(GraphApiContract, FastHierTouchesForwardHierNodesInForwardOrder) {
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

  EXPECT_EQ(hier_order(top->forward_hier()), hier_order(top->fast_hier()));
}

TEST(GraphApiContract, FastFlatTouchesForwardFlatNodes) {
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

  auto forward = flat_order(top->forward_flat());
  auto fast    = flat_order(top->fast_flat());
  std::sort(forward.begin(), forward.end());
  std::sort(fast.begin(), fast.end());
  EXPECT_EQ(forward, fast);
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

  auto dst0_inp = dst0_in.inp_edges();
  ASSERT_EQ(dst0_inp.size(), 1u);
  EXPECT_EQ(dst0_inp[0].driver, src0_out);
  EXPECT_EQ(dst0_inp[0].sink, dst0_in);

  auto src1_outp = src1_out.out_edges();
  ASSERT_EQ(src1_outp.size(), 1u);
  EXPECT_EQ(src1_outp[0].driver, src1_out);
  EXPECT_EQ(src1_outp[0].sink, dst1_in);
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
  for (auto n : g->forward_class()) {
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
// Note: the sink and driver pins use distinct non-zero port ids. Port 0 is a
// special "node-as-pin" entity shared by the node and any port-0 pin, so
// using port 0 for both an input and an output would alias them.
TEST(GraphApiContract, PinsAttributesAndEdgeIteration) {
  hhds::GraphLibrary glib;

  auto and_gio = glib.create_io("and_gate");
  and_gio->add_input("a", 1);
  and_gio->add_output("y", 2);

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
  and1_in.connect_driver(g_y);
  and1_out.connect_sink(g_z);

  // Pin-level edge iteration: all inputs land on and1_in; all outputs leave
  // from and1_out.
  auto inp = and1_in.inp_edges();
  EXPECT_EQ(inp.size(), 2u);
  for (const auto& edge : inp) {
    EXPECT_EQ(edge.sink, and1_in);
  }

  auto outp = and1_out.out_edges();
  EXPECT_EQ(outp.size(), 1u);
  EXPECT_EQ(outp.front().driver, and1_out);
  EXPECT_EQ(outp.front().sink, g_z);

  // Pin iteration — enumerate pins on a node directly.
  EXPECT_EQ(and1.inp_pins().size(), 1u);
  EXPECT_EQ(and1.out_pins().size(), 1u);
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
  EXPECT_TRUE(fs::exists(fs::path(test_dir) / "graph_1" / "body.bin"));

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
//
//     a ─┐
//        ├──→ [AND].a ──→ [AND].y ──→ [OR].a ──→ [OR].y ──→ y1
//     b ─┘
//              c ─────────→ [XOR].a ──→ [XOR].y ──→ y2
//
// Tests cover Edge::del_edge(), del_sink(), del_driver(), and del_node().
TEST(GraphApiContract, FineGrainedDeletion) {
  hhds::GraphLibrary glib;

  auto and_gio = glib.create_io("and_g");
  and_gio->add_input("a", 0);
  and_gio->add_output("y", 0);
  auto or_gio = glib.create_io("or_g");
  or_gio->add_input("a", 0);
  or_gio->add_output("y", 0);
  auto xor_gio = glib.create_io("xor_g");
  xor_gio->add_input("a", 0);
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
  auto and1_out = and1.create_driver_pin("y");
  auto or1_in   = or1.create_sink_pin("a");
  auto or1_out  = or1.create_driver_pin("y");
  auto xor1_in  = xor1.create_sink_pin("a");
  auto xor1_out = xor1.create_driver_pin("y");

  auto g_a  = g->get_input_pin("a");
  auto g_b  = g->get_input_pin("b");
  auto g_c  = g->get_input_pin("c");
  auto g_y1 = g->get_output_pin("y1");
  auto g_y2 = g->get_output_pin("y2");

  // AND inputs: a and b both drive the single commutative input "a".
  and1_in.connect_driver(g_a);
  and1_in.connect_driver(g_b);
  // AND output fans out to OR and XOR.
  and1_out.connect_sink(or1_in);
  and1_out.connect_sink(xor1_in);
  // XOR also driven by c.
  xor1_in.connect_driver(g_c);
  // Final outputs.
  or1_out.connect_sink(g_y1);
  xor1_out.connect_sink(g_y2);

  // State check before any deletes.
  EXPECT_EQ(and1_out.out_edges().size(), 2u);  // AND.y → OR.a, AND.y → XOR.a
  EXPECT_EQ(xor1_in.inp_edges().size(), 2u);   // AND.y → XOR.a, c → XOR.a

  // Edge::del_edge(): remove only that one enumerated edge.
  for (const auto& edge : xor1_in.inp_edges()) {
    if (edge.driver == and1_out) {
      edge.del_edge();
      break;
    }
  }
  EXPECT_EQ(xor1_in.inp_edges().size(), 1u);
  EXPECT_EQ(and1_out.out_edges().size(), 1u);
  EXPECT_TRUE(xor1.is_valid());
  EXPECT_TRUE(xor1_in.is_valid());

  // del_sink(): remove all edges into the sink; pins and node survive.
  EXPECT_EQ(and1_in.inp_edges().size(), 2u);
  and1_in.del_sink();
  EXPECT_EQ(and1_in.inp_edges().size(), 0u);
  EXPECT_TRUE(and1.is_valid());
  EXPECT_TRUE(and1_in.is_valid());

  // del_driver(): remove all edges from the driver.
  EXPECT_EQ(and1_out.out_edges().size(), 1u);
  and1_out.del_driver();
  EXPECT_EQ(and1_out.out_edges().size(), 0u);
  EXPECT_EQ(or1_in.inp_edges().size(), 0u);
  EXPECT_TRUE(and1.is_valid());
  EXPECT_TRUE(or1.is_valid());

  // del_node(): node + pins + connected edges gone; attributes gone.
  xor1.del_node();
  EXPECT_TRUE(xor1.is_invalid());
  EXPECT_TRUE(xor1_in.is_invalid());
  EXPECT_TRUE(xor1_out.is_invalid());
  EXPECT_FALSE(xor1.attr(name).has());

  // Graph IO pins belong to the graph, not to xor1.
  EXPECT_TRUE(g_c.is_valid());
  EXPECT_TRUE(g_y2.is_valid());

  // forward_class skips tombstones.
  std::vector<std::string> survivors;
  for (auto node : g->forward_class()) {
    EXPECT_TRUE(node.is_valid());
    if (node.attr(name).has()) {
      survivors.push_back(std::string(node.attr(name).get()));
    }
  }
  // xor1 is gone; and1 and or1 remain.
  EXPECT_EQ(survivors.size(), 2u);
}

// Sample Example 3 (hier storage): hier attributes are keyed by hierarchy
// position, so the same leaf node visited at two instantiations has two
// independent values.
TEST(GraphApiContract, HierAttributesAreKeyedByHierPosition) {
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

  // forward_hier enters the leaf body once per instantiation.
  std::vector<hhds::Node> leaf_instances;
  for (auto node : top->forward_hier()) {
    if (node.get_current_gid() == leaf->get_gid() && node.get_debug_nid() == leaf_n.get_debug_nid()) {
      leaf_instances.push_back(node);
    }
  }
  ASSERT_EQ(leaf_instances.size(), 2u);

  // Flat attr is shared across instances.
  leaf_instances[0].attr(name).set("leaf_internal");
  EXPECT_EQ(leaf_instances[1].attr(name).get(), "leaf_internal");

  // Hier attr is independent per instance.
  leaf_instances[0].attr(hbits).set(11);
  leaf_instances[1].attr(hbits).set(22);
  EXPECT_EQ(leaf_instances[0].attr(hbits).get(), 11);
  EXPECT_EQ(leaf_instances[1].attr(hbits).get(), 22);
}
