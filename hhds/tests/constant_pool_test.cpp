// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <gtest/gtest.h>

#include <filesystem>

#include "hhds/attrs/const_payload.hpp"
#include "hhds/graph.hpp"

TEST(ConstantPool, InternsAndAppendsMonotonically) {
  hhds::GraphLibrary lib;
  auto               gio = lib.create_io("top");
  auto               g   = gio->create_graph();

  auto small = g->get_constant_node().create_driver_pin(31);
  auto a0    = g->intern_constant("wide-a", 32);
  auto b     = g->intern_constant("wide-b", 32);
  auto a1    = g->intern_constant("wide-a", 32);

  EXPECT_EQ(a0, a1);
  EXPECT_EQ(a0.get_port_id(), 32u);
  EXPECT_EQ(b.get_port_id(), 33u);
  EXPECT_EQ(small.get_port_id(), 31u);
  EXPECT_EQ(a0.attr(hhds::attrs::const_payload).get(), "wide-a");
}

TEST(ConstantPool, RebuildsFromPersistedAttributes) {
  const auto dir = std::filesystem::temp_directory_path() / "hhds_constant_pool_test";
  std::filesystem::remove_all(dir);

  {
    hhds::GraphLibrary lib;
    auto               gio = lib.create_io("top");
    auto               g   = gio->create_graph();
    EXPECT_EQ(g->intern_constant("persisted", 32).get_port_id(), 32u);
    lib.save(dir.string());
  }

  {
    hhds::GraphLibrary lib;
    lib.load(dir.string());
    auto g = lib.find_io("top")->get_graph();
    EXPECT_EQ(g->intern_constant("persisted", 32).get_port_id(), 32u);
    EXPECT_EQ(g->intern_constant("new", 32).get_port_id(), 33u);
  }

  std::filesystem::remove_all(dir);
}
