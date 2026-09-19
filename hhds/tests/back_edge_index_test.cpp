// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// The back-edge index of an OVERFLOWED edge entry (Graph::PinEntry::back_slot0
// in graph.hpp). Port 0 of a node -- its sink pin 0 AND its driver pin 0 --
// shares one NodeEntry, so once that entry spills into an overflow set the one
// in-edge of sink 0 sits among the whole fanout of driver 0. The index lets
// get_driver_pins / has_driver / has_inp_edges answer in O(in-degree).
//
// It is a pure accelerator, so every test drives a random mutation stream
// against a reference model and checks the OBSERVABLE answers plus
// Graph::debug_back_index_consistent(), which re-derives the index from the
// overflow sets (count, slots, and a visit order equal to a full scan's).
// Covered: node-as-pin (NodeEntry) and a pin used in both directions
// (PinEntry), 0..5 drivers (the > 2 fallback and the rescan on the way back
// down), a self loop (the same entry holds both ends of one edge), node
// deletion, pin deletion, and save/load (the index is not persisted: it is
// written as zeros and rebuilt when the overflow sets are read).

#include <gtest/gtest.h>
#include <unistd.h>

#include <algorithm>
#include <filesystem>
#include <random>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "hhds/graph.hpp"

namespace {

using Key = std::pair<hhds::Nid, hhds::Port_id>;  // (driver node nid, driver port)

Key key_of(const hhds::Pin& drv) {
  return {drv.get_master_node().get_debug_nid() & ~static_cast<hhds::Nid>(3), drv.get_port_id()};
}

std::vector<Key> driver_keys(const hhds::Pin& sink) {
  std::vector<Key> out;
  for (const auto& d : sink.get_driver_pins()) {
    out.push_back(key_of(d));
  }
  return out;
}

std::set<Key> as_set(const std::vector<Key>& v) { return {v.begin(), v.end()}; }

// One "hub": a sink whose storage entry also carries a big fanout.
struct Hub {
  hhds::Node         node;
  hhds::Pin          sink;    // the port whose drivers we ask for
  hhds::Pin          out;     // the driver side of the SAME storage entry
  std::set<Key>      drivers; // model
  std::vector<hhds::Pin> fanout_sinks;
};

class BackEdgeIndex : public ::testing::Test {
protected:
  void check(const Hub& h, const char* what, int step) {
    const auto got = driver_keys(h.sink);
    ASSERT_EQ(got.size(), as_set(got).size()) << what << " step " << step << ": duplicate driver";
    ASSERT_EQ(as_set(got), h.drivers) << what << " step " << step;
    ASSERT_EQ(h.sink.has_driver(), !h.drivers.empty()) << what << " step " << step;
    ASSERT_EQ(h.out.has_sink(), !h.fanout_sinks.empty()) << what << " step " << step;
    if (h.drivers.size() == 1) {
      ASSERT_EQ(key_of(h.sink.get_driver_pin()), *h.drivers.begin()) << what << " step " << step;
    }
  }
};

}  // namespace

TEST_F(BackEdgeIndex, RandomMutationsAgreeWithModel) {
  hhds::GraphLibrary lib;
  auto               g = lib.create_io("top")->create_graph();
  std::mt19937       rng(12345);

  // Driver candidates: node-as-pin drivers (NodeEntry Vids) and port-3 drivers
  // (PinEntry Vids), so both Vid shapes land in the index slots.
  std::vector<hhds::Pin> sources;
  for (int i = 0; i < 12; ++i) {
    auto n = g->create_node();
    sources.push_back(i % 2 == 0 ? n.create_driver_pin() : n.create_driver_pin(3));
  }

  Hub node_hub;  // port 0: NodeEntry storage
  node_hub.node = g->create_node();
  node_hub.sink = node_hub.node.create_sink_pin();
  node_hub.out  = node_hub.node.create_driver_pin();

  Hub pin_hub;  // port 1 in BOTH directions: one PinEntry
  pin_hub.node = g->create_node();
  pin_hub.sink = pin_hub.node.create_sink_pin(1);
  pin_hub.out  = pin_hub.node.create_driver_pin(1);

  auto grow_fanout = [&](Hub& h, int n) {
    for (int i = 0; i < n; ++i) {
      auto s = (i % 3 == 0) ? g->create_node().create_sink_pin(2) : g->create_node().create_sink_pin();
      h.out.connect_sink(s);
      h.fanout_sinks.push_back(s);
    }
  };
  grow_fanout(node_hub, 40);  // past the 9 inline NodeEntry slots
  grow_fanout(pin_hub, 40);   // past the 6 inline PinEntry slots

  bool self_loop  = false;  // node_hub drives its own sink 0
  int  down_3_to_2 = 0;      // the transition that may force a rescan
  int  at_5        = 0;
  for (int step = 0; step < 6000; ++step) {
    Hub&         h      = (rng() & 1) ? node_hub : pin_hub;
    const size_t before = h.drivers.size();
    const int op = static_cast<int>(rng() % 100);
    if (op < 30) {  // connect a driver (bias toward crossing 0..5)
      auto& src = sources[rng() % sources.size()];
      if (h.drivers.size() < 5 && !h.drivers.contains(key_of(src))) {
        src.connect_sink(h.sink);
        h.drivers.insert(key_of(src));
      }
    } else if (op < 58) {  // disconnect a random driver
      if (!h.drivers.empty()) {
        auto drivers = h.sink.get_driver_pins();
        auto victim  = drivers[rng() % drivers.size()];
        h.sink.del_sink(victim);
        h.drivers.erase(key_of(victim));
      }
    } else if (op < 78) {  // fanout grows
      grow_fanout(h, 1 + static_cast<int>(rng() % 4));
    } else if (op < 93) {  // fanout shrinks (forward-edge erase: index must not move)
      if (h.fanout_sinks.size() > 12) {
        const size_t i = rng() % h.fanout_sinks.size();
        h.fanout_sinks[i].del_sink(h.out);
        h.fanout_sinks.erase(h.fanout_sinks.begin() + static_cast<std::ptrdiff_t>(i));
      }
    } else if (op < 97) {  // toggle the node_hub self loop (both ends in ONE set)
      if (!self_loop && node_hub.drivers.size() < 5) {
        node_hub.out.connect_sink(node_hub.sink);
        node_hub.drivers.insert(key_of(node_hub.out));
        self_loop = true;
      } else if (self_loop) {
        node_hub.sink.del_sink(node_hub.out);
        node_hub.drivers.erase(key_of(node_hub.out));
        self_loop = false;
      }
    } else {  // delete a connected source NODE outright (delete_node path)
      const size_t i   = rng() % sources.size();
      const Key    k   = key_of(sources[i]);
      auto         src = sources[i].get_master_node();
      src.del_node();
      node_hub.drivers.erase(k);
      pin_hub.drivers.erase(k);
      auto n     = g->create_node();
      sources[i] = (rng() & 1) ? n.create_driver_pin() : n.create_driver_pin(3);
    }
    down_3_to_2 += (before == 3 && h.drivers.size() == 2) ? 1 : 0;
    at_5 += h.drivers.size() == 5 ? 1 : 0;
    check(node_hub, "node_hub", step);
    check(pin_hub, "pin_hub", step);
    ASSERT_TRUE(g->debug_back_index_consistent()) << "step " << step;
  }
  // The stream must actually have exercised the > 2 regime and its way back.
  EXPECT_GT(down_3_to_2, 20);
  EXPECT_GT(at_5, 20);
}

TEST_F(BackEdgeIndex, DriverOrderMatchesAFullScan) {
  // Two drivers on one sink (a compact-loop carry-in is legal with seed + self
  // edge): the index answers from its slots, the order must still be the
  // overflow set's scan order. debug_back_index_consistent compares exactly
  // that; exercise it across fanout erasures, which reorder the set.
  hhds::GraphLibrary lib;
  auto               g   = lib.create_io("top")->create_graph();
  auto               hub = g->create_node();
  auto               out = hub.create_driver_pin();
  std::vector<hhds::Pin> sinks;
  for (int i = 0; i < 30; ++i) {
    sinks.push_back(g->create_node().create_sink_pin());
    out.connect_sink(sinks.back());
  }
  auto a = g->create_node().create_driver_pin();
  auto b = g->create_node().create_driver_pin(2);
  a.connect_sink(hub.create_sink_pin());
  for (int i = 0; i < 10; ++i) {
    out.connect_sink(g->create_node().create_sink_pin());
  }
  b.connect_sink(hub.get_sink_pin(0));
  ASSERT_TRUE(g->debug_back_index_consistent());
  for (size_t i = 0; i < sinks.size(); i += 2) {
    sinks[i].del_sink(out);  // swap-with-last erasure moves values around
    ASSERT_TRUE(g->debug_back_index_consistent()) << "after erase " << i;
  }
  const auto got = driver_keys(hub.get_sink_pin(0));
  ASSERT_EQ(got.size(), 2u);
  EXPECT_EQ(as_set(got), (std::set<Key>{key_of(a), key_of(b)}));
}

TEST_F(BackEdgeIndex, SurvivesSaveLoad) {
  namespace fs  = std::filesystem;
  const auto dir = fs::temp_directory_path() / ("hhds_back_edge_index_test_" + std::to_string(::getpid()));
  fs::remove_all(dir);

  std::vector<std::vector<Key>> before;
  {
    hhds::GraphLibrary lib;
    auto               g = lib.create_io("top")->create_graph();
    std::vector<hhds::Node> hubs;
    for (int drivers = 0; drivers <= 4; ++drivers) {
      auto hub = g->create_node();
      auto out = hub.create_driver_pin();
      for (int i = 0; i < 25; ++i) {
        out.connect_sink(g->create_node().create_sink_pin());
      }
      for (int d = 0; d < drivers; ++d) {
        g->create_node().create_driver_pin(static_cast<hhds::Port_id>(d)).connect_sink(hub.create_sink_pin());
      }
      hubs.push_back(hub);
    }
    ASSERT_TRUE(g->debug_back_index_consistent());
    for (const auto& hub : hubs) {
      before.push_back(driver_keys(hub.get_sink_pin(0)));
    }
    lib.save(dir.string());
  }

  hhds::GraphLibrary loaded;
  loaded.load(dir.string());
  auto g = loaded.find_io("top")->get_graph();
  ASSERT_TRUE(g->debug_back_index_consistent());  // rebuilt on the deferred overflow read

  std::vector<std::vector<Key>> after;
  // Hubs are the only nodes whose sink 0 AND driver 0 overflow; find them by
  // fanout and compare the ordered driver lists.
  for (auto node : g->body().nodes(hhds::Node_order::forward)) {
    size_t fanout = 0;
    for (auto e : node.out_edges()) {
      (void)e;
      ++fanout;
    }
    if (fanout == 25) {
      after.push_back(driver_keys(node.get_sink_pin(0)));
    }
  }
  std::sort(before.begin(), before.end());
  std::sort(after.begin(), after.end());
  EXPECT_EQ(after, before);
  fs::remove_all(dir);
}
