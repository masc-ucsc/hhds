// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

#include "hhds/graph.hpp"

namespace {
class Test_graph : public hhds::Graph {
public:
  using hhds::Attr_host::save_attr_stores;
  using hhds::Attr_host::load_attr_stores;
};
struct Hier {
  using value_type = int32_t;
  using storage = hhds::hier_storage;
};
struct Flat {
  using value_type = std::string;
  using storage = hhds::flat_storage;
};
struct Dense {
  using value_type = uint32_t;
  using storage = hhds::flat_storage;
  using layout = hhds::dense_layout;
};
struct Flag {
  using value_type = hhds::flag;
  using storage = hhds::flat_storage;
};

// Include occurrence paths that body traversal would not visit, and attributes
// on disconnected pins. Hierarchical hash iteration order is not persistent.
std::string bytes(const Test_graph& graph) {
  std::ostringstream stream(std::ios::binary);
  graph.save_attr_stores(stream);
  return stream.str();
}

template <class Tag>
void same_entries(Test_graph& actual, Test_graph& expected) {
  const auto& a = actual.attr_store(Tag{});
  const auto& b = expected.attr_store(Tag{});
  ASSERT_EQ(a.size(), b.size());
  for (const auto& entry : b) {
    const auto it = a.find(entry.first);
    ASSERT_NE(it, a.end());
    EXPECT_EQ(it->second, entry.second);
  }
}
void same_attributes(Test_graph& actual, Test_graph& expected) {
  same_entries<Hier>(actual, expected);
  same_entries<Flat>(actual, expected);
  same_entries<Dense>(actual, expected);
  same_entries<Flag>(actual, expected);
}

struct Fixture {
  Test_graph graph;
  std::vector<hhds::Node_class> nodes;
  explicit Fixture(unsigned count) {
    for (unsigned i = 0; i < count; ++i) {
      auto node = graph.create_node();
      nodes.push_back(node);
      std::vector<hhds::Attr_key> keys;
      keys.push_back(hhds::make_node_attr_key(node.get_class_index().value));
      for (const auto port : {0, 1, 3, 11}) {
        const auto pin = node.create_driver_pin(port);
        const auto raw = pin.get_class_index().value & ~hhds::Pid{2};
        keys.push_back(hhds::make_pin_attr_key(raw));
      }
      for (const auto key : keys) {
        graph.attr_store(Flat{})[key] = std::to_string(key);
        graph.attr_store(Dense{})[key] = i + 1;
        hhds::AttrRef<Flag>(&graph, key).set();
        for (unsigned root = 1; root <= 2; ++root) {
          for (unsigned path = 0; path < 3; ++path) {
            hhds::Hier_attr_key hier{root, {{7, 16, path}}, key};
            graph.attr_store(Hier{})[hier] = static_cast<int32_t>(i + root + path);
          }
        }
      }
      if (i > 0) {
        nodes[i - 1].create_driver_pin(3).connect_sink(node.create_sink_pin(1));
      }
    }
  }
};

TEST(GraphBulkErase, MatchesIndividualDeletionAndPersistence) {
  hhds::register_attr_tag<Hier>("hhds.bulk.hier");
  hhds::register_attr_tag<Flat>("hhds.bulk.flat");
  hhds::register_attr_tag<Dense>("hhds.bulk.dense");
  hhds::register_attr_tag<Flag>("hhds.bulk.flag");
  Fixture bulk(40), reference(40);
  same_attributes(bulk.graph, reference.graph);
  std::vector<hhds::Node_class> dead;
  for (unsigned i = 0; i < bulk.nodes.size(); ++i) {
    if (i % 3 != 0) {
      dead.push_back(bulk.nodes[i]);
      reference.nodes[i].del_node();
    }
  }
  // Arbitrary order and repeated handles must remove each object exactly once.
  std::reverse(dead.begin(), dead.end());
  dead.push_back(dead.front());
  bulk.graph.delete_nodes(dead);
  same_attributes(bulk.graph, reference.graph);
  size_t survivors = 0;
  for (const auto& node : bulk.graph.body().nodes()) {
    ++survivors;
    EXPECT_FALSE(node.has_inp_edges());
    EXPECT_FALSE(node.has_out_edges());
  }
  EXPECT_EQ(survivors, 14);
  Test_graph loaded;
  std::istringstream stream(bytes(bulk.graph), std::ios::binary);
  loaded.load_attr_stores(stream);
  same_attributes(loaded, reference.graph);
  EXPECT_EQ(loaded.attr_store(Hier{}).size(), 14 * 5 * 6);
  const auto before = bytes(bulk.graph);
  bulk.graph.delete_nodes({});
  EXPECT_EQ(bytes(bulk.graph), before);
}

TEST(GraphBulkErase, PreservesLiveEdgesAcrossOverflowAndInternalDeadEdges) {
  Test_graph graph;
  auto source = graph.create_node();
  auto sink = graph.create_node();
  auto live = graph.create_node();
  source.create_driver_pin(3).connect_sink(live.create_sink_pin(1));
  live.create_driver_pin(7).connect_sink(sink.create_sink_pin(4));
  std::vector<hhds::Node_class> dead;
  for (unsigned i = 0; i < 24; ++i) {
    auto node = graph.create_node();
    source.create_driver_pin(3).connect_sink(node.create_sink_pin(1));
    node.create_driver_pin(7).connect_sink(sink.create_sink_pin(4));
    if (!dead.empty()) {
      dead.back().create_driver_pin(9).connect_sink(node.create_sink_pin(5));
    }
    dead.push_back(node);
  }
  graph.delete_nodes(dead);
  std::vector<hhds::Pin_class> readers;
  for (const auto& edge : source.create_driver_pin(3).out_edges()) {
    readers.push_back(edge.sink);
  }
  ASSERT_EQ(readers.size(), 1);
  EXPECT_EQ(readers.front(), live.create_sink_pin(1));
  const auto drivers = sink.create_sink_pin(4).get_driver_pins();
  ASSERT_EQ(drivers.size(), 1);
  EXPECT_EQ(drivers.front(), live.create_driver_pin(7));
}

TEST(GraphBulkErase, InvalidBatchIsRejectedBeforeMutation) {
  Test_graph graph, foreign;
  auto node = graph.create_node();
  node.set_name("keep until batch is valid");
  auto other = foreign.create_node();
  const auto before = bytes(graph);
  const std::array<hhds::Node_class, 3> invalid{other, hhds::Node_class{}, graph.get_input_node()};
  for (const auto& bad : invalid) {
    const std::array<hhds::Node_class, 2> batch{node, bad};
    EXPECT_THROW(graph.delete_nodes(batch), std::invalid_argument);
    EXPECT_EQ(bytes(graph), before);
    EXPECT_EQ(node.get_name(), "keep until batch is valid");
  }
  auto tombstone = graph.create_node();
  tombstone.del_node();
  const std::array<hhds::Node_class, 2> stale{node, tombstone};
  EXPECT_THROW(graph.delete_nodes(stale), std::invalid_argument);
  EXPECT_EQ(bytes(graph), before);
  const std::array<hhds::Node_class, 1> valid{node};
  graph.delete_nodes(valid);
  EXPECT_TRUE(graph.attr_store(hhds::attrs::name).empty());
}
}  // namespace
