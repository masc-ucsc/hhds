// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// Micro-benchmark for the ONE-driver lookup of a sink pin whose storage entry
// also carries a huge forward fanout.
//
// Port 0 of a node (its sink pin 0 AND its driver pin 0) lives on the
// NodeEntry, so once that entry spills into an overflow set the set holds the
// in-edge of sink 0 mixed with every out-edge of driver 0. A get_driver_pin()
// that scans the whole set costs O(fanout) for an answer of size one. The same
// mixing happens on a PinEntry whose port is used in both directions (a
// LiveHD Memory's sink pid 1 + dout driver pid 1).
//
//   bazel run -c opt //hhds:driver_lookup_bench [-- FANOUT [QUERIES]]

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "hhds/graph.hpp"

namespace {

using Clock = std::chrono::steady_clock;

double ns_per(Clock::duration d, long n) { return std::chrono::duration<double, std::nano>(d).count() / static_cast<double>(n); }

}  // namespace

int main(int argc, char** argv) {
  const long fanout  = argc > 1 ? std::atol(argv[1]) : 100000;
  const long queries = argc > 2 ? std::atol(argv[2]) : 20000;

  hhds::GraphLibrary lib;
  auto               g = lib.create_io("top")->create_graph();

  // --- port 0: NodeEntry storage -----------------------------------------
  auto drv0 = g->create_node();
  auto hub0 = g->create_node();
  drv0.create_driver_pin().connect_sink(hub0.create_sink_pin());  // the ONE in-edge of hub0 sink 0
  {
    auto out = hub0.create_driver_pin();
    for (long i = 0; i < fanout; ++i) {
      out.connect_sink(g->create_node().create_sink_pin());
    }
  }

  // --- port 1 used both ways: PinEntry storage ---------------------------
  auto drv1 = g->create_node();
  auto hub1 = g->create_node();
  drv1.create_driver_pin(3).connect_sink(hub1.create_sink_pin(1));
  {
    auto out = hub1.create_driver_pin(1);
    for (long i = 0; i < fanout; ++i) {
      out.connect_sink(g->create_node().create_sink_pin(2));
    }
  }

  // --- port 0, driver connected AFTER the fanout, and a hub with no driver --
  auto drv2 = g->create_node();
  auto hub2 = g->create_node();
  auto hub3 = g->create_node();
  {
    auto out2 = hub2.create_driver_pin();
    auto out3 = hub3.create_driver_pin();
    for (long i = 0; i < fanout; ++i) {
      out2.connect_sink(g->create_node().create_sink_pin());
      out3.connect_sink(g->create_node().create_sink_pin());
    }
  }
  drv2.create_driver_pin().connect_sink(hub2.create_sink_pin());
  const auto sink2 = hub2.get_sink_pin(0);
  const auto sink3 = hub3.get_sink_pin(0);

  const auto sink0 = hub0.get_sink_pin(0);
  const auto sink1 = hub1.get_sink_pin(1);

  long hits = 0;
  auto t0   = Clock::now();
  for (long q = 0; q < queries; ++q) {
    hits += sink0.get_driver_pin().is_valid() ? 1 : 0;
  }
  auto t1 = Clock::now();
  std::printf("port0 get_driver_pin      fanout=%ld queries=%ld  %12.1f ns/query\n", fanout, queries, ns_per(t1 - t0, queries));

  t0 = Clock::now();
  for (long q = 0; q < queries; ++q) {
    hits += sink0.has_driver() ? 1 : 0;
  }
  t1 = Clock::now();
  std::printf("port0 has_driver          fanout=%ld queries=%ld  %12.1f ns/query\n", fanout, queries, ns_per(t1 - t0, queries));

  t0 = Clock::now();
  for (long q = 0; q < queries; ++q) {
    hits += hub0.has_inp_edges() ? 1 : 0;
  }
  t1 = Clock::now();
  std::printf("port0 node.has_inp_edges  fanout=%ld queries=%ld  %12.1f ns/query\n", fanout, queries, ns_per(t1 - t0, queries));

  t0 = Clock::now();
  for (long q = 0; q < queries; ++q) {
    hits += sink1.get_driver_pin().is_valid() ? 1 : 0;
  }
  t1 = Clock::now();
  std::printf("port1 get_driver_pin      fanout=%ld queries=%ld  %12.1f ns/query\n", fanout, queries, ns_per(t1 - t0, queries));

  t0 = Clock::now();
  for (long q = 0; q < queries; ++q) {
    hits += sink2.get_driver_pin().is_valid() ? 1 : 0;
  }
  t1 = Clock::now();
  std::printf("port0 late get_driver_pin fanout=%ld queries=%ld  %12.1f ns/query\n", fanout, queries, ns_per(t1 - t0, queries));

  t0 = Clock::now();
  for (long q = 0; q < queries; ++q) {
    hits += sink2.has_driver() ? 1 : 0;
  }
  t1 = Clock::now();
  std::printf("port0 late has_driver     fanout=%ld queries=%ld  %12.1f ns/query\n", fanout, queries, ns_per(t1 - t0, queries));

  t0 = Clock::now();
  for (long q = 0; q < queries; ++q) {
    hits += sink3.has_driver() ? 0 : 1;
  }
  t1 = Clock::now();
  std::printf("port0 none has_driver     fanout=%ld queries=%ld  %12.1f ns/query\n", fanout, queries, ns_per(t1 - t0, queries));

  if (hits != 7 * queries) {
    std::fprintf(stderr, "unexpected answer: hits=%ld\n", hits);
    return 1;
  }
  if (sink0.get_driver_pin().get_master_node() != drv0 || sink1.get_driver_pin().get_master_node() != drv1
      || sink2.get_driver_pin().get_master_node() != drv2 || sink3.get_driver_pin().is_valid()) {
    std::fprintf(stderr, "wrong driver\n");
    return 1;
  }
  return 0;
}
