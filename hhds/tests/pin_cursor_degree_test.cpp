// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// Covers two independent accelerators added for the graph-plumbing work:
//
//   1. The find_or_create_pin APPEND CURSOR. A node's pin list is a sorted
//      singly-linked list, so building it in ascending port order used to cost
//      Theta(P^2) hops (2.0e9 of them on one mid-size module). The cursor
//      resumes the scan at the last pin this graph created instead of at the
//      head. It is a pure accelerator, so every test here asserts the
//      OBSERVABLE chain -- order, completeness, identity of a re-requested pin,
//      in-edge order -- and deliberately drives the stale-cursor paths (pin
//      delete, clear_graph, node interleaving, descending and shuffled
//      insertion) where it must silently fall back to a head scan.
//
//   2. has_out_edges() / has_inp_edges(), which now answer from the packed
//      direction bit instead of constructing an EdgeRange per pin. Every shape
//      below is cross-checked against the materializing out_edges()/get_driver_pins()
//      the predicate must agree with, including the >6-edge OVERFLOW regime and
//      the far-target (ledge) regime.

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <numeric>
#include <random>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "hhds/graph.hpp"

namespace {

using PortVec = std::vector<hhds::Port_id>;

// The node's pin chain in LINK order. inp_sorted_pins() walks the very chain
// the cursor maintains (the node-as-pin first, then next_pin_id order), which
// is why its documented "ascending sink port" contract is the right observable
// here. Every pin the tests below create is given an in-edge so it shows up.
PortVec chain_ports(const hhds::Node& n) {
  PortVec out;
  for (auto sink : n.inp_sorted_pins()) {
    out.push_back(sink.get_port_id());
  }
  return out;
}

void expect_chain_is(const hhds::Node& n, const PortVec& expected) {
  const auto got = chain_ports(n);
  EXPECT_EQ(got, expected);
  EXPECT_TRUE(std::is_sorted(got.begin(), got.end())) << "pin chain must stay sorted by port id";
  EXPECT_EQ(std::set<hhds::Port_id>(got.begin(), got.end()).size(), got.size()) << "no duplicate port in the chain";
}

// Create sink pin `p` on `n` and give it an in-edge, so the chain is visible
// through inp_sorted_pins(). Returns the pin.
hhds::Pin wire(const hhds::Pin& drv, const hhds::Node& n, hhds::Port_id p) {
  auto pin = n.create_sink_pin(p);
  drv.connect_sink(pin);
  return pin;
}

PortVec iota_ports(hhds::Port_id lo, hhds::Port_id hi) {
  PortVec v;
  for (hhds::Port_id p = lo; p <= hi; ++p) {
    v.push_back(p);
  }
  return v;
}

}  // namespace

// --------------------------------------------------------------------------
// find_or_create_pin: the cursor must never change the answer
// --------------------------------------------------------------------------

TEST(PinCursor, AscendingCreateBuildsSortedChain) {
  hhds::GraphLibrary lib;
  auto               g = lib.create_io("top")->create_graph();
  auto               n   = g->create_node();
  auto               drv = g->create_node().create_driver_pin();

  for (hhds::Port_id p = 1; p <= 400; ++p) {
    EXPECT_EQ(wire(drv, n, p).get_port_id(), p);
  }
  expect_chain_is(n, iota_ports(1, 400));

  for (hhds::Port_id p = 1; p <= 400; ++p) {
    EXPECT_EQ(n.get_sink_pin(p).get_port_id(), p);
  }
}

TEST(PinCursor, DescendingCreateBuildsSortedChain) {
  hhds::GraphLibrary lib;
  auto               g = lib.create_io("top")->create_graph();
  auto               n   = g->create_node();
  auto               drv = g->create_node().create_driver_pin();

  for (hhds::Port_id p = 200; p >= 1; --p) {
    EXPECT_EQ(wire(drv, n, p).get_port_id(), p);
  }
  expect_chain_is(n, iota_ports(1, 200));
}

TEST(PinCursor, ShuffledCreateBuildsSortedChain) {
  hhds::GraphLibrary lib;
  auto               g = lib.create_io("top")->create_graph();
  auto               n   = g->create_node();
  auto               drv = g->create_node().create_driver_pin();

  auto ports = iota_ports(1, 300);
  std::shuffle(ports.begin(), ports.end(), std::mt19937(12345));
  for (auto p : ports) {
    EXPECT_EQ(wire(drv, n, p).get_port_id(), p);
  }
  expect_chain_is(n, iota_ports(1, 300));
}

TEST(PinCursor, InterleavedNodesKeepTheirOwnChains) {
  // The cursor names ONE node, so round-robin creation makes it wrong on almost
  // every call. The fallback must still produce three correct chains.
  hhds::GraphLibrary lib;
  auto               g = lib.create_io("top")->create_graph();
  auto               a = g->create_node();
  auto               b = g->create_node();
  auto               c   = g->create_node();
  auto               drv = g->create_node().create_driver_pin();

  PortVec ea, eb, ec;
  for (hhds::Port_id p = 1; p <= 150; ++p) {
    (void)wire(drv, a, p);
    (void)wire(drv, b, static_cast<hhds::Port_id>(p * 2));
    (void)wire(drv, c, static_cast<hhds::Port_id>(301 - p));  // descending
    ea.push_back(p);
    eb.push_back(static_cast<hhds::Port_id>(p * 2));
    ec.push_back(static_cast<hhds::Port_id>(150 + p));
  }
  expect_chain_is(a, ea);
  expect_chain_is(b, eb);
  expect_chain_is(c, ec);
}

TEST(PinCursor, RecreateReturnsTheExistingPin) {
  hhds::GraphLibrary    lib;
  auto                  g = lib.create_io("top")->create_graph();
  auto                  n   = g->create_node();
  auto                  drv = g->create_node().create_driver_pin();
  std::vector<uint64_t> pids;

  for (hhds::Port_id p = 1; p <= 64; ++p) {
    pids.push_back(wire(drv, n, p).get_debug_pid());
  }
  // Re-request below, at, and above the cursor, in both directions.
  for (hhds::Port_id p = 1; p <= 64; ++p) {
    EXPECT_EQ(n.create_sink_pin(p).get_debug_pid(), pids[p - 1]) << "port " << p;
  }
  for (hhds::Port_id p = 64; p >= 1; --p) {
    EXPECT_EQ(n.create_sink_pin(p).get_debug_pid(), pids[p - 1]) << "port " << p;
  }
  expect_chain_is(n, iota_ports(1, 64));
}

TEST(PinCursor, StaleAfterTombstonedPin) {
  // GraphIO::delete_input is the public route to Graph::delete_pin: it zeroes
  // the PinEntry in place. Delete the exact pin the cursor points at, then keep
  // declaring inputs. A cursor that trusted its own pid would chain the next
  // pin off a tombstone and orphan it from INPUT_NODE.
  hhds::GraphLibrary lib;
  auto               gio = lib.create_io("top");
  auto               g   = gio->create_graph();

  for (hhds::Port_id p = 1; p <= 40; ++p) {
    gio->add_input("i" + std::to_string(p), p);
  }
  gio->delete_input("i40");  // the cursor target: tombstones that PinEntry
  for (hhds::Port_id p = 41; p <= 60; ++p) {
    gio->add_input("i" + std::to_string(p), p);
  }
  for (hhds::Port_id p = 1; p <= 60; ++p) {
    if (p == 40) {
      continue;
    }
    const auto pin = g->get_input_pin("i" + std::to_string(p));
    EXPECT_TRUE(pin.is_valid()) << "input i" << p << " lost after the tombstone";
    EXPECT_EQ(pin.get_port_id(), p) << "input i" << p;
  }

  // A MIDDLE tombstone, then a re-declare at the same port.
  gio->delete_input("i20");
  gio->add_input("i20b", 20);
  for (hhds::Port_id p = 1; p <= 60; ++p) {
    if (p == 40 || p == 20) {
      continue;
    }
    EXPECT_EQ(g->get_input_pin("i" + std::to_string(p)).get_port_id(), p) << "input i" << p;
  }
  EXPECT_EQ(g->get_input_pin("i20b").get_port_id(), 20u);
}

TEST(PinCursor, DisconnectingEveryEdgeLeavesTheChain) {
  // Pin_class::del_pin() drops the pin's EDGES; the PinEntry (and its chain
  // slot) survives. The cursor must keep working across that too.
  hhds::GraphLibrary lib;
  auto               g   = lib.create_io("top")->create_graph();
  auto               n   = g->create_node();
  auto               drv = g->create_node().create_driver_pin();

  for (hhds::Port_id p = 1; p <= 40; ++p) {
    (void)wire(drv, n, p);
  }
  n.get_sink_pin(40).del_pin();
  for (hhds::Port_id p = 41; p <= 60; ++p) {
    (void)wire(drv, n, p);
  }
  PortVec expected;
  for (hhds::Port_id p = 1; p <= 60; ++p) {
    if (p != 40) {  // disconnected, so it carries no in-edge any more
      expected.push_back(p);
    }
  }
  expect_chain_is(n, expected);
  // ...but the pin itself is still there, at its sorted position.
  EXPECT_EQ(n.get_sink_pin(40).get_port_id(), 40u);
  (void)wire(drv, n, 40);
  expect_chain_is(n, iota_ports(1, 60));
}

TEST(PinCursor, StaleAfterClear) {
  hhds::GraphLibrary lib;
  auto               g = lib.create_io("top")->create_graph();
  {
    auto n   = g->create_node();
    auto drv = g->create_node().create_driver_pin();
    for (hhds::Port_id p = 1; p <= 50; ++p) {
      (void)wire(drv, n, p);
    }
  }
  g->clear();  // every PinEntry is tombstoned: the cursor must not trust its pid
  auto n2   = g->create_node();
  auto drv2 = g->create_node().create_driver_pin();
  for (hhds::Port_id p = 1; p <= 50; ++p) {
    (void)wire(drv2, n2, p);
  }
  expect_chain_is(n2, iota_ports(1, 50));
}

TEST(PinCursor, InpEdgesStayAscendingBySinkPort) {
  // inp_sorted_pins() promises ascending sink-port order, and that promise is a
  // direct consequence of the chain the cursor builds.
  hhds::GraphLibrary lib;
  auto               g   = lib.create_io("top")->create_graph();
  auto               dst = g->create_node();
  auto               drv = g->create_node().create_driver_pin();

  constexpr hhds::Port_id kPorts = 120;
  for (hhds::Port_id p = kPorts; p >= 1; --p) {  // created DESCENDING on purpose
    (void)wire(drv, dst, p);
  }
  PortVec seen;
  for (auto sink : dst.inp_sorted_pins()) {
    seen.push_back(sink.get_port_id());
  }
  EXPECT_EQ(seen.size(), static_cast<size_t>(kPorts));
  EXPECT_TRUE(std::is_sorted(seen.begin(), seen.end()));
  EXPECT_EQ(seen.front(), 1u);
  EXPECT_EQ(seen.back(), kPorts);
}

// --------------------------------------------------------------------------
// has_out_edges / has_inp_edges: must agree with the materializing ranges
// --------------------------------------------------------------------------

namespace {

void expect_predicates_agree(const hhds::Node& n, const char* what) {
  size_t outs = 0;
  for (const auto& e : n.out_edges()) {
    (void)e;
    ++outs;
  }
  size_t ins = 0;
  for (auto sink : n.inp_sorted_pins()) {
    ins += sink.get_driver_pins().size();
  }
  EXPECT_EQ(n.has_out_edges(), outs != 0) << what << ": out_edges()=" << outs;
  EXPECT_EQ(n.has_inp_edges(), ins != 0) << what << ": in-drivers=" << ins;
}

}  // namespace

TEST(EdgeDegree, PredicatesAgreeWithRangesAcrossStorageRegimes) {
  hhds::GraphLibrary lib;
  auto               g = lib.create_io("top")->create_graph();

  // (a) isolated node
  auto isolated = g->create_node();
  expect_predicates_agree(isolated, "isolated");
  EXPECT_FALSE(isolated.has_out_edges());
  EXPECT_FALSE(isolated.has_inp_edges());

  // (b) 300 sink pins, no driver. The old code built one EdgeRange per pin
  //     just to discover there is no out-edge anywhere.
  auto sink_only = g->create_node();
  for (hhds::Port_id p = 1; p <= 300; ++p) {
    g->create_node().create_driver_pin().connect_sink(sink_only.create_sink_pin(p));
  }
  expect_predicates_agree(sink_only, "sink_only");
  EXPECT_TRUE(sink_only.has_inp_edges());
  EXPECT_FALSE(sink_only.has_out_edges());

  // (c) driver only
  auto driver_only = g->create_node();
  driver_only.create_driver_pin().connect_sink(g->create_node().create_sink_pin(1));
  expect_predicates_agree(driver_only, "driver_only");
  EXPECT_TRUE(driver_only.has_out_edges());
  EXPECT_FALSE(driver_only.has_inp_edges());

  // (d) both directions
  auto both = g->create_node();
  g->create_node().create_driver_pin().connect_sink(both.create_sink_pin(1));
  both.create_driver_pin().connect_sink(g->create_node().create_sink_pin(1));
  expect_predicates_agree(both, "both");
  EXPECT_TRUE(both.has_out_edges());
  EXPECT_TRUE(both.has_inp_edges());

  // (e) OVERFLOW regime: 64 sinks on one driver pin spill past the 6 inline
  //     slots into the overflow set, which has_edge_dir must scan too.
  auto fanout = g->create_node();
  auto d      = fanout.create_driver_pin();
  for (int i = 0; i < 64; ++i) {
    d.connect_sink(g->create_node().create_sink_pin(1));
  }
  expect_predicates_agree(fanout, "overflow fanout");
  EXPECT_TRUE(fanout.has_out_edges());
  EXPECT_FALSE(fanout.has_inp_edges());

  // (f) far target: a large delta cannot be packed into a 13-bit sedge
  //     magnitude, so the edge lands in ledge0/ledge1 instead.
  auto near = g->create_node();
  for (int i = 0; i < 40000; ++i) {
    (void)g->create_node();
  }
  auto far = g->create_node();
  near.create_driver_pin().connect_sink(far.create_sink_pin(7));
  expect_predicates_agree(near, "far driver");
  expect_predicates_agree(far, "far sink");
  EXPECT_TRUE(near.has_out_edges());
  EXPECT_FALSE(near.has_inp_edges());
  EXPECT_TRUE(far.has_inp_edges());
  EXPECT_FALSE(far.has_out_edges());
}

TEST(EdgeDegree, PredicatesFollowEdgeDeletion) {
  hhds::GraphLibrary lib;
  auto               g = lib.create_io("top")->create_graph();

  auto a  = g->create_node();
  auto b  = g->create_node();
  auto da = a.create_driver_pin();
  da.connect_sink(b.create_sink_pin(1));
  EXPECT_TRUE(a.has_out_edges());
  EXPECT_TRUE(b.has_inp_edges());

  for (auto sink : b.inp_pins_snapshot()) {
    sink.del_sink();
  }
  expect_predicates_agree(a, "after del_edge (driver)");
  expect_predicates_agree(b, "after del_edge (sink)");
  EXPECT_FALSE(a.has_out_edges());
  EXPECT_FALSE(b.has_inp_edges());
}

// The port-0 (node-as-pin) storage is a NodeEntry, which has 3 extra packed
// slots on top of the 4 a PinEntry has -- a separate decoder, so a separate
// case.
TEST(EdgeDegree, NodeAsPinSlotsAreScannedIncludingTheExtras) {
  hhds::GraphLibrary lib;
  auto               g = lib.create_io("top")->create_graph();

  for (int fanout = 1; fanout <= 9; ++fanout) {
    auto src = g->create_node();
    auto d   = src.create_driver_pin();  // port 0: NodeEntry storage
    for (int i = 0; i < fanout; ++i) {
      d.connect_sink(g->create_node().create_sink_pin(1));
    }
    expect_predicates_agree(src, "node-as-pin fanout");
    EXPECT_TRUE(src.has_out_edges()) << "fanout " << fanout;
    EXPECT_FALSE(src.has_inp_edges()) << "fanout " << fanout;
  }
}

// --------------------------------------------------------------------------
// The point of the cursor: pin creation must not be quadratic
// --------------------------------------------------------------------------

namespace {

// Wall time to build one node with `pins` sink pins, in ascending port order.
double build_ms(hhds::Port_id pins) {
  hhds::GraphLibrary lib;
  auto               g = lib.create_io("top")->create_graph();
  auto               n = g->create_node();

  const auto t0 = std::chrono::steady_clock::now();
  for (hhds::Port_id p = 1; p <= pins; ++p) {
    (void)n.create_sink_pin(p);
  }
  const auto t1 = std::chrono::steady_clock::now();
  return std::chrono::duration<double, std::milli>(t1 - t0).count();
}

}  // namespace

TEST(PinCursor, AscendingCreationIsNotQuadratic) {
  // A sorted singly-linked pin list scanned from the head costs Theta(P^2):
  // quadrupling P quadruples the time per pin, so the 4x point takes ~16x.
  // With the append cursor it is linear, so ~4x. The gate is deliberately slack
  // (8x) -- it only has to separate "linear-ish" from "quadratic", and it must
  // not go red because the box is busy.
  (void)build_ms(1000);  // warm the allocator; result discarded

  double small = 1e18;
  double large = 1e18;
  for (int rep = 0; rep < 3; ++rep) {  // best-of-3: a loaded box only adds time
    small = std::min(small, build_ms(4000));
    large = std::min(large, build_ms(16000));
  }
  const double ratio = large / std::max(small, 1e-3);
  EXPECT_LT(ratio, 8.0) << "4x the pins took " << ratio << "x the time (" << small << " ms -> " << large
                        << " ms): pin creation looks quadratic again";
}
