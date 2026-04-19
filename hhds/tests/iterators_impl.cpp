#include <gtest/gtest.h>

#include <vector>

#include "hhds/graph.hpp"
#include "hhds/tree.hpp"

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

  ASSERT_EQ(in.inp_edges().size(), 1);
  EXPECT_EQ(in.inp_edges().front().sink, in);
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
  for (auto node : graph->forward_class()) {
    EXPECT_EQ(node.get_graph(), graph.get());
    order.push_back(node.get_debug_nid());
  }

  ASSERT_EQ(order.size(), 3);
  EXPECT_EQ(order[0], n1.get_debug_nid());
  EXPECT_EQ(order[1], n2.get_debug_nid());
  EXPECT_EQ(order[2], n3.get_debug_nid());
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
