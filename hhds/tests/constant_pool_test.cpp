// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <gtest/gtest.h>

#include <filesystem>
#include <memory>
#include <stdexcept>
#include <thread>

#include "hhds/graph.hpp"

namespace {

Dlop integer(int64_t v) { return *Dlop::create_integer(v); }

// A value wider than one 64-bit word: lives in the pool's heap storage, so it
// exercises the deep copy and the cross-thread teardown paths.
Dlop wide() { return *Dlop::from_pyrope("0x1234567890abcdef_1234567890abcdef"); }

}  // namespace

TEST(ConstantPool, InternsByValueAndAppendsMonotonically) {
  hhds::GraphLibrary lib;
  auto               gio = lib.create_io("top");
  auto               g   = gio->create_graph();

  auto a0 = g->create_constant(integer(7));
  auto b  = g->create_constant(integer(-3));
  auto a1 = g->create_constant(integer(7));

  EXPECT_EQ(a0, a1) << "structural dedup";
  EXPECT_EQ(a0.get_port_id(), 1u);
  EXPECT_EQ(b.get_port_id(), 2u);
  EXPECT_EQ(g->constant_count(), 2u);

  ASSERT_NE(a0.const_value(), nullptr);
  EXPECT_TRUE(a0.const_value()->same_repr(integer(7)));
  EXPECT_TRUE(a0.is_const());
  EXPECT_TRUE(a0.is_known_true());
  EXPECT_FALSE(a0.is_known_false());

  auto z = g->create_constant(integer(0));
  EXPECT_TRUE(z.is_known_false());
  EXPECT_FALSE(z.is_known_true());

  // Dedup keys on the type too: the value is stored verbatim, so a Boolean
  // true and an Integer -1 are distinct constants (canonicalization is the
  // caller's policy).
  auto t = g->create_constant(*Dlop::create_bool(true));
  EXPECT_NE(t, b);
  EXPECT_TRUE(t.is_known_true());

  // A String constant is a value but never "false": is_known_false is type
  // gated (Dlop::is_known_zero accepts Integer/Boolean only). is_known_true is
  // NOT -- it is hlop truthiness, so a non-empty String is "known true". The
  // two probes are therefore not complements; treat them as independent.
  auto s = g->create_constant(*Dlop::from_pyrope("'abc'"));
  EXPECT_TRUE(s.is_const());
  EXPECT_FALSE(s.is_known_false());
  EXPECT_TRUE(s.is_known_true()) << "hlop truthiness, not a numeric probe";
}

TEST(ConstantPool, NonConstantsAnswerWithoutAPoolSlot) {
  hhds::GraphLibrary lib;
  auto               gio = lib.create_io("top");
  gio->add_input("a", 0);
  auto g = gio->create_graph();

  auto n = g->create_node();
  auto d = n.create_driver_pin(1);
  EXPECT_EQ(d.const_value(), nullptr);
  EXPECT_FALSE(d.is_const());
  EXPECT_FALSE(d.is_known_false());
  EXPECT_FALSE(d.is_known_true());

  EXPECT_FALSE(n.create_driver_pin(0).is_const()) << "node-as-pin(0)";
  EXPECT_FALSE(g->get_input_pin("a").is_const()) << "graph input";
  EXPECT_EQ(hhds::Pin_class{}.const_value(), nullptr) << "detached handle";

  EXPECT_THROW(g->create_constant(Dlop{}), std::invalid_argument) << "Invalid is not a value";
  EXPECT_THROW(g->create_constant(*Dlop::from_pyrope("nil")), std::invalid_argument) << "Nil is not a value";

  // Every route to a CONST_NODE pin that is not create_constant is refused --
  // port 0 included. A node-as-pin(0) handle on CONST_NODE is not a pool slot
  // (slots are dense 1..N), so it would report is_const() == false while
  // get_master_node() still said CONST_NODE: the two standard "is this a
  // constant?" idioms would disagree. That handle is exactly what the
  // pre-pool `create_constant()` returned, so it is the idiom callers port
  // from -- it has to fail loudly, not silently answer "not a constant".
  EXPECT_THROW((void)g->get_constant_node().create_driver_pin(5), std::logic_error) << "explicit port";
  EXPECT_THROW((void)g->get_constant_node().create_driver_pin(), std::logic_error) << "pin(0) driver";
  EXPECT_THROW((void)g->get_constant_node().create_driver_pin(0), std::logic_error) << "pin(0) driver, explicit";
  EXPECT_THROW((void)g->get_constant_node().create_sink_pin(), std::logic_error) << "pin(0) sink";
}

TEST(ConstantPool, PointersSurviveGrowth) {
  hhds::GraphLibrary lib;
  auto               gio = lib.create_io("top");
  auto               g   = gio->create_graph();

  auto        first = g->create_constant(wide());
  const Dlop* p     = first.const_value();
  ASSERT_NE(p, nullptr);
  for (int64_t i = 1; i <= 5000; ++i) {
    (void)g->create_constant(integer(i * 977));
  }
  EXPECT_EQ(first.const_value(), p) << "deque storage: no relocation on growth";
  EXPECT_TRUE(p->same_repr(wide()));
  EXPECT_EQ(g->create_constant(integer(977)).get_port_id(), 2u) << "dedup still finds the earlier pin";
}

TEST(ConstantPool, PersistsAndReloads) {
  const auto dir = std::filesystem::temp_directory_path() / "hhds_constant_pool_test";
  std::filesystem::remove_all(dir);

  {
    hhds::GraphLibrary lib;
    auto               gio = lib.create_io("top");
    auto               g   = gio->create_graph();
    EXPECT_EQ(g->create_constant(integer(5)).get_port_id(), 1u);
    EXPECT_EQ(g->create_constant(wide()).get_port_id(), 2u);
    EXPECT_EQ(g->create_constant(*Dlop::from_pyrope("0ub1?0")).get_port_id(), 3u);
    lib.save(dir.string());
  }

  {
    hhds::GraphLibrary lib;
    lib.load(dir.string());
    auto g = lib.find_io("top")->get_graph();
    EXPECT_EQ(g->constant_count(), 3u);
    EXPECT_EQ(g->create_constant(integer(5)).get_port_id(), 1u) << "reloaded values dedup against new mints";
    EXPECT_EQ(g->create_constant(wide()).get_port_id(), 2u);
    EXPECT_TRUE(g->create_constant(*Dlop::from_pyrope("0ub1?0")).const_value()->has_unknowns());
    EXPECT_EQ(g->create_constant(integer(9)).get_port_id(), 4u) << "append resumes after the reloaded tail";
  }

  std::filesystem::remove_all(dir);
}

TEST(ConstantPool, CopiedWithTheBody) {
  hhds::GraphLibrary src;
  auto               gio = src.create_io("m");
  auto               g   = gio->create_graph();
  auto               n   = g->create_node();
  g->create_constant(wide()).connect_sink(n.create_sink_pin(1));
  g->create_constant(integer(3)).connect_sink(n.create_sink_pin(2));

  hhds::GraphLibrary dst;
  ASSERT_TRUE(dst.copy_from(src, "m"));
  auto g2 = dst.find_io("m")->get_graph();
  EXPECT_EQ(g2->constant_count(), 2u);
  for (auto node : g2->body().nodes()) {
    for (const auto& e : node.inp_edges()) {
      ASSERT_TRUE(e.driver.is_const());
      EXPECT_NE(e.driver.const_value(), nullptr);
    }
  }
  EXPECT_EQ(g2->create_constant(integer(3)).get_port_id(), 2u) << "the copied pool dedups";
}

TEST(ConstantPool, TeardownOnAnotherThreadIsSafe) {
  // Dlop word buffers come from thread_local pools; a library built on one
  // thread and destroyed on another must not corrupt either pool.
  std::unique_ptr<hhds::GraphLibrary> lib;
  std::thread                         builder([&lib] {
    lib      = std::make_unique<hhds::GraphLibrary>();
    auto gio = lib->create_io("top");
    auto g   = gio->create_graph();
    for (int64_t i = 0; i < 300; ++i) {
      (void)g->create_constant(i % 3 == 0 ? wide() : integer(i));
    }
  });
  builder.join();
  ASSERT_NE(lib, nullptr);
  EXPECT_EQ(lib->find_io("top")->get_graph()->create_constant(wide()).get_port_id(), 1u);
  lib.reset();  // destroyed here, on the main thread
}
