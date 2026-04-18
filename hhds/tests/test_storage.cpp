#include <gtest/gtest.h>

#include <filesystem>

#include "hhds/graph.hpp"
#include "hhds/tree.hpp"

namespace test_attrs {

struct bits_t {
  using value_type = int;
  using storage    = hhds::flat_storage;
};
inline constexpr bits_t bits{};

struct hbits_t {
  using value_type = int;
  using storage    = hhds::hier_storage;
};
inline constexpr hbits_t hbits{};

struct loc_t {
  using value_type = int;
  using storage    = hhds::flat_storage;
};
inline constexpr loc_t loc{};

}  // namespace test_attrs

// ------------------------------------------------------------------
// Compile-time size checks — these enforce the storage layout
// documented in docs/storage_internals.md.
// ------------------------------------------------------------------

static_assert(sizeof(hhds::NodeEntry) == 32, "NodeEntry must be 32 bytes");
static_assert(sizeof(hhds::PinEntry) == 32, "PinEntry must be 32 bytes");
static_assert(sizeof(hhds::Tree_pointers) == 192, "Tree_pointers must be 192 bytes (3 cache lines)");

// ------------------------------------------------------------------
// Graph storage tests
// ------------------------------------------------------------------

TEST(GraphStorage, InitialTableSizes) {
  hhds::GraphLibrary lib;
  auto               gio   = lib.create_io("top");
  auto               graph = gio->create_graph();

  // node_table starts with 4 sentinel entries (invalid, INPUT, OUTPUT, CONST)
  // pin_table starts with 1 sentinel entry
  // These are accessible through create_node / create_pin counts.

  // Creating a node should add exactly 1 NodeEntry.
  auto n1 = graph->create_node();
  auto n2 = graph->create_node();
  EXPECT_TRUE(hhds::Graph::is_valid(n1));
  EXPECT_TRUE(hhds::Graph::is_valid(n2));
}

TEST(GraphStorage, Pin0CreatesNoPinEntry) {
  hhds::GraphLibrary lib;
  auto               gio   = lib.create_io("top");
  auto               graph = gio->create_graph();

  auto n1 = graph->create_node();
  auto n2 = graph->create_node();

  // create_driver_pin(0) should return a node-as-pin without allocating a PinEntry.
  auto dpin0 = n1.create_driver_pin(0);
  EXPECT_TRUE(dpin0.is_valid());
  EXPECT_EQ(dpin0.get_port_id(), 0u);

  // The pin_pid should have bit 0 = 0 (node, not pin).
  EXPECT_EQ(dpin0.get_pin_pid() & 1u, 0u);

  // create_sink_pin(0) should also return the node-as-pin.
  auto spin0 = n1.create_sink_pin(0);
  EXPECT_TRUE(spin0.is_valid());
  EXPECT_EQ(spin0.get_port_id(), 0u);
  EXPECT_EQ(spin0.get_pin_pid() & 1u, 0u);

  // The raw_nid should be the same for both.
  EXPECT_EQ(dpin0.get_raw_nid(), spin0.get_raw_nid());

  // connect through pin(0) and verify edges work
  spin0.connect_driver(n2.create_driver_pin(0));

  auto inp = spin0.inp_edges();
  EXPECT_EQ(inp.size(), 1u);
  EXPECT_EQ(inp[0].sink_pin(), spin0);

  auto outp = n2.create_driver_pin(0).out_edges();
  EXPECT_EQ(outp.size(), 1u);
}

TEST(GraphStorage, NonZeroPortSharesSinglePinEntry) {
  hhds::GraphLibrary lib;

  auto sub_gio = lib.create_io("sub");
  sub_gio->add_input("a", 1);
  sub_gio->add_output("y", 1);

  auto top_gio = lib.create_io("top");
  auto graph   = top_gio->create_graph();

  auto node = graph->create_node();
  node.set_subnode(sub_gio);

  // First call creates the PinEntry.
  auto sink_pin = node.create_sink_pin("a");
  EXPECT_TRUE(sink_pin.is_valid());
  EXPECT_EQ(sink_pin.get_port_id(), 1u);
  EXPECT_EQ(sink_pin.get_pin_pid() & 1u, 1u);  // real PinEntry

  // Second call with the same port_id should reuse the PinEntry (same Pid base).
  auto driver_pin = node.create_driver_pin("y");
  EXPECT_TRUE(driver_pin.is_valid());
  EXPECT_EQ(driver_pin.get_port_id(), 1u);
  EXPECT_EQ(driver_pin.get_pin_pid() & 1u, 1u);

  // Both should resolve to the same underlying PinEntry (same id ignoring bit 1).
  EXPECT_EQ(driver_pin.get_pin_pid() & ~static_cast<hhds::Pid>(2), sink_pin.get_pin_pid() & ~static_cast<hhds::Pid>(2));
}

TEST(GraphStorage, EdgeBidirectionalBits) {
  hhds::GraphLibrary lib;
  auto               gio   = lib.create_io("top");
  auto               graph = gio->create_graph();

  auto n1 = graph->create_node();
  auto n2 = graph->create_node();

  // n1 drives n2 (n1 = driver, n2 = sink)
  n1.connect_sink(n2);

  // n1 out_edges: should see n2 as a sink (local edge, bit 1 = 0)
  auto out = n1.out_edges();
  EXPECT_EQ(out.size(), 1u);

  // n2 inp_edges: should see n1 as a driver (back edge, bit 1 = 1)
  auto inp = n2.inp_edges();
  EXPECT_EQ(inp.size(), 1u);

  // Cross-check: n1 should have no inp_edges, n2 no out_edges
  EXPECT_EQ(n1.inp_edges().size(), 0u);
  EXPECT_EQ(n2.out_edges().size(), 0u);
}

TEST(GraphStorage, EdgeAndNodeDeletionTombstones) {
  hhds::GraphLibrary lib;
  auto               gio   = lib.create_io("top");
  auto               graph = gio->create_graph();

  auto driver = graph->create_node();
  auto sink   = graph->create_node();
  auto d      = driver.create_driver_pin(1);
  auto s      = sink.create_sink_pin(2);

  s.connect_driver(d);
  EXPECT_EQ(s.inp_edges().size(), 1u);
  s.del_sink(d);
  EXPECT_EQ(s.inp_edges().size(), 0u);
  EXPECT_EQ(d.out_edges().size(), 0u);

  s.connect_driver(d);
  driver.del_node();
  EXPECT_TRUE(driver.is_invalid());
  EXPECT_TRUE(d.is_invalid());
  EXPECT_TRUE(s.is_valid());
  EXPECT_EQ(s.inp_edges().size(), 0u);

  std::vector<hhds::Nid> visited;
  for (auto node : graph->forward_class()) {
    visited.push_back(node.get_raw_nid());
  }
  EXPECT_EQ(visited.size(), 1u);
  EXPECT_EQ(visited[0], sink.get_raw_nid());
}

TEST(GraphStorage, ClearSemantics) {
  hhds::GraphLibrary lib;
  auto               gio = lib.create_io("top");
  gio->add_input("a", 1);
  auto graph = gio->create_graph();
  auto node  = graph->create_node();

  graph->clear();
  EXPECT_TRUE(node.is_invalid());
  EXPECT_TRUE(gio->has_graph());
  EXPECT_TRUE(graph->get_input_pin("a").is_valid());
  EXPECT_EQ(graph->forward_class().size(), 0u);
  auto node_after_clear = graph->create_node();
  EXPECT_TRUE(node.is_invalid());
  EXPECT_TRUE(node_after_clear.is_valid());
  EXPECT_NE(node_after_clear.get_raw_nid(), node.get_raw_nid());

  gio->clear();
  EXPECT_EQ(lib.find_io("top"), nullptr);
  EXPECT_FALSE(gio->has_graph());
}

TEST(GraphStorage, SubnodeForcesOverflow) {
  hhds::GraphLibrary lib;
  auto               leaf_gio = lib.create_io("leaf");
  auto               top_gio  = lib.create_io("top");
  auto               graph    = top_gio->create_graph();

  auto node = graph->create_node();
  node.set_subnode(leaf_gio);

  // After set_subnode, the node should be in overflow mode.
  auto* entry = graph->ref_node(node.get_raw_nid());
  EXPECT_TRUE(entry->check_overflow());
  EXPECT_TRUE(entry->has_subnode());
}

TEST(GraphAttrs, FlatGetSetHasDeleteOnNodeAndPin) {
  hhds::GraphLibrary lib;
  auto               gio   = lib.create_io("top");
  auto               graph = gio->create_graph();

  auto node = graph->create_node();
  auto pin  = node.create_driver_pin(1);

  node.attr(hhds::attrs::name).set("adder");
  pin.attr(hhds::attrs::name).set("sum");
  node.attr(test_attrs::bits).set(32);

  EXPECT_TRUE(graph->has_attr(hhds::attrs::name));
  EXPECT_TRUE(graph->has_attr(test_attrs::bits));
  EXPECT_TRUE(node.attr(hhds::attrs::name).has());
  EXPECT_TRUE(pin.attr(hhds::attrs::name).has());
  EXPECT_EQ(node.attr(hhds::attrs::name).get(), "adder");
  EXPECT_EQ(pin.attr(hhds::attrs::name).get(), "sum");
  EXPECT_EQ(node.attr(test_attrs::bits).get(), 32);

  pin.attr(hhds::attrs::name).del();
  EXPECT_FALSE(pin.attr(hhds::attrs::name).has());
  EXPECT_TRUE(node.attr(hhds::attrs::name).has());
}

TEST(GraphAttrs, PortZeroPinAttrsDoNotCollideWithNodeAttrs) {
  hhds::GraphLibrary lib;
  auto               gio   = lib.create_io("top");
  auto               graph = gio->create_graph();

  auto node = graph->create_node();
  auto pin0 = node.create_driver_pin(0);

  node.attr(hhds::attrs::name).set("node_name");
  pin0.attr(hhds::attrs::name).set("pin0_name");

  EXPECT_EQ(node.attr(hhds::attrs::name).get(), "node_name");
  EXPECT_EQ(pin0.attr(hhds::attrs::name).get(), "pin0_name");
  EXPECT_EQ(node.create_sink_pin(0).attr(hhds::attrs::name).get(), "pin0_name");
}

TEST(GraphAttrs, DeleteNodeRemovesNodeAndPinAttrs) {
  hhds::GraphLibrary lib;
  auto               gio   = lib.create_io("top");
  auto               graph = gio->create_graph();

  auto node = graph->create_node();
  auto pin0 = node.create_driver_pin(0);
  auto pin1 = node.create_driver_pin(1);

  node.attr(hhds::attrs::name).set("node");
  pin0.attr(hhds::attrs::name).set("pin0");
  pin1.attr(hhds::attrs::name).set("pin1");

  EXPECT_EQ(graph->attr_store(hhds::attrs::name).size(), 3u);

  node.del_node();

  EXPECT_EQ(graph->attr_store(hhds::attrs::name).size(), 0u);
}

TEST(GraphAttrs, AttrClearKeepsRegistrationAndClearDropsIt) {
  hhds::GraphLibrary lib;
  auto               gio   = lib.create_io("top");
  auto               graph = gio->create_graph();

  auto node = graph->create_node();
  node.attr(test_attrs::bits).set(64);

  graph->attr_clear(test_attrs::bits);
  EXPECT_TRUE(graph->has_attr(test_attrs::bits));
  EXPECT_FALSE(node.attr(test_attrs::bits).has());

  graph->clear();
  EXPECT_FALSE(graph->has_attr(test_attrs::bits));
}

TEST(GraphAttrs, HierAttrUsesHierarchyContext) {
  hhds::GraphLibrary lib;

  auto leaf_io = lib.create_io("leaf");
  auto leaf    = leaf_io->create_graph();
  auto leaf_n  = leaf->create_node();

  auto top_io = lib.create_io("top");
  auto top    = top_io->create_graph();
  auto inst1  = top->create_node();
  auto inst2  = top->create_node();
  inst1.set_subnode(leaf_io);
  inst2.set_subnode(leaf_io);

  std::vector<hhds::Node_class> leaf_instances;
  for (auto node : top->forward_hier()) {
    if (node.get_current_gid() == leaf->get_gid() && node.get_raw_nid() == leaf_n.get_raw_nid()) {
      leaf_instances.push_back(node);
    }
  }

  ASSERT_EQ(leaf_instances.size(), 2u);

  leaf_instances[0].attr(hhds::attrs::name).set("leaf_internal");
  leaf_instances[0].attr(test_attrs::hbits).set(11);
  leaf_instances[1].attr(test_attrs::hbits).set(22);

  EXPECT_EQ(leaf_instances[1].attr(hhds::attrs::name).get(), "leaf_internal");
  EXPECT_EQ(leaf_instances[0].attr(test_attrs::hbits).get(), 11);
  EXPECT_EQ(leaf_instances[1].attr(test_attrs::hbits).get(), 22);
}

// ------------------------------------------------------------------
// Tree storage tests
// ------------------------------------------------------------------

TEST(TreeStorage, RootAllocatesTwoChunks) {
  auto forest = hhds::Forest::create();
  auto tio    = forest->create_io("t");
  auto tree   = tio->create_tree();

  // add_root allocates 2 chunks: dummy (index 0) + root (index 1)
  auto root = tree->add_root_node();
  EXPECT_TRUE(hhds::Tree::is_valid(root));
  EXPECT_EQ(root.get_current_pos(), hhds::ROOT);
}

TEST(TreeStorage, SiblingsPackWithinChunk) {
  auto forest = hhds::Forest::create();
  auto tio    = forest->create_io("t");
  auto tree   = tio->create_tree();

  auto root = tree->add_root_node();

  // Add 8 children — first fills chunk, rest should pack or link
  hhds::Tree_pos first_child = 0;
  for (int i = 0; i < 8; ++i) {
    auto child = tree->add_child(root);
    if (i == 0) {
      first_child = child.get_current_pos();
    }
  }

  // First child should be at a chunk boundary (offset 0)
  EXPECT_EQ(first_child & hhds::CHUNK_MASK, 0);

  // All 8 siblings: first 8 fit in one chunk. The 8th slot (offset 7)
  // uses the full chunk. Verify last child is reachable.
  auto last = tree->get_last_child(root);
  EXPECT_TRUE(hhds::Tree::is_valid(last));
}

TEST(TreeStorage, ChildCreatesNewChunk) {
  auto forest = hhds::Forest::create();
  auto tio    = forest->create_io("t");
  auto tree   = tio->create_tree();

  auto root  = tree->add_root_node();
  auto child = tree->add_child(root);

  // The child lives in a new chunk (not the root's chunk).
  auto root_chunk  = root.get_current_pos() >> hhds::CHUNK_SHIFT;
  auto child_chunk = child.get_current_pos() >> hhds::CHUNK_SHIFT;
  EXPECT_NE(root_chunk, child_chunk);
}

TEST(TreeStorage, DeleteLeafTombstones) {
  auto forest = hhds::Forest::create();
  auto tio    = forest->create_io("t");
  auto tree   = tio->create_tree();

  auto root = tree->add_root_node();
  auto c1   = tree->add_child(root);
  auto c2   = tree->add_child(root);

  tree->delete_leaf(c2);

  // c1 should still be valid, c2 should be gone
  EXPECT_TRUE(hhds::Tree::is_valid(tree->as_class(c1.get_current_pos())));
  EXPECT_TRUE(c2.is_invalid());

  // After deletion, last child should be c1
  auto last = tree->get_last_child(root);
  EXPECT_EQ(last.get_current_pos(), c1.get_current_pos());

  auto c3 = tree->add_child(root);
  EXPECT_NE(c3.get_current_pos(), c2.get_current_pos());
}

TEST(TreeStorage, DeleteSubtreeAndClearTombstones) {
  auto forest = hhds::Forest::create();
  auto tio    = forest->create_io("t");
  auto tree   = tio->create_tree();

  auto root       = tree->add_root_node();
  auto child      = root.add_child();
  auto grandchild = child.add_child();
  auto sibling    = root.add_child();

  child.del_node();
  EXPECT_TRUE(child.is_invalid());
  EXPECT_TRUE(grandchild.is_invalid());
  EXPECT_TRUE(sibling.is_valid());
  EXPECT_EQ(tree->get_first_child(root).get_current_pos(), sibling.get_current_pos());

  tree->clear();
  EXPECT_TRUE(root.is_invalid());
  EXPECT_TRUE(tio->has_tree());
  EXPECT_EQ(forest->find_io("t"), tio);
  auto new_root = tree->add_root_node();
  EXPECT_TRUE(root.is_invalid());
  EXPECT_TRUE(new_root.is_valid());

  tio->clear();
  EXPECT_EQ(forest->find_io("t"), nullptr);
  EXPECT_FALSE(tio->has_tree());
}

TEST(TreeAttrs, FlatGetSetDeleteAndClear) {
  auto forest = hhds::Forest::create();
  auto tio    = forest->create_io("t");
  auto tree   = tio->create_tree();

  auto root  = tree->add_root_node();
  auto child = root.add_child();

  root.attr(hhds::attrs::name).set("program");
  child.attr(test_attrs::loc).set(10);

  EXPECT_TRUE(tree->has_attr(hhds::attrs::name));
  EXPECT_TRUE(tree->has_attr(test_attrs::loc));
  EXPECT_EQ(root.attr(hhds::attrs::name).get(), "program");
  EXPECT_EQ(child.attr(test_attrs::loc).get(), 10);

  child.del_node();
  EXPECT_EQ(tree->attr_store(test_attrs::loc).size(), 0u);

  tree->clear();
  EXPECT_FALSE(tree->has_attr(hhds::attrs::name));
  EXPECT_FALSE(tree->has_attr(test_attrs::loc));
}

// ------------------------------------------------------------------
// Binary save/load round-trip tests
// ------------------------------------------------------------------

TEST(GraphPersistence, SaveLoadRoundTrip) {
  namespace fs               = std::filesystem;
  const std::string test_dir = "/tmp/hhds_test_graph_persist";
  fs::remove_all(test_dir);

  hhds::register_attr_tag<test_attrs::bits_t>("test_attrs::bits");

  hhds::GraphLibrary lib;
  auto               top_gio = lib.create_io("top");
  top_gio->add_input("a", 1);
  top_gio->add_output("y", 1);
  auto graph = top_gio->create_graph();

  // Build a graph with nodes, edges, pins, and a subnode.
  auto sub_gio = lib.create_io("sub");
  sub_gio->add_input("x", 1);

  auto n1 = graph->create_node();
  auto n2 = graph->create_node();
  auto n3 = graph->create_node();
  n1.set_subnode(sub_gio);
  n1.attr(hhds::attrs::name).set("adder");
  n2.attr(test_attrs::bits).set(32);

  auto drv = n1.create_driver_pin(0);
  auto snk = n2.create_sink_pin(0);
  drv.connect_sink(snk);
  auto wide_pin = n3.create_driver_pin(1);
  wide_pin.attr(test_attrs::bits).set(7);

  // Also connect n2 -> n3 to have multiple edges.
  auto drv2 = n2.create_driver_pin(0);
  auto snk2 = n3.create_sink_pin(0);
  drv2.connect_sink(snk2);

  // Collect original edges.
  auto orig_out_n1 = graph->out_edges(n1);
  auto orig_out_n2 = graph->out_edges(n2);

  // Save.
  graph->save_body(test_dir);

  // Verify files exist.
  EXPECT_TRUE(fs::exists(fs::path(test_dir) / "body.bin"));

  // Create a fresh graph, load into it.
  auto top_gio2 = lib.create_io("top2");
  auto graph2   = top_gio2->create_graph();
  graph2->load_body(test_dir);

  // Verify round-trip: same node count, same edges.
  auto loaded_n1 = hhds::Node_class(graph2.get(), n1.get_raw_nid());
  auto loaded_n2 = hhds::Node_class(graph2.get(), n2.get_raw_nid());
  auto loaded_n3 = hhds::Node_class(graph2.get(), n3.get_raw_nid());

  auto loaded_out_n1 = graph2->out_edges(loaded_n1);
  auto loaded_out_n2 = graph2->out_edges(loaded_n2);
  EXPECT_EQ(loaded_out_n1.size(), orig_out_n1.size());
  EXPECT_EQ(loaded_out_n2.size(), orig_out_n2.size());

  // Verify subnode survived.
  auto* entry = graph2->ref_node(n1.get_raw_nid());
  EXPECT_TRUE(entry->has_subnode());
  EXPECT_EQ(entry->get_subnode(), sub_gio->get_gid());
  EXPECT_EQ(loaded_n1.attr(hhds::attrs::name).get(), "adder");
  EXPECT_EQ(loaded_n2.attr(test_attrs::bits).get(), 32);
  EXPECT_EQ(loaded_n3.get_driver_pin(1).attr(test_attrs::bits).get(), 7);

  fs::remove_all(test_dir);
}

TEST(GraphPersistence, OverflowSetRoundTrip) {
  namespace fs               = std::filesystem;
  const std::string test_dir = "/tmp/hhds_test_graph_overflow";
  fs::remove_all(test_dir);

  hhds::GraphLibrary lib;
  auto               gio   = lib.create_io("top");
  auto               graph = gio->create_graph();

  // Create a hub node connected to many others (force overflow).
  auto                          hub = graph->create_node();
  std::vector<hhds::Node_class> targets;
  for (int i = 0; i < 20; ++i) {
    auto t = graph->create_node();
    targets.push_back(t);
    auto d = hub.create_driver_pin(0);
    auto s = t.create_sink_pin(0);
    d.connect_sink(s);
  }

  // Verify overflow happened.
  auto* hub_entry = graph->ref_node(hub.get_raw_nid());
  EXPECT_TRUE(hub_entry->check_overflow());

  auto orig_edges = graph->out_edges(hub);

  // Save and reload.
  graph->save_body(test_dir);

  auto gio2   = lib.create_io("top_reload");
  auto graph2 = gio2->create_graph();
  graph2->load_body(test_dir);

  auto loaded_hub   = hhds::Node_class(graph2.get(), hub.get_raw_nid());
  auto loaded_edges = graph2->out_edges(loaded_hub);
  EXPECT_EQ(loaded_edges.size(), orig_edges.size());

  fs::remove_all(test_dir);
}

TEST(TreePersistence, SaveLoadRoundTrip) {
  namespace fs               = std::filesystem;
  const std::string test_dir = "/tmp/hhds_test_tree_persist";
  fs::remove_all(test_dir);

  hhds::register_attr_tag<test_attrs::loc_t>("test_attrs::loc");

  auto forest = hhds::Forest::create();
  auto tio    = forest->create_io("t");
  auto tree   = tio->create_tree();

  auto root = tree->add_root_node();
  auto c1   = tree->add_child(root);
  auto c2   = tree->add_child(root);
  auto gc1  = tree->add_child(c1);

  root.set_type(10);
  c1.set_type(20);
  c2.set_type(30);
  gc1.set_type(40);
  root.attr(hhds::attrs::name).set("program");
  gc1.attr(test_attrs::loc).set(99);

  tree->save_body(test_dir);
  EXPECT_TRUE(fs::exists(fs::path(test_dir) / "body.bin"));

  // Create a fresh tree and load.
  auto tio2  = forest->create_io("t2");
  auto tree2 = tio2->create_tree();
  tree2->load_body(test_dir);

  // Verify types survived.
  EXPECT_EQ(tree2->get_type(root.get_current_pos()), 10);
  EXPECT_EQ(tree2->get_type(c1.get_current_pos()), 20);
  EXPECT_EQ(tree2->get_type(c2.get_current_pos()), 30);
  EXPECT_EQ(tree2->get_type(gc1.get_current_pos()), 40);
  EXPECT_EQ(tree2->as_class(root.get_current_pos()).attr(hhds::attrs::name).get(), "program");
  EXPECT_EQ(tree2->as_class(gc1.get_current_pos()).attr(test_attrs::loc).get(), 99);

  // Verify structure.
  auto loaded_c1 = tree2->get_first_child(root);
  EXPECT_EQ(loaded_c1.get_current_pos(), c1.get_current_pos());

  fs::remove_all(test_dir);
}
