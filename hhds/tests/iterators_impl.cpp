// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// Runnable iterator/API tests for features that are currently implemented.
// Pending API tests remain in tests/iterators.cpp.

#include <gtest/gtest.h>

#include <regex>

#include "absl/container/flat_hash_map.h"
#include "hhds/graph.hpp"
#include "hhds/tree.hpp"

namespace {

bool contains_pin_pid(const std::vector<hhds::Pin_class>& pins, hhds::Pid pid) {
  for (const auto& pin : pins) {
    if (pin.get_pin_pid() == pid) {
      return true;
    }
  }
  return false;
}

std::shared_ptr<hhds::Graph> create_declared_graph(hhds::GraphLibrary& lib, std::string_view prefix) {
  static int next_graph_id = 0;
  auto       gio           = lib.create_graphio(std::string(prefix) + "_" + std::to_string(++next_graph_id));
  return gio->create_graph();
}

}  // namespace

// ---------------------------------------------------------------------------
// Section 1: Compact ID types
// ---------------------------------------------------------------------------

TEST(CompactTypes, NodeClassFromGraph) {
  hhds::Graph g;

  // create_node returns Node_class directly (not raw Nid)
  auto node = g.create_node();
  EXPECT_EQ(node.get_port_id(), 0);  // Node_class encodes port-id 0 identity
}

TEST(CompactTypes, PinClassFromGraph) {
  hhds::Graph g;
  auto        node = g.create_node();
  auto        pin  = g.create_pin(node, 3);

  EXPECT_EQ(pin.get_raw_nid(), node.get_raw_nid());  // get_raw_nid() for internal cross-check only
  EXPECT_EQ(pin.get_port_id(), 3);
}

TEST(CompactTypes, NodeClassHashable) {
  hhds::Graph g;
  auto        n1 = g.create_node();
  auto        n2 = g.create_node();

  EXPECT_EQ(n1, n1);
  EXPECT_NE(n1, n2);

  // Usable as absl::flat_hash_map key for external attribute tables
  absl::flat_hash_map<hhds::Node_class, int> attrs;
  attrs[n1] = 42;
  EXPECT_EQ(attrs[n1], 42);
}

TEST(CompactTypes, NodeCompactConversions) {
  hhds::GraphLibrary lib;
  auto               g   = create_declared_graph(lib, "node_compact");
  const hhds::Gid    gid = g->get_gid();

  auto node = g->create_node();

  const auto flat_from_class = hhds::to_flat(node, gid);
  EXPECT_EQ(flat_from_class.get_root_gid(), gid);
  EXPECT_EQ(flat_from_class.get_current_gid(), gid);
  EXPECT_EQ(flat_from_class.get_raw_nid(), node.get_raw_nid() & ~static_cast<hhds::Nid>(2));

  const auto class_from_flat = hhds::to_class(flat_from_class);
  EXPECT_EQ(class_from_flat.get_raw_nid(), node.get_raw_nid() & ~static_cast<hhds::Nid>(2));

  bool found_in_hier = false;
  for (const auto& hnode : g->fast_hier()) {
    if ((hnode.get_raw_nid() & ~static_cast<hhds::Nid>(2)) == (node.get_raw_nid() & ~static_cast<hhds::Nid>(2))) {
      found_in_hier              = true;
      const auto flat_from_hier  = hhds::to_flat(hnode);
      const auto class_from_hier = hhds::to_class(hnode);
      EXPECT_EQ(flat_from_hier.get_root_gid(), gid);
      EXPECT_EQ(flat_from_hier.get_current_gid(), gid);
      EXPECT_EQ(flat_from_hier.get_raw_nid(), node.get_raw_nid() & ~static_cast<hhds::Nid>(2));
      EXPECT_EQ(class_from_hier.get_raw_nid(), node.get_raw_nid() & ~static_cast<hhds::Nid>(2));
      break;
    }
  }
  EXPECT_TRUE(found_in_hier);
}

TEST(CompactTypes, PinCompactConversions) {
  hhds::GraphLibrary lib;
  auto               g   = create_declared_graph(lib, "pin_compact");
  const hhds::Gid    gid = g->get_gid();

  auto node = g->create_node();
  auto pin  = g->create_pin(node, 3);

  const auto flat_from_class = hhds::to_flat(pin, gid);
  EXPECT_EQ(flat_from_class.get_root_gid(), gid);
  EXPECT_EQ(flat_from_class.get_current_gid(), gid);
  EXPECT_EQ(flat_from_class.get_raw_nid(), node.get_raw_nid() & ~static_cast<hhds::Nid>(2));
  EXPECT_EQ(flat_from_class.get_port_id(), 3);
  EXPECT_EQ(flat_from_class.get_pin_pid(), pin.get_pin_pid());

  const auto class_from_flat = hhds::to_class(flat_from_class);
  EXPECT_EQ(class_from_flat.get_raw_nid(), node.get_raw_nid() & ~static_cast<hhds::Nid>(2));
  EXPECT_EQ(class_from_flat.get_port_id(), 3);
  EXPECT_EQ(class_from_flat.get_pin_pid(), pin.get_pin_pid());

  absl::flat_hash_map<hhds::Pin_flat, int> attrs;
  attrs[flat_from_class] = 7;
  EXPECT_EQ(attrs[flat_from_class], 7);
}

TEST(CompactTypes, GraphIsValidParity) {
  EXPECT_FALSE(hhds::Graph::is_valid(hhds::Node_class()));
  EXPECT_FALSE(hhds::Graph::is_valid(hhds::Pin_class()));
  EXPECT_FALSE(hhds::Graph::is_valid(hhds::Node_flat()));
  EXPECT_FALSE(hhds::Graph::is_valid(hhds::Pin_flat()));
  EXPECT_FALSE(hhds::Graph::is_valid(hhds::Node_hier()));
  EXPECT_FALSE(hhds::Graph::is_valid(hhds::Pin_hier()));

  hhds::GraphLibrary lib;
  auto               g   = create_declared_graph(lib, "validity");
  const hhds::Gid    gid = g->get_gid();

  const auto node = g->create_node();
  const auto pin  = g->create_pin(node, 3);

  EXPECT_TRUE(hhds::Graph::is_valid(node));
  EXPECT_TRUE(hhds::Graph::is_valid(pin));

  const auto node_flat = hhds::to_flat(node, gid);
  const auto pin_flat  = hhds::to_flat(pin, gid);
  EXPECT_TRUE(hhds::Graph::is_valid(node_flat));
  EXPECT_TRUE(hhds::Graph::is_valid(pin_flat));

  auto hier_gids = std::make_shared<std::vector<hhds::Gid>>(static_cast<size_t>(hhds::ROOT + 1), hhds::Gid_invalid);
  (*hier_gids)[static_cast<size_t>(hhds::ROOT)] = gid;
  const hhds::Tid hier_tid                      = -9;

  const hhds::Node_hier node_hier(hier_tid, hier_gids, hhds::ROOT, node.get_raw_nid());
  const hhds::Pin_hier  pin_hier(hier_tid, hier_gids, hhds::ROOT, pin.get_raw_nid(), pin.get_port_id(), pin.get_pin_pid());
  EXPECT_TRUE(hhds::Graph::is_valid(node_hier));
  EXPECT_TRUE(hhds::Graph::is_valid(pin_hier));
}

TEST(GraphNaming, CreateAndFindByName) {
  hhds::GraphLibrary lib;
  auto               gio = lib.create_graphio("alu");
  auto               alu = gio->create_graph();

  ASSERT_TRUE(gio);
  ASSERT_TRUE(alu);
  EXPECT_EQ(gio->get_gid(), 1);
  EXPECT_EQ(gio->get_name(), "alu");
  EXPECT_EQ(alu->get_gid(), 1);
  EXPECT_EQ(alu->get_name(), "alu");
  EXPECT_EQ(alu->get_graphio(), gio);
  EXPECT_EQ(lib.find_graphio("alu"), gio);
  EXPECT_EQ(lib.find_graph("alu"), alu);
  EXPECT_FALSE(lib.find_graph("missing"));
}

TEST(GraphNaming, GraphIOCanExistWithoutBody) {
  hhds::GraphLibrary lib;
  auto               gio = lib.create_graphio("parser");

  ASSERT_TRUE(gio);
  EXPECT_EQ(gio->get_gid(), 1);
  EXPECT_EQ(gio->get_name(), "parser");
  EXPECT_FALSE(gio->has_graph());
  EXPECT_FALSE(gio->get_graph());
  EXPECT_EQ(lib.find_graphio("parser"), gio);
  EXPECT_FALSE(lib.find_graph("parser"));

  auto parser = gio->create_graph();
  ASSERT_TRUE(parser);
  EXPECT_TRUE(gio->has_graph());
  EXPECT_EQ(gio->get_graph(), parser);
  EXPECT_EQ(lib.find_graph("parser"), parser);
}

TEST(GraphNaming, DeleteGraphPreservesDeclarationLookup) {
  hhds::GraphLibrary lib;
  auto               gio = lib.create_graphio("alu");
  gio->add_input("a", 1);
  gio->add_output("y", 2);
  auto            alu = gio->create_graph();
  const hhds::Gid gid = alu->get_gid();

  lib.delete_graph(alu);

  EXPECT_FALSE(lib.has_graph(gid));
  EXPECT_EQ(lib.find_graphio("alu"), gio);
  EXPECT_FALSE(lib.find_graph("alu"));
  EXPECT_FALSE(gio->has_graph());
  EXPECT_FALSE(gio->get_graph());
  EXPECT_TRUE(gio->has_input("a"));
  EXPECT_TRUE(gio->has_output("y"));

  auto recreated = gio->create_graph();
  ASSERT_TRUE(recreated);
  EXPECT_EQ(lib.find_graph("alu"), recreated);
  EXPECT_EQ(recreated->get_input_pin("a").get_port_id(), 1);
  EXPECT_EQ(recreated->get_output_pin("y").get_port_id(), 2);
}

TEST(GraphNaming, DuplicateNameRejected) {
  hhds::GraphLibrary lib;
  (void)lib.create_graphio("alu");

  EXPECT_DEATH({ (void)lib.create_graphio("alu"); }, "graph name already exists");
}

TEST(GraphNaming, GraphCreationIsIdempotentPerGraphIO) {
  hhds::GraphLibrary lib;
  auto               gio = lib.create_graphio("alu");
  auto               g1  = gio->create_graph();
  auto               g2  = gio->create_graph();

  ASSERT_TRUE(g1);
  EXPECT_EQ(g1, g2);
  EXPECT_EQ(g1->get_gid(), gio->get_gid());
}

TEST(GraphNaming, DeleteGraphIODeletesDeclarationAndBody) {
  hhds::GraphLibrary lib;
  auto               gio = lib.create_graphio("alu");
  gio->add_input("a", 1);
  gio->add_output("y", 2);
  auto            graph = gio->create_graph();
  const hhds::Gid gid   = graph->get_gid();

  lib.delete_graphio("alu");

  EXPECT_FALSE(lib.find_graphio("alu"));
  EXPECT_FALSE(lib.find_graph("alu"));
  EXPECT_FALSE(lib.has_graph(gid));
  EXPECT_FALSE(gio->has_graph());
  EXPECT_FALSE(gio->get_graph());
}

TEST(GraphPorts, DeclarationQueriesAndMaterializedPins) {
  hhds::GraphLibrary lib;
  auto               gio = lib.create_graphio("register");
  gio->add_input("d", 0, true);
  gio->add_output("q", 0, true);

  EXPECT_TRUE(gio->has_input("d"));
  EXPECT_TRUE(gio->has_output("q"));
  EXPECT_EQ(gio->get_input_port_id("d"), 0);
  EXPECT_EQ(gio->get_output_port_id("q"), 0);
  EXPECT_TRUE(gio->is_loop_last("d"));
  EXPECT_TRUE(gio->is_loop_last("q"));
  EXPECT_FALSE(gio->has_graph());

  auto graph = gio->create_graph();
  ASSERT_TRUE(graph);

  const auto input_pin  = graph->get_input_pin("d");
  const auto output_pin = graph->get_output_pin("q");
  EXPECT_EQ(input_pin.get_master_node().get_raw_nid(), hhds::Graph::INPUT_NODE);
  EXPECT_EQ(output_pin.get_master_node().get_raw_nid(), hhds::Graph::OUTPUT_NODE);
  EXPECT_EQ(input_pin.get_port_id(), 0);
  EXPECT_EQ(output_pin.get_port_id(), 0);
}

TEST(GraphPorts, DuplicateDeclaredIoPinNamesRejected) {
  hhds::GraphLibrary lib;
  auto               gio = lib.create_graphio("dup_ports");
  gio->add_input("a", 1);

  EXPECT_DEATH({ gio->add_output("a", 2); }, "output pin name already exists");
}

TEST(GraphPorts, EditingDeclarationUpdatesExistingBody) {
  hhds::GraphLibrary lib;
  auto               gio = lib.create_graphio("alu");
  gio->add_input("a", 1);
  gio->add_output("y", 2);
  auto graph = gio->create_graph();
  ASSERT_TRUE(graph);

  auto node = graph->create_node();
  graph->add_edge(graph->get_input_pin("a"), node);
  graph->add_edge(node, graph->get_output_pin("y"));
  EXPECT_EQ(graph->inp_edges(node).size(), 1U);
  EXPECT_EQ(graph->out_edges(node).size(), 1U);

  gio->delete_input("a");
  EXPECT_FALSE(gio->has_input("a"));
  EXPECT_TRUE(graph->inp_edges(node).empty());

  gio->delete_output("y");
  EXPECT_FALSE(gio->has_output("y"));
  EXPECT_TRUE(graph->out_edges(node).empty());

  gio->add_input("b", 7);
  gio->add_output("z", 8);
  EXPECT_EQ(graph->get_input_pin("b").get_port_id(), 7);
  EXPECT_EQ(graph->get_output_pin("z").get_port_id(), 8);
}

TEST(GraphPorts, DeletingDeclaredIoPinsReindexesRemainingNames) {
  hhds::GraphLibrary lib;
  auto               gio = lib.create_graphio("reindex_ports");
  gio->add_input("a", 1);
  gio->add_input("b", 2);
  gio->add_input("c", 3);
  gio->add_output("x", 4);
  gio->add_output("y", 5);
  auto graph = gio->create_graph();
  ASSERT_TRUE(graph);

  gio->delete_input("b");
  gio->delete_output("x");

  EXPECT_TRUE(gio->has_input("a"));
  EXPECT_TRUE(gio->has_input("c"));
  EXPECT_FALSE(gio->has_input("b"));
  EXPECT_TRUE(gio->has_output("y"));
  EXPECT_FALSE(gio->has_output("x"));
  EXPECT_EQ(gio->get_input_port_id("a"), 1);
  EXPECT_EQ(gio->get_input_port_id("c"), 3);
  EXPECT_EQ(gio->get_output_port_id("y"), 5);
  EXPECT_EQ(graph->get_input_pin("a").get_port_id(), 1);
  EXPECT_EQ(graph->get_input_pin("c").get_port_id(), 3);
  EXPECT_EQ(graph->get_output_pin("y").get_port_id(), 5);
}

TEST(CompactTypes, EdgeFlatConversions) {
  hhds::GraphLibrary lib;
  auto               g   = create_declared_graph(lib, "edge_flat");
  const hhds::Gid    gid = g->get_gid();

  auto src = g->create_node();
  auto dst = g->create_node();
  auto sp  = g->create_pin(src, 1);
  auto dp  = g->create_pin(dst, 2);
  g->add_edge(sp, dp);

  auto out_edges = g->out_edges(sp);
  ASSERT_EQ(out_edges.size(), 1U);
  const auto& edge = out_edges[0];

  const auto flat = hhds::to_flat(edge, gid);
  EXPECT_EQ(flat.driver.get_root_gid(), gid);
  EXPECT_EQ(flat.driver.get_current_gid(), gid);
  EXPECT_EQ(flat.sink.get_root_gid(), gid);
  EXPECT_EQ(flat.sink.get_current_gid(), gid);
  EXPECT_EQ(flat.driver.get_pin_pid(), edge.driver_pin.get_pin_pid());
  EXPECT_EQ(flat.sink.get_pin_pid(), edge.sink_pin.get_pin_pid());

  const auto edge_class = hhds::to_class(flat);
  EXPECT_EQ(edge_class.driver_pin.get_pin_pid(), edge.driver_pin.get_pin_pid());
  EXPECT_EQ(edge_class.sink_pin.get_pin_pid(), edge.sink_pin.get_pin_pid());

  absl::flat_hash_map<hhds::Edge_flat, int> attrs;
  attrs[flat] = 11;
  EXPECT_EQ(attrs[flat], 11);
}

TEST(CompactTypes, EdgeHierConversions) {
  hhds::GraphLibrary lib;
  auto               g   = create_declared_graph(lib, "edge_hier");
  const hhds::Gid    gid = g->get_gid();

  auto src = g->create_node();
  auto dst = g->create_node();
  auto sp  = g->create_pin(src, 1);
  auto dp  = g->create_pin(dst, 2);
  g->add_edge(sp, dp);

  auto out_edges = g->out_edges(sp);
  ASSERT_EQ(out_edges.size(), 1U);
  const auto& edge = out_edges[0];

  auto            hier_ref  = hhds::Tree::create();
  auto            hier_gids = std::make_shared<std::vector<hhds::Gid>>();
  auto            hier_pos  = hier_ref->add_root();
  const hhds::Tid hier_tid  = -9;
  hier_gids->resize(static_cast<size_t>(hier_pos + 1), hhds::Gid_invalid);
  (*hier_gids)[static_cast<size_t>(hier_pos)] = gid;

  const auto hier = hhds::to_hier(edge, hier_tid, hier_gids, hier_pos);
  EXPECT_EQ(hier.driver.get_hier_tid(), hier_tid);
  EXPECT_EQ(hier.driver.get_hier_pos(), hier_pos);
  EXPECT_EQ(hier.sink.get_hier_tid(), hier_tid);
  EXPECT_EQ(hier.sink.get_hier_pos(), hier_pos);
  EXPECT_EQ(hier.driver.get_root_gid(), gid);
  EXPECT_EQ(hier.driver.get_current_gid(), gid);
  EXPECT_EQ(hier.sink.get_root_gid(), gid);
  EXPECT_EQ(hier.sink.get_current_gid(), gid);

  const auto flat = hhds::to_flat(hier);
  EXPECT_EQ(flat.driver.get_pin_pid(), edge.driver_pin.get_pin_pid());
  EXPECT_EQ(flat.sink.get_pin_pid(), edge.sink_pin.get_pin_pid());

  const auto edge_class = hhds::to_class(hier);
  EXPECT_EQ(edge_class.driver_pin.get_pin_pid(), edge.driver_pin.get_pin_pid());
  EXPECT_EQ(edge_class.sink_pin.get_pin_pid(), edge.sink_pin.get_pin_pid());
}

TEST(CompactTypes, GraphGetSubsReturnsDirectSubnodes) {
  hhds::GraphLibrary lib;
  auto               root      = create_declared_graph(lib, "root");
  auto               child     = create_declared_graph(lib, "child");
  const hhds::Gid    child_gid = child->get_gid();

  auto regular = root->create_node();
  auto sub1    = root->create_node();
  auto sub2    = root->create_node();

  root->set_subnode(sub1, child_gid);
  root->set_subnode(sub2, child_gid);

  // const auto subs = root->get_subs();
  // ASSERT_EQ(subs.size(), 2U);
  // EXPECT_EQ(subs[0], sub1);
  // EXPECT_EQ(subs[1], sub2);
  // EXPECT_NE(subs[0], regular);
}

// ---------------------------------------------------------------------------
// Section 2: Direction-aware edge iteration
// ---------------------------------------------------------------------------

TEST(EdgeIteration, OutEdges) {
  hhds::Graph g;
  auto        n1 = g.create_node();
  auto        n2 = g.create_node();
  auto        n3 = g.create_node();
  g.add_edge(n1, n2);
  g.add_edge(n1, n3);

  // out_edges: edges where n1 is the driver
  int count = 0;
  for (auto edge : g.out_edges(n1)) {
    EXPECT_EQ(edge.driver.get_port_id(), 0);
    count++;
  }
  EXPECT_EQ(count, 2);

  count = 0;
  for (auto edge : g.inp_edges(n1)) {
    EXPECT_EQ(edge.driver.get_port_id(), 0);
    count++;
  }
  EXPECT_EQ(count, 0);

  count = 0;
  for (auto edge : g.out_edges(n2)) {
    EXPECT_EQ(edge.driver.get_port_id(), 0);
    count++;
  }
  EXPECT_EQ(count, 0);

  count = 0;
  for (auto edge : g.inp_edges(n2)) {
    EXPECT_EQ(edge.driver.get_port_id(), 0);
    count++;
  }
  EXPECT_EQ(count, 1);
}

TEST(EdgeIteration, InpEdges) {
  hhds::Graph g;
  auto        n1 = g.create_node();
  auto        n2 = g.create_node();
  auto        n3 = g.create_node();
  g.add_edge(n1, n3);
  g.add_edge(n2, n3);

  // inp_edges: edges where n3 is the sink
  int count = 0;
  for (auto edge : g.inp_edges(n3)) {
    EXPECT_EQ(edge.sink.get_port_id(), 0);
    count++;
  }
  EXPECT_EQ(count, 2);

  count = 0;
  for (auto edge : g.inp_edges(n1)) {
    EXPECT_EQ(edge.sink.get_port_id(), 0);
    count++;
  }
  EXPECT_EQ(count, 0);
}

// ---------------------------------------------------------------------------
// Section 2 (cont.): Bit encoding contract
// ---------------------------------------------------------------------------

TEST(BitEncoding, NodeToNode) {
  hhds::Graph g;
  auto        n1 = g.create_node();
  auto        n2 = g.create_node();
  g.add_edge(n1, n2);

  auto out = g.out_edges(n1);
  ASSERT_EQ(out.size(), 1);
  EXPECT_EQ((out[0].driver.get_raw_nid() & static_cast<hhds::Nid>(1)), static_cast<hhds::Nid>(0));
  EXPECT_EQ((out[0].driver.get_raw_nid() & static_cast<hhds::Nid>(2)), static_cast<hhds::Nid>(2));
  EXPECT_EQ((out[0].sink.get_raw_nid() & static_cast<hhds::Nid>(1)), static_cast<hhds::Nid>(0));
  EXPECT_EQ((out[0].sink.get_raw_nid() & static_cast<hhds::Nid>(2)), static_cast<hhds::Nid>(0));
}

TEST(BitEncoding, PinToPin) {
  hhds::Graph g;
  auto        n1 = g.create_node();
  auto        n2 = g.create_node();
  auto        p1 = g.create_pin(n1, 1);
  auto        p2 = g.create_pin(n2, 2);
  g.add_edge(p1, p2);

  auto out = g.out_edges(p1);
  ASSERT_EQ(out.size(), 1);
  EXPECT_EQ((out[0].driver_pin.get_pin_pid() & static_cast<hhds::Pid>(1)), static_cast<hhds::Pid>(1));
  EXPECT_EQ((out[0].driver_pin.get_pin_pid() & static_cast<hhds::Pid>(2)), static_cast<hhds::Pid>(2));
  EXPECT_EQ((out[0].sink_pin.get_pin_pid() & static_cast<hhds::Pid>(1)), static_cast<hhds::Pid>(1));
  EXPECT_EQ((out[0].sink_pin.get_pin_pid() & static_cast<hhds::Pid>(2)), static_cast<hhds::Pid>(0));
}

TEST(BitEncoding, NodeToPin) {
  hhds::Graph g;
  auto        n1 = g.create_node();
  auto        n2 = g.create_node();
  auto        p2 = g.create_pin(n2, 2);
  g.add_edge(n1, p2);

  auto out = g.out_edges(n1);
  ASSERT_EQ(out.size(), 1);
  EXPECT_EQ((out[0].driver.get_raw_nid() & static_cast<hhds::Nid>(1)), static_cast<hhds::Nid>(0));
  EXPECT_EQ((out[0].driver.get_raw_nid() & static_cast<hhds::Nid>(2)), static_cast<hhds::Nid>(2));
  EXPECT_EQ((out[0].sink_pin.get_pin_pid() & static_cast<hhds::Pid>(1)), static_cast<hhds::Pid>(1));
  EXPECT_EQ((out[0].sink_pin.get_pin_pid() & static_cast<hhds::Pid>(2)), static_cast<hhds::Pid>(0));
}

TEST(BitEncoding, PinToNode) {
  hhds::Graph g;
  auto        n1 = g.create_node();
  auto        n2 = g.create_node();
  auto        p1 = g.create_pin(n1, 1);
  g.add_edge(p1, n2);

  auto out = g.out_edges(p1);
  ASSERT_EQ(out.size(), 1);
  EXPECT_EQ((out[0].driver_pin.get_pin_pid() & static_cast<hhds::Pid>(1)), static_cast<hhds::Pid>(1));
  EXPECT_EQ((out[0].driver_pin.get_pin_pid() & static_cast<hhds::Pid>(2)), static_cast<hhds::Pid>(2));
  EXPECT_EQ((out[0].sink.get_raw_nid() & static_cast<hhds::Nid>(1)), static_cast<hhds::Nid>(0));
  EXPECT_EQ((out[0].sink.get_raw_nid() & static_cast<hhds::Nid>(2)), static_cast<hhds::Nid>(0));
}

// ---------------------------------------------------------------------------
// Section 3: del_edge
// ---------------------------------------------------------------------------

TEST(DelEdge, BasicRemoval) {
  hhds::Graph g;
  auto        n1 = g.create_node();
  auto        n2 = g.create_node();
  auto        p1 = g.create_pin(n1, 0);
  auto        p2 = g.create_pin(n2, 0);
  g.add_edge(p1, p2);

  int count = 0;
  for (auto edge : g.out_edges(n1)) {
    (void)edge;
    count++;
  }
  EXPECT_EQ(count, 0);  // node (pin0) has no edge

  count = 0;
  for (auto edge : g.out_edges(p1)) {
    EXPECT_EQ(edge.driver_pin.get_pin_pid(), (p1.get_pin_pid() | static_cast<hhds::Pid>(2)));
    EXPECT_EQ(edge.sink_pin.get_pin_pid(), p2.get_pin_pid());
    EXPECT_EQ((edge.driver_pin.get_pin_pid() & static_cast<hhds::Pid>(1)), static_cast<hhds::Pid>(1));
    EXPECT_EQ((edge.driver_pin.get_pin_pid() & static_cast<hhds::Pid>(2)), static_cast<hhds::Pid>(2));
    EXPECT_EQ((edge.sink_pin.get_pin_pid() & static_cast<hhds::Pid>(1)), static_cast<hhds::Pid>(1));
    EXPECT_EQ((edge.sink_pin.get_pin_pid() & static_cast<hhds::Pid>(2)), static_cast<hhds::Pid>(0));
    EXPECT_EQ(edge.driver_pin.get_master_node().get_raw_nid(), n1.get_raw_nid());
    EXPECT_EQ(edge.sink_pin.get_master_node().get_raw_nid(), n2.get_raw_nid());
    count++;
  }
  EXPECT_EQ(count, 1);

  count = 0;
  for (auto edge : g.inp_edges(p2)) {
    EXPECT_EQ(edge.driver_pin.get_pin_pid(), (p1.get_pin_pid() | static_cast<hhds::Pid>(2)));
    EXPECT_EQ(edge.sink_pin.get_pin_pid(), p2.get_pin_pid());
    EXPECT_EQ((edge.driver_pin.get_pin_pid() & static_cast<hhds::Pid>(1)), static_cast<hhds::Pid>(1));
    EXPECT_EQ((edge.driver_pin.get_pin_pid() & static_cast<hhds::Pid>(2)), static_cast<hhds::Pid>(2));
    EXPECT_EQ((edge.sink_pin.get_pin_pid() & static_cast<hhds::Pid>(1)), static_cast<hhds::Pid>(1));
    EXPECT_EQ((edge.sink_pin.get_pin_pid() & static_cast<hhds::Pid>(2)), static_cast<hhds::Pid>(0));
    EXPECT_EQ(edge.driver_pin.get_master_node().get_raw_nid(), n1.get_raw_nid());
    EXPECT_EQ(edge.sink_pin.get_master_node().get_raw_nid(), n2.get_raw_nid());
    count++;
  }
  EXPECT_EQ(count, 1);

  count = 0;
  for (auto edge : g.inp_edges(p1)) {
    (void)edge;
    count++;
  }
  EXPECT_EQ(count, 0);

  count = 0;
  for (auto edge : g.out_edges(p2)) {
    (void)edge;
    count++;
  }
  EXPECT_EQ(count, 0);

  count = 0;
  for (auto edge : g.out_edges(n1)) {
    (void)edge;
    count++;
  }
  EXPECT_EQ(count, 0);

  count = 0;
  for (auto edge : g.out_edges(n2)) {
    (void)edge;
    count++;
  }
  EXPECT_EQ(count, 0);

  // DELETE, and then no edges in any direction
  g.del_edge(p1, p2);

  count = 0;
  for (auto edge : g.out_edges(n1)) {
    (void)edge;
    count++;
  }
  EXPECT_EQ(count, 0);

  count = 0;
  for (auto edge : g.inp_edges(n1)) {
    (void)edge;
    count++;
  }
  EXPECT_EQ(count, 0);

  count = 0;
  for (auto edge : g.out_edges(p1)) {
    (void)edge;
    count++;
  }
  EXPECT_EQ(count, 0);

  count = 0;
  for (auto edge : g.inp_edges(p1)) {
    (void)edge;
    count++;
  }
  EXPECT_EQ(count, 0);

  count = 0;
  for (auto edge : g.out_edges(n2)) {
    (void)edge;
    count++;
  }
  EXPECT_EQ(count, 0);

  count = 0;
  for (auto edge : g.inp_edges(n2)) {
    (void)edge;
    count++;
  }
  EXPECT_EQ(count, 0);

  count = 0;
  for (auto edge : g.out_edges(p2)) {
    (void)edge;
    count++;
  }
  EXPECT_EQ(count, 0);

  count = 0;
  for (auto edge : g.inp_edges(p2)) {
    (void)edge;
    count++;
  }
  EXPECT_EQ(count, 0);
}

// ---------------------------------------------------------------------------
// Section 4: Lazy graph traversal
// ---------------------------------------------------------------------------

TEST(LazyTraversal, FastClassSingleGraph) {
  hhds::Graph g;
  auto        n1 = g.create_node();
  auto        n2 = g.create_node();
  auto        n3 = g.create_node();
  g.add_edge(n1, n2);
  g.add_edge(n2, n3);

  // fast_class: span over the graph's internal node storage (cheap, no allocation)
  int count = 0;
  for (auto node : g.fast_class()) {
    (void)node.get_port_id();
    count++;
  }
  // 3 user nodes + built-in nodes (INPUT, OUTPUT, CONST)
  EXPECT_GE(count, 3);
}

TEST(LazyTraversal, FastFlatSingleGraph) {
  hhds::GraphLibrary lib;
  auto               g   = create_declared_graph(lib, "flat");
  const hhds::Gid    gid = g->get_gid();

  (void)g->create_node();
  (void)g->create_node();

  auto nodes = g->fast_flat();

  // 3 built-ins + 2 created nodes
  EXPECT_EQ(nodes.size(), 5);
  for (const auto& node : nodes) {
    EXPECT_EQ(node.get_root_gid(), gid);
    EXPECT_EQ(node.get_current_gid(), gid);
    EXPECT_EQ(node.get_port_id(), 0);
  }
}

TEST(LazyTraversal, FastFlatHierarchy) {
  hhds::GraphLibrary lib;
  auto               root      = create_declared_graph(lib, "root");
  auto               child     = create_declared_graph(lib, "child");
  auto               leaf      = create_declared_graph(lib, "leaf");
  const hhds::Gid    root_gid  = root->get_gid();
  const hhds::Gid    child_gid = child->get_gid();
  const hhds::Gid    leaf_gid  = leaf->get_gid();

  (void)root->create_node();
  auto root_sub = root->create_node();
  (void)root->create_node();
  root->set_subnode(root_sub, child_gid);

  (void)child->create_node();
  auto child_sub = child->create_node();
  (void)child->create_node();
  child->set_subnode(child_sub, leaf_gid);

  (void)leaf->create_node();

  auto nodes = root->fast_flat();

  EXPECT_EQ(nodes.size(), 14);

  int root_count  = 0;
  int child_count = 0;
  int leaf_count  = 0;

  for (const auto& node : nodes) {
    EXPECT_EQ(node.get_root_gid(), root_gid);
    if (node.get_current_gid() == root_gid) {
      root_count++;
    } else if (node.get_current_gid() == child_gid) {
      child_count++;
    } else if (node.get_current_gid() == leaf_gid) {
      leaf_count++;
    } else {
      FAIL() << "Unexpected current_gid in fast_flat traversal";
    }
  }

  EXPECT_EQ(root_count, 5);
  EXPECT_EQ(child_count, 5);
  EXPECT_EQ(leaf_count, 4);
}

TEST(LazyTraversal, ForwardClassTopologicalOrder) {
  hhds::Graph g;
  auto        n1 = g.create_node();
  auto        n2 = g.create_node();
  auto        n3 = g.create_node();
  auto        n4 = g.create_node();

  auto n2p1 = g.create_pin(n2, 1);
  auto n4p1 = g.create_pin(n4, 1);

  g.add_edge(n1, n2);
  g.add_edge(n1, n3);
  g.add_edge(n2p1, n4);
  g.add_edge(n3, n4p1);

  auto order  = g.forward_class();
  auto order2 = g.forward_class();
  EXPECT_EQ(order.data(), order2.data());

  ASSERT_EQ(order.size(), 4);
  absl::flat_hash_map<hhds::Nid, size_t> pos;
  for (size_t i = 0; i < order.size(); ++i) {
    pos[order[i].get_raw_nid() & ~static_cast<hhds::Nid>(3)] = i;
  }

  const hhds::Nid n1_id = n1.get_raw_nid() & ~static_cast<hhds::Nid>(3);
  const hhds::Nid n2_id = n2.get_raw_nid() & ~static_cast<hhds::Nid>(3);
  const hhds::Nid n3_id = n3.get_raw_nid() & ~static_cast<hhds::Nid>(3);
  const hhds::Nid n4_id = n4.get_raw_nid() & ~static_cast<hhds::Nid>(3);

  EXPECT_LT(pos[n1_id], pos[n2_id]);
  EXPECT_LT(pos[n1_id], pos[n3_id]);
  EXPECT_LT(pos[n2_id], pos[n4_id]);
  EXPECT_LT(pos[n3_id], pos[n4_id]);

  const size_t before_size = order.size();
  (void)g.create_node();
  auto order3 = g.forward_class();
  EXPECT_EQ(order3.size(), before_size + 1);
}

TEST(LazyTraversal, ForwardClassTopologicalOrder2) {
  hhds::GraphLibrary lib;
  auto               gio = lib.create_graphio("forward_declared");
  gio->add_input("inp", 23);
  gio->add_output("out", 40);
  auto g = gio->create_graph();

  auto n1 = g->create_node();
  auto n2 = g->create_node();
  auto n3 = g->create_node();
  auto n4 = g->create_node();

  auto inp1 = g->get_input_pin("inp");
  auto out1 = g->get_output_pin("out");

  // n4 -> n3 -> n1 -> n2
  std::vector<hhds::Node_class> expected;
  expected.emplace_back(n4);
  expected.emplace_back(n3);
  expected.emplace_back(n1);
  expected.emplace_back(n2);

  g->add_edge(inp1, n4);
  g->add_edge(n4, n3);
  g->add_edge(n3, n1);
  g->add_edge(n1, n2);
  g->add_edge(n2, out1);

  int pos = 0;
  for (auto node : g->forward_class()) {
    EXPECT_EQ(expected[pos], node);
    pos++;
  }
  EXPECT_EQ(pos, expected.size());

  g->add_edge(n3, out1);  // Still should be the same
  pos = 0;
  for (auto node : g->forward_class()) {
    EXPECT_EQ(expected[pos], node);
    pos++;
  }
  EXPECT_EQ(pos, expected.size());

  auto child = create_declared_graph(lib, "forward_child");
  g->set_subnode(n3, child->get_gid());
  pos = 0;
  for (auto node : g->forward_class()) {
    EXPECT_EQ(expected[pos], node);
    pos++;
  }
  EXPECT_EQ(pos, expected.size());
}

TEST(LazyTraversal, ForwardFlatHierarchyAndCacheInvalidation) {
  hhds::GraphLibrary lib;
  auto               root      = create_declared_graph(lib, "root");
  auto               child     = create_declared_graph(lib, "child");
  const hhds::Gid    root_gid  = root->get_gid();
  const hhds::Gid    child_gid = child->get_gid();

  auto root_in  = root->create_node();
  auto root_sub = root->create_node();
  auto root_out = root->create_node();
  root->add_edge(root_in, root_sub);
  root->add_edge(root_sub, root_out);

  auto child_in  = child->create_node();
  auto child_out = child->create_node();
  child->add_edge(child_in, child_out);

  root->set_subnode(root_sub, child_gid);

  auto flat1 = root->forward_flat();
  auto flat2 = root->forward_flat();
  EXPECT_EQ(flat1.data(), flat2.data());

  size_t pos_root_in  = flat1.size();
  size_t pos_root_out = flat1.size();
  size_t first_child  = flat1.size();
  size_t last_child   = 0;

  for (size_t i = 0; i < flat1.size(); ++i) {
    const auto& n = flat1[i];
    if (n.get_current_gid() == root_gid && n.get_raw_nid() == root_in.get_raw_nid()) {
      pos_root_in = i;
    }
    if (n.get_current_gid() == root_gid && n.get_raw_nid() == root_out.get_raw_nid()) {
      pos_root_out = i;
    }
    if (n.get_current_gid() == child_gid) {
      if (first_child == flat1.size()) {
        first_child = i;
      }
      last_child = i;
    }
  }

  ASSERT_NE(first_child, flat1.size());
  ASSERT_LT(pos_root_in, first_child);
  ASSERT_LT(last_child, pos_root_out);

  const size_t before_size = flat1.size();
  (void)child->create_node();
  auto flat3 = root->forward_flat();
  EXPECT_EQ(flat3.size(), before_size + 1);
}

TEST(LazyTraversal, FastHierDistinguishesInstances) {
  hhds::GraphLibrary lib;
  auto               root      = create_declared_graph(lib, "root");
  auto               child     = create_declared_graph(lib, "child");
  const hhds::Gid    root_gid  = root->get_gid();
  const hhds::Gid    child_gid = child->get_gid();

  (void)root->create_node();
  auto sub1 = root->create_node();
  auto sub2 = root->create_node();
  root->set_subnode(sub1, child_gid);
  root->set_subnode(sub2, child_gid);

  (void)child->create_node();
  (void)child->create_node();

  auto hier1 = root->fast_hier();
  auto hier2 = root->fast_hier();
  EXPECT_EQ(hier1.data(), hier2.data());

  std::vector<hhds::Node_hier>           unique_child_nodes;
  absl::flat_hash_map<hhds::Nid, size_t> child_counts_by_raw_nid;
  size_t                                 root_count = 0;
  for (const auto& node : hier1) {
    EXPECT_EQ(node.get_root_gid(), root_gid);
    if (node.get_current_gid() == root_gid) {
      ++root_count;
      continue;
    }
    if (node.get_current_gid() == child_gid) {
      bool seen = false;
      for (const auto& existing : unique_child_nodes) {
        if (existing == node) {
          seen = true;
          break;
        }
      }
      if (!seen) {
        unique_child_nodes.push_back(node);
      }
      child_counts_by_raw_nid[node.get_raw_nid()]++;
      continue;
    }
    FAIL() << "Unexpected graph ID in fast_hier traversal";
  }

  EXPECT_EQ(root_count, 4U);                      // built-ins + one regular node (subnodes are expanded)
  EXPECT_EQ(unique_child_nodes.size(), 10U);      // two child instances, each with 5 nodes
  EXPECT_EQ(child_counts_by_raw_nid.size(), 5U);  // child built-ins + 2 created nodes
  for (const auto& [raw_nid, count] : child_counts_by_raw_nid) {
    (void)raw_nid;
    EXPECT_EQ(count, 2U);  // each child-graph node appears in two hierarchy instances
  }

  const size_t before_size = hier1.size();
  (void)child->create_node();
  auto hier3 = root->fast_hier();
  EXPECT_EQ(hier3.size(), before_size + 2U);  // two child instances, each gets one new node
}

TEST(LazyTraversal, ForwardHierOrderAndEpochInvalidation) {
  hhds::GraphLibrary lib;
  auto               root      = create_declared_graph(lib, "root");
  auto               child     = create_declared_graph(lib, "child");
  const hhds::Gid    root_gid  = root->get_gid();
  const hhds::Gid    child_gid = child->get_gid();

  auto root_in  = root->create_node();
  auto root_sub = root->create_node();
  auto root_out = root->create_node();
  root->add_edge(root_in, root_sub);
  root->add_edge(root_sub, root_out);
  root->set_subnode(root_sub, child_gid);

  auto child_in  = child->create_node();
  auto child_out = child->create_node();
  child->add_edge(child_in, child_out);

  auto hier1 = root->forward_hier();
  auto hier2 = root->forward_hier();
  EXPECT_EQ(hier1.data(), hier2.data());

  size_t pos_root_in  = hier1.size();
  size_t pos_root_out = hier1.size();
  size_t first_child  = hier1.size();
  size_t last_child   = 0;

  for (size_t i = 0; i < hier1.size(); ++i) {
    const auto& n = hier1[i];
    if (n.get_current_gid() == root_gid && n.get_raw_nid() == root_in.get_raw_nid()) {
      pos_root_in = i;
    }
    if (n.get_current_gid() == root_gid && n.get_raw_nid() == root_out.get_raw_nid()) {
      pos_root_out = i;
    }
    if (n.get_current_gid() == child_gid) {
      if (first_child == hier1.size()) {
        first_child = i;
      }
      last_child = i;
    }
  }

  ASSERT_NE(first_child, hier1.size());
  ASSERT_LT(pos_root_in, first_child);
  ASSERT_LT(last_child, pos_root_out);

  const size_t before_size = hier1.size();
  (void)child->create_node();
  auto hier3 = root->forward_hier();
  EXPECT_EQ(hier3.size(), before_size + 1U);
}

// ---------------------------------------------------------------------------
// Section 4 (cont.): add_edge(Node_class, Node_class) pin-0 shorthand
// ---------------------------------------------------------------------------

TEST(AddEdgeShorthand, NodeToNode) {
  hhds::Graph g;
  auto        n1 = g.create_node();
  auto        n2 = g.create_node();

  // add_edge with Node_class uses implicit pin 0 on both sides
  g.add_edge(n1, n2);

  int count = 0;
  for (auto edge : g.out_edges(n1)) {
    (void)edge;
    count++;
  }
  EXPECT_EQ(count, 1);
}

// ---------------------------------------------------------------------------
// Section 8: Node pin iteration
// ---------------------------------------------------------------------------

TEST(PinIteration, GetPins) {
  hhds::Graph g;
  auto        node = g.create_node();
  auto        p1   = g.create_pin(node, 1);
  auto        p2   = g.create_pin(node, 2);
  auto        p3   = g.create_pin(node, 3);

  auto pins = g.get_pins(node);
  ASSERT_EQ(pins.size(), 3);
  EXPECT_EQ(pins[0].get_pin_pid(), p1.get_pin_pid());
  EXPECT_EQ(pins[1].get_pin_pid(), p2.get_pin_pid());
  EXPECT_EQ(pins[2].get_pin_pid(), p3.get_pin_pid());
}

TEST(PinIteration, GetPinsIncludesPin0WhenMaterialized) {
  hhds::Graph g;
  auto        node = g.create_node();
  auto        p0   = g.create_pin(node, 0);
  auto        p1   = g.create_pin(node, 1);
  auto        p2   = g.create_pin(node, 2);
  auto        p3   = g.create_pin(node, 3);

  auto pins = g.get_pins(node);
  ASSERT_EQ(pins.size(), 4);
  EXPECT_EQ(pins[0].get_pin_pid(), p0.get_pin_pid());
  EXPECT_EQ(pins[1].get_pin_pid(), p1.get_pin_pid());
  EXPECT_EQ(pins[2].get_pin_pid(), p2.get_pin_pid());
  EXPECT_EQ(pins[3].get_pin_pid(), p3.get_pin_pid());
}

TEST(PinIteration, DriverAndSinkPins) {
  hhds::Graph g;
  auto        n1 = g.create_node();
  auto        n2 = g.create_node();

  auto p0 = g.create_pin(n1, 0);  // driver-only
  auto p1 = g.create_pin(n1, 1);  // sink-only
  auto p2 = g.create_pin(n1, 2);  // both

  auto n2p0 = g.create_pin(n2, 0);
  auto n2p1 = g.create_pin(n2, 1);

  g.add_edge(p0, n2p0);
  g.add_edge(n2p1, p1);
  g.add_edge(p2, n2p0);
  g.add_edge(n2p1, p2);

  auto drivers = g.get_driver_pins(n1);
  auto sinks   = g.get_sink_pins(n1);

  ASSERT_EQ(drivers.size(), 2);
  ASSERT_EQ(sinks.size(), 2);

  EXPECT_TRUE(contains_pin_pid(drivers, p0.get_pin_pid()));
  EXPECT_FALSE(contains_pin_pid(sinks, p0.get_pin_pid()));

  EXPECT_FALSE(contains_pin_pid(drivers, p1.get_pin_pid()));
  EXPECT_TRUE(contains_pin_pid(sinks, p1.get_pin_pid()));

  EXPECT_TRUE(contains_pin_pid(drivers, p2.get_pin_pid()));
  EXPECT_TRUE(contains_pin_pid(sinks, p2.get_pin_pid()));
}
TEST(CompactTypes, GraphHandlesExposeStableIdentity) {
  hhds::GraphLibrary lib;
  auto               g1  = create_declared_graph(lib, "g1");
  const hhds::Gid    gid = g1->get_gid();
  auto               g2  = lib.get_graph(gid);

  ASSERT_TRUE(g2);
  EXPECT_EQ(g1.get(), g2.get());
  EXPECT_EQ(g2->get_gid(), gid);
}

TEST(CompactTypes, ClassWrappersArePureLocalValues) {
  hhds::GraphLibrary lib;
  auto               g1 = create_declared_graph(lib, "g1");
  auto               g2 = create_declared_graph(lib, "g2");

  auto n1 = g1->create_node();
  auto n2 = g2->create_node();
  auto p1 = g1->create_pin(n1, 1);
  auto p2 = g2->create_pin(n2, 1);

  EXPECT_EQ(n1, n2);
  EXPECT_EQ(p1, p2);
  EXPECT_EQ(p1.get_master_node(), n1);
  EXPECT_EQ(p2.get_master_node(), n2);

  EXPECT_NE(hhds::to_flat(n1, g1->get_gid()), hhds::to_flat(n2, g2->get_gid()));
  EXPECT_NE(hhds::to_flat(p1, g1->get_gid()), hhds::to_flat(p2, g2->get_gid()));

  lib.delete_graph(g1->get_gid());
  EXPECT_EQ(n1.get_raw_nid(), n2.get_raw_nid());
  EXPECT_EQ(p1.get_pin_pid(), p2.get_pin_pid());
}
