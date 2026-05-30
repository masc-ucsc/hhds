// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// Coverage for the slot state machine added in TODO_hhds.md item 1g:
//   create_tree / find_tree / find_tree_rw / Tree::commit / Tree::abort
// and the symmetric API for Forest<Graph> (GraphLibrary).
//
// The semantics under test:
//   - create_tree CAS-transitions an Empty slot to Writing and returns a
//     writable shared_ptr<Tree>. Concurrent create_tree losers get nullptr.
//   - While the writable handle is alive, find_tree returns nullptr (the
//     tree is hidden from external observers).
//   - Dropping the writable handle without calling commit/abort implicitly
//     publishes (slot -> Public). find_tree then returns the body.
//   - Tree::commit() publishes immediately while the writable handle is
//     still held; subsequent find_tree returns the body.
//   - Tree::abort() then dropping the writable handle reverts the slot to
//     Empty, freeing the name for a fresh create_tree.
//   - find_tree_rw CAS-transitions Public -> Writing; concurrent readers
//     block its acquisition (returns nullptr while readers hold handles).

#include <gtest/gtest.h>

#include <memory>

#include "hhds/graph.hpp"
#include "hhds/tree.hpp"

namespace {

TEST(ForestSlotState, FindTreeReturnsNullWhileWriting) {
  auto forest = hhds::Forest::create();
  auto tio    = forest->create_io("name");

  auto tree = tio->create_tree();
  ASSERT_NE(tree, nullptr);
  // Writer holds the writable handle — find_tree must not see the body.
  EXPECT_EQ(forest->find_tree("name"), nullptr);

  // Another find_tree_rw must also fail (slot is Writing, not Public).
  EXPECT_EQ(forest->find_tree_rw("name"), nullptr);

  // Concurrent create_tree on the same TreeIO also loses.
  EXPECT_EQ(tio->create_tree(), nullptr);
}

TEST(ForestSlotState, ImplicitPublishOnDrop) {
  auto forest = hhds::Forest::create();
  auto tio    = forest->create_io("name");

  hhds::Tree_pos root_nid = hhds::INVALID;
  {
    auto tree = tio->create_tree();
    ASSERT_NE(tree, nullptr);
    root_nid = tree->add_root_node().get_debug_nid();
    EXPECT_EQ(forest->find_tree("name"), nullptr);
    // Scope exit drops `tree` -> implicit publish.
  }

  auto found = forest->find_tree("name");
  ASSERT_NE(found, nullptr);
  EXPECT_EQ(found->get_root_node().get_debug_nid(), root_nid);
}

TEST(ForestSlotState, ExplicitCommitPublishesBeforeDrop) {
  auto forest = hhds::Forest::create();
  auto tio    = forest->create_io("name");

  auto tree = tio->create_tree();
  ASSERT_NE(tree, nullptr);
  (void)tree->add_root_node();

  EXPECT_FALSE(tree->is_frozen());
  EXPECT_EQ(forest->find_tree("name"), nullptr);

  tree->commit();
  EXPECT_TRUE(tree->is_frozen());

  // find_tree sees the body even while the writable handle is still alive.
  auto found = forest->find_tree("name");
  ASSERT_NE(found, nullptr);
  EXPECT_EQ(found.get(), tree.get());
}

TEST(ForestSlotState, AbortRevertsSlotToEmpty) {
  auto forest = hhds::Forest::create();
  auto tio    = forest->create_io("name");

  {
    auto tree = tio->create_tree();
    ASSERT_NE(tree, nullptr);
    tree->abort();
    // Dropping the writable handle with abort_pending should tear the
    // slot back to Empty.
  }

  EXPECT_EQ(forest->find_tree("name"), nullptr);
  // Name is free again for a fresh create_tree on the same TreeIO.
  auto retry = tio->create_tree();
  ASSERT_NE(retry, nullptr);
  retry->commit();
  EXPECT_NE(forest->find_tree("name"), nullptr);
}

TEST(ForestSlotState, FindTreeRwBlocksOnReaders) {
  auto forest = hhds::Forest::create();
  auto tio    = forest->create_io("name");
  {
    auto tree = tio->create_tree();
    (void)tree->add_root_node();
    tree->commit();
  }

  // Acquire a read-only handle — its presence must block find_tree_rw.
  auto reader = forest->find_tree("name");
  ASSERT_NE(reader, nullptr);

  EXPECT_EQ(forest->find_tree_rw("name"), nullptr);

  reader.reset();
  // Now no outstanding readers — find_tree_rw should succeed.
  auto writer = forest->find_tree_rw("name");
  ASSERT_NE(writer, nullptr);

  // While the find_tree_rw handle is alive, find_tree returns nullptr
  // (same write-intent exclusion as create_tree).
  EXPECT_EQ(forest->find_tree("name"), nullptr);
}

TEST(ForestSlotState, FindTreeRwReleaseReturnsToPublic) {
  auto forest = hhds::Forest::create();
  auto tio    = forest->create_io("name");
  {
    auto tree = tio->create_tree();
    (void)tree->add_root_node();
    tree->commit();
  }

  {
    auto writer = forest->find_tree_rw("name");
    ASSERT_NE(writer, nullptr);
    EXPECT_EQ(forest->find_tree("name"), nullptr);
    // Drop writer — slot goes back to Public.
  }
  EXPECT_NE(forest->find_tree("name"), nullptr);
}

// ----- Graph mirror -----

TEST(GraphLibrarySlotState, FindGraphReturnsNullWhileWriting) {
  hhds::GraphLibrary lib;
  auto               gio = lib.create_io("name");

  auto graph = gio->create_graph();
  ASSERT_NE(graph, nullptr);
  EXPECT_EQ(lib.find_graph("name"), nullptr);
  EXPECT_EQ(lib.find_graph_rw("name"), nullptr);
  EXPECT_EQ(gio->create_graph(), nullptr);
}

TEST(GraphLibrarySlotState, ImplicitPublishOnDrop) {
  hhds::GraphLibrary lib;
  auto               gio = lib.create_io("name");

  {
    auto graph = gio->create_graph();
    ASSERT_NE(graph, nullptr);
    (void)graph->create_node();
    EXPECT_EQ(lib.find_graph("name"), nullptr);
  }

  EXPECT_NE(lib.find_graph("name"), nullptr);
}

TEST(GraphLibrarySlotState, ExplicitCommitPublishesBeforeDrop) {
  hhds::GraphLibrary lib;
  auto               gio = lib.create_io("name");

  auto graph = gio->create_graph();
  ASSERT_NE(graph, nullptr);
  EXPECT_FALSE(graph->is_frozen());
  EXPECT_EQ(lib.find_graph("name"), nullptr);

  graph->commit();
  EXPECT_TRUE(graph->is_frozen());
  auto found = lib.find_graph("name");
  ASSERT_NE(found, nullptr);
  EXPECT_EQ(found.get(), graph.get());
}

TEST(GraphLibrarySlotState, AbortRevertsSlotToEmpty) {
  hhds::GraphLibrary lib;
  auto               gio = lib.create_io("name");

  {
    auto graph = gio->create_graph();
    ASSERT_NE(graph, nullptr);
    graph->abort();
  }

  EXPECT_EQ(lib.find_graph("name"), nullptr);
  auto retry = gio->create_graph();
  ASSERT_NE(retry, nullptr);
  retry->commit();
  EXPECT_NE(lib.find_graph("name"), nullptr);
}

TEST(GraphLibrarySlotState, FindGraphRwBlocksOnReaders) {
  hhds::GraphLibrary lib;
  auto               gio = lib.create_io("name");
  {
    auto graph = gio->create_graph();
    (void)graph->create_node();
    graph->commit();
  }

  auto reader = lib.find_graph("name");
  ASSERT_NE(reader, nullptr);
  EXPECT_EQ(lib.find_graph_rw("name"), nullptr);

  reader.reset();
  auto writer = lib.find_graph_rw("name");
  ASSERT_NE(writer, nullptr);
  EXPECT_EQ(lib.find_graph("name"), nullptr);
}

}  // namespace
