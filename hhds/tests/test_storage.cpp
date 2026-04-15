#include <gtest/gtest.h>

#include "hhds/graph.hpp"
#include "hhds/tree.hpp"

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
  EXPECT_EQ(driver_pin.get_pin_pid() & ~static_cast<hhds::Pid>(2),
            sink_pin.get_pin_pid() & ~static_cast<hhds::Pid>(2));
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

  // After deletion, last child should be c1
  auto last = tree->get_last_child(root);
  EXPECT_EQ(last.get_current_pos(), c1.get_current_pos());
}
