// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// HHDS name-persistence contract.
//
// When a tree/graph is deleted by name and later recreated with the same name,
// the Forest/GraphLibrary must reuse the original id. Parent trees/graphs
// store subnode references by raw tid/gid in their binary bodies; preserving
// the (name -> id) pair across delete + recreate lets those references remain
// valid, including across save/load.

#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <vector>

#include "hhds/attr.hpp"
#include "hhds/attrs/name.hpp"
#include "hhds/graph.hpp"
#include "hhds/tree.hpp"

using hhds::attrs::name;

TEST(NamePersistence, TreeSubnodeSurvivesDeleteRecreateAcrossSaveLoad) {
  namespace fs               = std::filesystem;
  const std::string test_dir = "/tmp/hhds_name_persistence_tree";
  fs::remove_all(test_dir);

  // ── Phase 1: build two trees — "top" references "leaf" via set_subnode ───
  hhds::Tid leaf_tid_before = 0;
  {
    auto forest     = hhds::Forest::create();
    auto leaf_tio   = forest->create_io("leaf");
    auto top_tio    = forest->create_io("top");
    leaf_tid_before = leaf_tio->get_tid();

    auto leaf      = leaf_tio->create_tree();
    auto leaf_root = leaf->add_root_node();
    leaf_root.attr(name).set("leaf_v1");
    leaf_root.add_child().attr(name).set("leaf_v1_child");

    auto top      = top_tio->create_tree();
    auto top_root = top->add_root_node();
    top_root.attr(name).set("top_root");
    auto caller = top_root.add_child();
    caller.attr(name).set("calls_leaf");
    caller.set_subnode(leaf_tio);  // stores leaf_tid_before in top's body

    // Hierarchical traversal descends into leaf.
    std::vector<std::string> hier;
    for (auto n : top->pre_order_with_subtrees(top_root, true)) {
      hier.push_back(std::string(n.attr(name).get()));
    }
    EXPECT_EQ(hier, (std::vector<std::string>{"top_root", "calls_leaf", "leaf_v1", "leaf_v1_child"}));

    forest->save(test_dir);
  }

  // ── Phase 2: reload, delete "leaf", save again ───────────────────────────
  {
    auto forest = hhds::Forest::create();
    forest->load(test_dir);

    auto leaf_tio = forest->find_io("leaf");
    ASSERT_NE(leaf_tio, nullptr);
    EXPECT_EQ(leaf_tio->get_tid(), leaf_tid_before);

    // delete_treeio forces the refcount to zero — the subnode reference inside
    // top's body is what we want to keep live via name-based id reuse.
    forest->delete_treeio(leaf_tio);
    EXPECT_EQ(forest->find_io("leaf"), nullptr);

    forest->save(test_dir);
  }

  // ── Phase 3: reload, recreate "leaf" with NEW content, traversal works ──
  {
    auto forest = hhds::Forest::create();
    forest->load(test_dir);

    // "leaf" is gone from the live index…
    EXPECT_EQ(forest->find_io("leaf"), nullptr);

    // …but recreating by the same name MUST reuse the original tid, so the
    // subnode reference inside top's body still resolves.
    auto leaf_tio_v2 = forest->create_io("leaf");
    EXPECT_EQ(leaf_tio_v2->get_tid(), leaf_tid_before);

    auto leaf_v2      = leaf_tio_v2->create_tree();
    auto leaf_v2_root = leaf_v2->add_root_node();
    leaf_v2_root.attr(name).set("leaf_v2");
    leaf_v2_root.add_child().attr(name).set("leaf_v2_child");

    auto top_tio = forest->find_io("top");
    ASSERT_NE(top_tio, nullptr);
    auto top      = top_tio->get_tree();
    auto top_root = top->get_root_node();

    std::vector<std::string> hier;
    for (auto n : top->pre_order_with_subtrees(top_root, true)) {
      hier.push_back(std::string(n.attr(name).get()));
    }
    EXPECT_EQ(hier, (std::vector<std::string>{"top_root", "calls_leaf", "leaf_v2", "leaf_v2_child"}));
  }

  fs::remove_all(test_dir);
}

TEST(NamePersistence, GraphSubnodeSurvivesDeleteRecreateAcrossSaveLoad) {
  namespace fs               = std::filesystem;
  const std::string test_dir = "/tmp/hhds_name_persistence_graph";
  fs::remove_all(test_dir);

  // ── Phase 1: build two graphs — "top" instantiates "leaf" via set_subnode
  hhds::Gid leaf_gid_before = 0;
  {
    hhds::GraphLibrary lib;
    auto               leaf_gio = lib.create_io("leaf");
    auto               top_gio  = lib.create_io("top");
    leaf_gid_before             = leaf_gio->get_gid();

    auto leaf   = leaf_gio->create_graph();
    auto leaf_n = leaf->create_node();
    leaf_n.attr(name).set("leaf_internal_v1");

    auto top  = top_gio->create_graph();
    auto inst = top->create_node();
    inst.attr(name).set("leaf_instance");
    inst.set_subnode(leaf_gio);  // stores leaf_gid_before in top's body

    std::vector<std::string> hier;
    for (auto n : top->forward_hier()) {
      if (n.attr(name).has()) {
        hier.push_back(std::string(n.attr(name).get()));
      }
    }
    EXPECT_EQ(hier, (std::vector<std::string>{"leaf_instance", "leaf_internal_v1"}));

    lib.save(test_dir);
  }

  // ── Phase 2: reload, delete "leaf", save again ───────────────────────────
  {
    hhds::GraphLibrary lib;
    lib.load(test_dir);

    auto leaf_gio = lib.find_io("leaf");
    ASSERT_NE(leaf_gio, nullptr);
    EXPECT_EQ(leaf_gio->get_gid(), leaf_gid_before);

    lib.delete_graphio(leaf_gio);
    EXPECT_EQ(lib.find_io("leaf"), nullptr);

    lib.save(test_dir);
  }

  // ── Phase 3: reload, recreate "leaf" with NEW content, traversal works ──
  {
    hhds::GraphLibrary lib;
    lib.load(test_dir);

    EXPECT_EQ(lib.find_io("leaf"), nullptr);

    auto leaf_gio_v2 = lib.create_io("leaf");
    EXPECT_EQ(leaf_gio_v2->get_gid(), leaf_gid_before);

    auto leaf_v2   = leaf_gio_v2->create_graph();
    auto leaf_v2_n = leaf_v2->create_node();
    leaf_v2_n.attr(name).set("leaf_internal_v2");

    auto top_gio = lib.find_io("top");
    ASSERT_NE(top_gio, nullptr);
    auto top = top_gio->get_graph();

    std::vector<std::string> hier;
    for (auto n : top->forward_hier()) {
      if (n.attr(name).has()) {
        hier.push_back(std::string(n.attr(name).get()));
      }
    }
    EXPECT_EQ(hier, (std::vector<std::string>{"leaf_instance", "leaf_internal_v2"}));
  }

  fs::remove_all(test_dir);
}

TEST(NamePersistence, IoDeclarationDeleteRequiresDisconnectedPinAndPreservesNameUntilDelete) {
  hhds::GraphLibrary lib;

  auto and_gio = lib.create_io("and_gate_for_io_delete");
  and_gio->add_input("a", 1);
  and_gio->add_output("y", 2);

  auto top_gio = lib.create_io("top_io_delete");
  top_gio->add_input("used_io", 1);
  top_gio->add_input("unused_io", 2);
  top_gio->add_output("used_out", 1);

  auto top = top_gio->create_graph();

  auto and1 = top->create_node();
  and1.set_subnode(and_gio);

  auto used_io = top->get_input_pin("used_io");
  auto and1_in = and1.create_sink_pin("a");

  used_io.connect_sink(and1_in);
  EXPECT_EQ(used_io.get_pin_name(), "used_io");
  EXPECT_EQ(used_io.out_edges().size(), 1u);

#ifndef NDEBUG
  EXPECT_DEATH(top_gio->delete_input("used_io"), "connected");
#endif

  used_io.del_pin();
  EXPECT_TRUE(used_io.is_valid());
  EXPECT_EQ(used_io.get_pin_name(), "used_io");
  EXPECT_EQ(used_io.out_edges().size(), 0u);

  top_gio->delete_input("used_io");
  EXPECT_FALSE(top_gio->has_input("used_io"));
  EXPECT_TRUE(used_io.is_invalid());

  top_gio->delete_input("unused_io");
  EXPECT_FALSE(top_gio->has_input("unused_io"));

  auto and1_out = and1.create_driver_pin("y");
  auto used_out = top->get_output_pin("used_out");

  and1_out.connect_sink(used_out);
  EXPECT_EQ(used_out.get_pin_name(), "used_out");
  EXPECT_EQ(used_out.inp_edges().size(), 1u);

#ifndef NDEBUG
  EXPECT_DEATH(top_gio->delete_output("used_out"), "connected");
#endif

  used_out.del_pin();
  EXPECT_TRUE(used_out.is_valid());
  EXPECT_EQ(used_out.get_pin_name(), "used_out");
  EXPECT_EQ(used_out.inp_edges().size(), 0u);

  top_gio->delete_output("used_out");
  EXPECT_FALSE(top_gio->has_output("used_out"));
  EXPECT_TRUE(used_out.is_invalid());
}

// A port declared AFTER the body already carries other pins materializes at a
// late pin_table slot. On reload, the IO name->Pid maps are pre-filled by
// materializing the decls in decl-list order onto a fresh empty body, so
// without load_body's reconcile pass they would point at the WRONG slots of
// the loaded table (the late input's map entry would land on an internal pin).
// Contract: after save+load, every declared IO name resolves — get_input_pin /
// get_output_pin / get_pin_name — to the pin at its SAVED slot, identified by
// its surviving edges.
TEST(NamePersistence, IoMapsReconcileAgainstLoadedBodyAfterLateDecl) {
  namespace fs               = std::filesystem;
  const std::string test_dir = "/tmp/hhds_name_persistence_late_io";
  fs::remove_all(test_dir);

  {
    hhds::GraphLibrary lib;
    auto               gio = lib.create_io("late_io_top");
    gio->add_input("a", 1);
    gio->add_output("y", 1);
    auto g = gio->create_graph();

    // Grow the pin table past the IO pins before declaring the late port.
    auto n1     = g->create_node();
    auto n1_in  = n1.create_sink_pin(3);
    auto n1_out = n1.create_driver_pin(4);
    g->get_input_pin("a").connect_sink(n1_in);
    n1_out.connect_sink(g->get_output_pin("y"));

    gio->add_input("late_in", 2);  // materializes at a slot AFTER n1's pins
    auto n1_in2 = n1.create_sink_pin(5);
    g->get_input_pin("late_in").connect_sink(n1_in2);

    lib.save(test_dir);
  }

  {
    hhds::GraphLibrary lib;
    lib.load(test_dir);
    auto gio = lib.find_io("late_io_top");
    ASSERT_NE(gio, nullptr);
    auto g = gio->get_graph();
    ASSERT_NE(g, nullptr);

    auto late = g->get_input_pin("late_in");
    EXPECT_EQ(late.get_pin_name(), "late_in");
    EXPECT_EQ(late.out_edges().size(), 1u);  // still drives n1's port-5 sink

    auto a = g->get_input_pin("a");
    EXPECT_EQ(a.get_pin_name(), "a");
    EXPECT_EQ(a.out_edges().size(), 1u);

    EXPECT_EQ(g->get_output_pin("y").get_pin_name(), "y");
    EXPECT_EQ(g->get_output_pin("y").inp_edges().size(), 1u);
  }

  fs::remove_all(test_dir);
}
