#include <gtest/gtest.h>

#include <algorithm>
#include <vector>

#include "hhds/graph.hpp"
#include "hhds/tree.hpp"

TEST(GraphWrapperApi, DeletePinDisconnectsBothDirectionsFromEitherHandle) {
  for (hhds::Port_id port : {0, 7}) {
    for (bool use_driver : {false, true}) {
      hhds::GraphLibrary lib;
      auto               graph    = lib.create_io("top")->create_graph();
      auto               node     = graph->create_node();
      auto               sink     = node.create_sink_pin(port);
      auto               driver   = node.create_driver_pin(port);
      auto               upstream = graph->create_node().create_driver_pin();
      sink.connect_driver(upstream);
      std::vector<hhds::Pin_class> downstream;
      // Exercise overflow as well as the shared port-0 and real-pin entries.
      for (int i = 0; i < 20; ++i) {
        auto next = graph->create_node().create_sink_pin();
        driver.connect_sink(next);
        downstream.push_back(next);
      }

      (use_driver ? driver : sink).del_pin();

      EXPECT_TRUE(sink.is_valid());
      EXPECT_TRUE(driver.is_valid());
      EXPECT_TRUE(node.inp_sorted_pins().empty());
      EXPECT_TRUE(node.out_sorted_pins().empty());
      EXPECT_TRUE(upstream.out_edges().empty());
      for (const auto& next : downstream) {
        EXPECT_TRUE(next.get_driver_pins().empty());
      }
    }
  }
}

TEST(GraphTraversalApi, PinAndEdgeIteratorsCompareTheirPositions) {
  EXPECT_TRUE(hhds::OutEdgeRange{}.empty());
  EXPECT_TRUE(hhds::SortedPinRange{}.empty());

  for (int fanout : {2, 20}) {
    hhds::GraphLibrary lib;
    auto               graph  = lib.create_io("top")->create_graph();
    auto               source = graph->create_node();
    auto               sink   = graph->create_node();
    for (int i = 0; i < fanout; ++i) {
      source.create_driver_pin().connect_sink(sink.create_sink_pin(static_cast<hhds::Port_id>(i)));
    }
    source.create_driver_pin(7).connect_sink(sink.create_sink_pin(30));

    auto pins   = sink.inp_sorted_pins();
    auto p      = pins.begin();
    auto p_copy = p;
    EXPECT_EQ(p, p_copy);
    EXPECT_EQ(p, pins.begin());
    auto old_p = p++;
    EXPECT_EQ(old_p, p_copy);
    EXPECT_NE(p, p_copy);
    ++p_copy;
    EXPECT_EQ(p, p_copy);
    while (p != pins.end()) {
      ++p;
    }
    EXPECT_EQ(p, hhds::SortedPinIterator{});

    auto edges  = source.out_edges();
    auto e      = edges.begin();
    auto e_copy = e;
    EXPECT_EQ(e, e_copy);
    EXPECT_EQ(e, edges.begin());
    auto old_e = e++;
    EXPECT_EQ(old_e, e_copy);
    EXPECT_NE(e, e_copy);
    ++e_copy;
    EXPECT_EQ(e, e_copy);
    while (e != edges.end()) {
      ++e;
    }
    EXPECT_EQ(e, hhds::OutEdgeIterator{});
  }
}

TEST(GraphTraversalApi, BackwardReachabilityFollowsEveryInputOfDriverNodes) {
  hhds::GraphLibrary lib;
  auto               graph     = lib.create_io("top")->create_graph();
  auto               a         = graph->create_node();
  auto               b         = graph->create_node();
  auto               c         = graph->create_node();
  auto               extra     = graph->create_node();
  auto               a_out     = a.create_driver_pin();
  auto               b_out     = b.create_driver_pin(9);
  auto               c_out     = c.create_driver_pin();
  auto               extra_out = extra.create_driver_pin();
  a_out.connect_sink(b.create_sink_pin());
  extra_out.connect_sink(b.create_sink_pin());  // plural carry-shaped input
  b_out.connect_sink(c.create_sink_pin(7));

  auto view = graph->occurrences();
  for (auto search : {hhds::Search_order::dfs, hhds::Search_order::bfs}) {
    hhds::Reach_options options;
    options.direction    = hhds::Direction::backward;
    options.search_order = search;
    std::vector<hhds::Pid> reached;
    for (const auto& pin : view.reachable_pins({view.lift(c_out)}, options)) {
      reached.push_back(pin.base_pin().get_debug_pid());
    }
    std::sort(reached.begin(), reached.end());
    std::vector<hhds::Pid> expected{a_out.get_debug_pid(), b_out.get_debug_pid(), extra_out.get_debug_pid()};
    std::sort(expected.begin(), expected.end());
    EXPECT_EQ(reached, expected);
  }
}

TEST(GraphDeclarationApi, CreateFindAndNavigate) {
  hhds::GraphLibrary lib;
  auto               gio = lib.create_io("alu");
  ASSERT_NE(gio, nullptr);
  EXPECT_EQ(gio->get_name(), "alu");
  EXPECT_EQ(lib.find_io("alu"), gio);

  auto graph = gio->create_graph();
  ASSERT_NE(graph, nullptr);
  EXPECT_EQ(graph->get_io(), gio);
  EXPECT_EQ(gio->get_graph(), graph);
}

#ifndef NDEBUG
TEST(GraphTraversalApi, ActiveIteratorsRejectDeletedGraphs) {
  hhds::GraphLibrary lib;
  auto               graph = lib.create_io("top")->create_graph();
  auto               a     = graph->create_node();
  auto               b     = graph->create_node();
  a.create_driver_pin().connect_sink(b.create_sink_pin());
  auto forward = graph->body().nodes(hhds::Node_order::forward).begin();
  auto reverse = graph->body().nodes(hhds::Node_order::reverse).begin();
  auto edges   = a.out_edges();
  auto edge    = edges.begin();
  auto pins    = b.inp_sorted_pins();
  auto pin     = pins.begin();
  lib.delete_graph(graph);

  EXPECT_DEATH((void)*forward, "graph is no longer valid");
  EXPECT_DEATH(++forward, "graph is no longer valid");
  EXPECT_DEATH((void)*reverse, "graph is no longer valid");
  EXPECT_DEATH(++reverse, "graph is no longer valid");
  EXPECT_DEATH((void)*edge, "graph is no longer valid");
  EXPECT_DEATH(++edge, "graph is no longer valid");
  EXPECT_DEATH((void)edges.begin(), "graph is no longer valid");
  EXPECT_DEATH((void)*pin, "graph is no longer valid|structurally mutated");
  EXPECT_DEATH(++pin, "graph is no longer valid|structurally mutated");
  EXPECT_DEATH((void)pins.begin(), "graph is no longer valid");
}
#endif

TEST(GraphWrapperApi, PinsConnectAndIterateEdges) {
  hhds::GraphLibrary lib;

  auto cell = lib.create_io("cell");
  cell->add_input("a", 0);
  cell->add_output("y", 0);

  auto top_io = lib.create_io("top");
  top_io->add_input("x", 1);
  top_io->add_output("z", 0);

  auto top  = top_io->create_graph();
  auto inst = top->create_node();
  inst.set_subnode(cell);

  auto in  = inst.create_sink_pin("a");
  auto out = inst.create_driver_pin("y");

  EXPECT_EQ(inst.get_sink_pin("a"), in);
  EXPECT_EQ(inst.get_driver_pin("y"), out);
  EXPECT_EQ(in.get_pin_name(), "a");
  EXPECT_EQ(out.get_pin_name(), "y");

  in.connect_driver(top->get_input_pin("x"));
  out.connect_sink(top->get_output_pin("z"));

  ASSERT_EQ(in.get_driver_pins().size(), 1);
  EXPECT_EQ(in.get_driver_pin(), top->get_input_pin("x"));
  ASSERT_EQ(out.out_edges().size(), 1);
  EXPECT_EQ(out.out_edges().front().driver, out);
}

TEST(GraphTraversalApi, ForwardClassUsesNodeWrappers) {
  hhds::GraphLibrary lib;
  auto               gio   = lib.create_io("top");
  auto               graph = gio->create_graph();

  auto n1 = graph->create_node();
  auto n2 = graph->create_node();
  auto n3 = graph->create_node();

  n1.create_driver_pin().connect_sink(n2.create_sink_pin());
  n2.create_driver_pin().connect_sink(n3.create_sink_pin());

  std::vector<hhds::Nid> order;
  for (auto node : graph->body().nodes(hhds::Node_order::forward)) {
    EXPECT_EQ(node.get_graph(), graph.get());
    order.push_back(node.get_debug_nid());
  }

  ASSERT_EQ(order.size(), 3);
  EXPECT_EQ(order[0], n1.get_debug_nid());
  EXPECT_EQ(order[1], n2.get_debug_nid());
  EXPECT_EQ(order[2], n3.get_debug_nid());
}

TEST(GraphTraversalApi, BackwardClassUsesNodeWrappers) {
  hhds::GraphLibrary lib;
  auto               gio   = lib.create_io("top");
  auto               graph = gio->create_graph();

  auto n1 = graph->create_node();
  auto n2 = graph->create_node();
  auto n3 = graph->create_node();

  n1.create_driver_pin().connect_sink(n2.create_sink_pin());
  n2.create_driver_pin().connect_sink(n3.create_sink_pin());

  std::vector<hhds::Nid> order;
  for (auto node : graph->body().nodes(hhds::Node_order::reverse)) {
    EXPECT_EQ(node.get_graph(), graph.get());
    order.push_back(node.get_debug_nid());
  }

  ASSERT_EQ(order.size(), 3);
  EXPECT_EQ(order[0], n3.get_debug_nid());
  EXPECT_EQ(order[1], n2.get_debug_nid());
  EXPECT_EQ(order[2], n1.get_debug_nid());
}

TEST(TreeDeclarationApi, CreateFindAndNavigate) {
  auto forest = hhds::Forest::create();
  auto tio    = forest->create_io("tree");
  ASSERT_NE(tio, nullptr);
  EXPECT_EQ(tio->get_name(), "tree");
  EXPECT_EQ(forest->find_io("tree"), tio);

  auto tree = tio->create_tree();
  ASSERT_NE(tree, nullptr);
  EXPECT_EQ(tree->get_io(), tio);
  EXPECT_EQ(tio->get_tree(), tree);
}
