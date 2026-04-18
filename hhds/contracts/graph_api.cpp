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

// Sample Example 3: declaring custom attributes. Flat vs hier storage
// is picked at compile time via Tag::storage.
TEST(GraphApiContract, CustomAttributeDeclarations) {
  hhds::GraphLibrary glib;
  auto               gio = glib.create_io("arith");
  auto               g   = gio->create_graph();

  auto node = g->create_node();

  using hhds::attrs::name;
  using contract_attrs::bits;

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

  using hhds::attrs::name;
  using contract_attrs::delay;

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

  and1_in.connect_driver(g_x);
  and1_in.connect_driver(g_y);
  and1_out.connect_sink(g_z);

  // Pin-level edge iteration: all inputs land on and1_in; all outputs leave
  // from and1_out.
  auto inp = and1_in.inp_edges();
  EXPECT_EQ(inp.size(), 2u);
  for (const auto& edge : inp) {
    EXPECT_EQ(edge.sink_pin(), and1_in);
  }

  auto outp = and1_out.out_edges();
  EXPECT_EQ(outp.size(), 1u);
  EXPECT_EQ(outp.front().driver_pin(), and1_out);
  EXPECT_EQ(outp.front().sink_pin(), g_z);

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

  using hhds::attrs::name;
  using contract_attrs::bits;

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

  // Save body + attribute maps.
  g->save_body(test_dir);
  EXPECT_TRUE(fs::exists(fs::path(test_dir) / "body.bin"));

  // Load into a fresh graph from a new declaration.
  auto gio2 = glib.create_io("alu_loaded");
  auto g2   = gio2->create_graph();
  g2->load_body(test_dir);

  auto loaded_add = hhds::Node(g2.get(), add.get_raw_nid());
  auto loaded_sub = hhds::Node(g2.get(), sub.get_raw_nid());
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
// Tests cover del_sink(driver), del_sink(), del_driver(), and del_node().
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

  // del_sink(specific_driver): remove only that one edge into the sink.
  xor1_in.del_sink(and1_out);
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

  using hhds::attrs::name;
  using contract_attrs::hbits;

  // forward_hier enters the leaf body once per instantiation.
  std::vector<hhds::Node> leaf_instances;
  for (auto node : top->forward_hier()) {
    if (node.get_current_gid() == leaf->get_gid() && node.get_raw_nid() == leaf_n.get_raw_nid()) {
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
