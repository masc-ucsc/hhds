#include <algorithm>
// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// RANDOMIZED equivalence fuzz for the pin-centric readers.
//
// sorted_pins_test.cpp pins the contract down on hand-built shapes. This file
// attacks the same contract from the other side: thousands of RANDOM node
// shapes, each cross-checked against the old edge readers, which is the
// property the 753-call-site migration actually rests on --
//
//     inp_sorted_pins()  ==  the distinct sink pins of inp_edges(), in order
//     out_sorted_pins()  ==  the distinct driver pins of out_edges(), in order
//
// WHY RANDOM AND NOT MORE HAND CASES. The direction flags are STICKY: add_edge
// sets has_driver/has_sink and del_edge never clears them, so after a deletion
// a PinEntry can claim a direction it no longer has. The iterator is supposed
// to absorb that -- the flags are only ever a NEGATIVE filter (`driver_only()`
// skips, and it requires the opposite bit CLEAR), with connectivity confirmed
// by the has_edge_dir probe. That argument is sound on paper, but its failure
// mode is a SILENTLY DROPPED OPERAND, not a crash, so it is worth hitting with
// combinations nobody thought to write down: pins that were both directions
// and lost one, pins deleted out from under a dual-direction sibling, fanouts
// that cross the inline/overflow boundary, and the node-as-pin(0) doing each
// of those.
//
// The generator deliberately produces the shapes the flags make interesting:
//   - dual-direction pins (same port_id used as driver AND sink)
//   - edge deletions that strand a stale flag
//   - pin deletions
//   - fanout spanning the 4-slot inline set into overflow
//   - port 0 (node-as-pin) connected in one, both or neither direction
//
// Failures print the seed; rerun with it as argv[1] to reproduce exactly.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <vector>

#include "hhds/graph.hpp"

namespace {

#define TEST_CHECK(condition)                                                            \
  do {                                                                                   \
    if (!(condition)) {                                                                  \
      std::fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition); \
      std::abort();                                                                      \
    }                                                                                    \
  } while (false)

using hhds::Node_class;
using hhds::Pin_class;
using hhds::Port_id;

std::vector<hhds::Pid> pids_of(const std::vector<Pin_class>& v) {
  std::vector<hhds::Pid> out;
  out.reserve(v.size());
  for (const auto& p : v) {
    out.push_back(p.get_debug_pid());
  }
  return out;
}

// Reference answers, built INDEPENDENTLY of the reader under test.
//
// The in-pin oracle sweeps every node's OUT edges and keeps the sinks landing
// on `n`: the other direction of the same storage, never n's pin list. (It used
// to fold the node-level inp_edges(), which no longer exists -- a sink pin has
// one driver, so there is no in-edge list.) Sorted ascending by port id, which
// puts the node-as-pin (port 0) first, the order inp_sorted_pins() produces.
std::vector<Pin_class> expected_inp_pins(Node_class n) {
  std::vector<Pin_class> out;
  auto* const            g = n.get_graph();
  const auto             scan = [&](const Node_class& other) {
    for (const auto& e : other.out_edges()) {
      if (e.sink.get_master_node() != n) {
        continue;
      }
      bool seen = false;
      for (const auto& p : out) {
        seen = seen || (p == e.sink);
      }
      if (!seen) {
        out.push_back(e.sink);
      }
    }
  };
  for (auto other : g->body().nodes()) {
    scan(other);
  }
  // The builtin singletons are NOT in body().nodes(), but they drive it.
  scan(g->get_input_node());
  scan(g->get_constant_node());
  std::sort(out.begin(), out.end(),
            [](const Pin_class& a, const Pin_class& b) { return a.get_port_id() < b.get_port_id(); });
  return out;
}

std::vector<Pin_class> expected_out_pins(Node_class n) {
  std::vector<Pin_class> out;
  for (const auto& e : n.out_edges()) {
    if (out.empty() || !(out.back() == e.driver)) {
      out.push_back(e.driver);
    }
  }
  return out;
}

template <typename Range>
std::vector<Pin_class> collect(const Range& r) {
  std::vector<Pin_class> out;
  for (auto p : r) {
    out.push_back(p);
  }
  return out;
}

void report_and_die(const char* what, uint64_t seed, const std::vector<Pin_class>& want,
                    const std::vector<Pin_class>& got) {
  const auto a = pids_of(want);
  const auto b = pids_of(got);
  std::fprintf(stderr, "sorted_pins_fuzz: %s MISMATCH (seed=%llu)\n  edge-reader says:", what,
               static_cast<unsigned long long>(seed));
  for (auto p : a) {
    std::fprintf(stderr, " %llu", static_cast<unsigned long long>(p));
  }
  std::fprintf(stderr, "\n  pin-reader says: ");
  for (auto p : b) {
    std::fprintf(stderr, " %llu", static_cast<unsigned long long>(p));
  }
  std::fprintf(stderr, "\n");
  std::abort();
}

// The whole contract, checked against the old readers on one node.
void cross_check(Node_class n, uint64_t seed) {
  const auto want_in  = expected_inp_pins(n);
  const auto want_out = expected_out_pins(n);

  const auto got_in  = collect(n.inp_sorted_pins());
  const auto got_out = collect(n.out_sorted_pins());

  if (pids_of(want_in) != pids_of(got_in)) {
    report_and_die("inp_sorted_pins", seed, want_in, got_in);
  }
  if (pids_of(want_out) != pids_of(got_out)) {
    report_and_die("out_sorted_pins", seed, want_out, got_out);
  }

  // The snapshot twins must agree with the lazy iterators element for element:
  // the 68 mutate-while-walking callers migrate to them, so a divergence here
  // would be a migration that silently changes behaviour.
  {
    const auto             s_in = n.inp_pins_snapshot();
    std::vector<Pin_class> v_in(s_in.begin(), s_in.end());
    if (pids_of(want_in) != pids_of(v_in)) {
      report_and_die("inp_pins_snapshot", seed, want_in, v_in);
    }
    const auto             s_out = n.out_pins_snapshot();
    std::vector<Pin_class> v_out(s_out.begin(), s_out.end());
    if (pids_of(want_out) != pids_of(v_out)) {
      report_and_die("out_pins_snapshot", seed, want_out, v_out);
    }
  }

  // size()/empty()/front() must not disagree with the walk they summarize.
  TEST_CHECK(n.inp_sorted_pins().size() == got_in.size());
  TEST_CHECK(n.out_sorted_pins().size() == got_out.size());
  TEST_CHECK(n.inp_sorted_pins().empty() == got_in.empty());
  TEST_CHECK(n.out_sorted_pins().empty() == got_out.empty());
  if (!got_in.empty()) {
    TEST_CHECK(n.inp_sorted_pins().front() == got_in.front());
  }
  if (!got_out.empty()) {
    TEST_CHECK(n.out_sorted_pins().front() == got_out.front());
  }

  // Per-element promises the consumers are told they may rely on.
  Port_id last = 0;
  for (auto p : n.inp_sorted_pins()) {
    TEST_CHECK(p.is_sink());
    TEST_CHECK(p.get_port_id() >= last);  // ascending, port 0 first
    last = p.get_port_id();
    // "A consumer handed a sink pin by inp_sorted_pins() MAY rely on the
    // driver being present" -- the promise get_driver_pin() is built on.
    TEST_CHECK(p.has_driver());
    TEST_CHECK(p.get_driver_pin().is_valid());
  }
  last = 0;
  for (auto p : n.out_sorted_pins()) {
    TEST_CHECK(p.is_driver());
    TEST_CHECK(p.get_port_id() >= last);
    last = p.get_port_id();
    TEST_CHECK(p.has_sink());
    // get_sink_pin() is well-defined ONLY for a driver with exactly one sink
    // (graph.hpp: "a driver's fanout is a SET"). Check it where it is defined,
    // and check the documented invalid answer where it is not. The plural case
    // also asserts in debug builds, so only the count-1 branch may call it
    // there.
    size_t fanout = 0;
    for (const auto& e : p.out_edges()) {
      (void)e;
      ++fanout;
    }
    TEST_CHECK(fanout >= 1);  // has_sink() said so
    if (fanout == 1) {
      TEST_CHECK(p.get_sink_pin().is_valid());
      TEST_CHECK(p.get_sink_pin() == (*p.out_edges().begin()).sink);
    }
  }
}

// One random shape, fully cross-checked.
void fuzz_one(uint64_t seed) {
  std::mt19937_64 rng(seed);
  auto            pick = [&rng](int lo, int hi) { return static_cast<int>(rng() % static_cast<uint64_t>(hi - lo + 1)) + lo; };

  hhds::GraphLibrary lib;
  auto               io = lib.create_io("fuzz" + std::to_string(seed));
  auto               g  = io->create_graph();

  auto n = g->create_node();

  // Ports 1..P, each independently a sink, a driver, both, or neither. "Both"
  // is the case that forces the flags to be two bits.
  const int              max_port = pick(1, 10);
  std::vector<Pin_class> sinks;
  std::vector<Pin_class> drivers;
  std::vector<Pin_class> far_drivers;  // the far end of each in-edge
  std::vector<Pin_class> far_sinks;    // the far end of each out-edge

  for (int port = 1; port <= max_port; ++port) {
    const int role = pick(0, 3);  // 0 none, 1 sink, 2 driver, 3 both
    if (role == 1 || role == 3) {
      auto sp  = n.create_sink_pin(static_cast<Port_id>(port));
      auto drv = g->create_node().create_driver_pin(static_cast<Port_id>(1));
      sp.connect_driver(drv);
      sinks.push_back(sp);
      far_drivers.push_back(drv);
    }
    if (role == 2 || role == 3) {
      auto dp = n.create_driver_pin(static_cast<Port_id>(port));
      // Fanout sometimes crosses the 4-slot inline set into overflow.
      const int fanout = pick(0, 2) == 0 ? pick(1, 3) : pick(4, 40);
      for (int i = 0; i < fanout; ++i) {
        auto sk = g->create_node().create_sink_pin(static_cast<Port_id>(1));
        dp.connect_sink(sk);
        far_sinks.push_back(sk);
      }
      drivers.push_back(dp);
    }
  }

  // The node-as-pin(0): its edges live on the NodeEntry and it has NO PinEntry,
  // so it has no direction flags at all and is pure probe. Exercise each of the
  // four in/out combinations.
  const int port0_role = pick(0, 3);
  if (port0_role == 1 || port0_role == 3) {
    auto drv = g->create_node().create_driver_pin(static_cast<Port_id>(1));
    n.get_sink_pin(static_cast<Port_id>(0)).connect_driver(drv);
    far_drivers.push_back(drv);
  }
  if (port0_role == 2 || port0_role == 3) {
    const int fanout = pick(1, 6);
    for (int i = 0; i < fanout; ++i) {
      auto sk = g->create_node().create_sink_pin(static_cast<Port_id>(1));
      n.get_driver_pin(static_cast<Port_id>(0)).connect_sink(sk);
      far_sinks.push_back(sk);
    }
  }

  cross_check(n, seed);

  // Now STRAND SOME FLAGS. Every deletion below leaves has_driver/has_sink set
  // on a pin that may no longer have an edge in that direction; the iterator
  // must still agree with the edge readers.
  const int deletions = pick(0, 4);
  for (int d = 0; d < deletions; ++d) {
    const int kind = pick(0, 2);
    if (kind == 0 && !far_drivers.empty()) {
      // Drop one in-edge: its sink pin may become driver-only in fact while
      // still MARKED a sink.
      const size_t idx = rng() % far_drivers.size();
      auto         drv = far_drivers[idx];
      if (drv.is_valid()) {
        auto out = drv.out_edges();
        auto it  = out.begin();
        if (it != out.end()) {
          (*it).del_edge();
        }
      }
      far_drivers.erase(far_drivers.begin() + static_cast<long>(idx));
    } else if (kind == 1 && !far_sinks.empty()) {
      const size_t idx = rng() % far_sinks.size();
      auto         snk = far_sinks[idx];
      if (snk.is_valid()) {
        auto in = snk.get_driver_pins();
        if (!in.empty()) {
          snk.del_sink(in.front());
        }
      }
      far_sinks.erase(far_sinks.begin() + static_cast<long>(idx));
    } else if (!sinks.empty()) {
      // Delete a whole pin of the node under test.
      const size_t idx = rng() % sinks.size();
      auto         sp  = sinks[idx];
      if (sp.is_valid()) {
        sp.del_pin();
      }
      sinks.erase(sinks.begin() + static_cast<long>(idx));
    }
    cross_check(n, seed);
  }
}

}  // namespace

int main(int argc, char** argv) {
  // Default: a fixed sweep, so the test is deterministic in CI. With an
  // argument: reproduce one reported seed.
  if (argc > 1) {
    const uint64_t seed = std::strtoull(argv[1], nullptr, 10);
    fuzz_one(seed);
    std::printf("sorted_pins_fuzz_test: seed %llu passed\n", static_cast<unsigned long long>(seed));
    return 0;
  }

  constexpr uint64_t kCases = 3000;
  for (uint64_t seed = 1; seed <= kCases; ++seed) {
    fuzz_one(seed);
  }
  std::printf("sorted_pins_fuzz_test: %llu random shapes cross-checked against the edge readers\n",
              static_cast<unsigned long long>(kCases));
  return 0;
}
