// A subnode whose own entry holds no edge keeps its callee gid inline.
//
// Every cell of a mapped netlist is a subnode, and its edges sit on its pins (port ids 1 and up), not
// in its own entry. set_subnode used to move such a node to an overflow edge set only to free the
// field that holds the callee gid: a hash set per cell. The gid now stays in the entry until the entry
// gets an edge of its own (a port-0 edge); then it moves to an overflow set, so a subnode with port-0
// edges is stored and visited exactly as before.
#include <unistd.h>

#include <cstdio>
#include <filesystem>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include "hhds/graph.hpp"

namespace {
void check(bool value, const char* what) {
  if (!value) {
    throw std::runtime_error(std::string("inline subnode: ") + what);
  }
}

// The drivers of `node`'s port-0 sink and the sinks of its port-0 driver, as node ids.
std::multiset<hhds::Nid> fanin(const hhds::Node_class& node) {
  std::multiset<hhds::Nid> out;
  for (const auto& driver : node.create_sink_pin(0).get_driver_pins()) {
    out.insert(driver.get_master_node().get_debug_nid());
  }
  return out;
}
std::multiset<hhds::Nid> fanout(const hhds::Node_class& node) {
  std::multiset<hhds::Nid> out;
  for (const auto& edge : node.create_driver_pin(0).out_edges()) {
    out.insert(edge.sink.get_master_node().get_debug_nid());
  }
  return out;
}
}  // namespace

int main() {
  namespace fs = std::filesystem;
  hhds::GraphLibrary lib;
  auto               cell = lib.create_io("cell");
  cell->add_input("a", 1);
  cell->add_output("y", 2);
  auto other = lib.create_io("other_cell");
  other->add_input("a", 1);
  other->add_output("y", 2);
  auto top   = lib.create_io("top");
  auto graph = top->create_graph();

  // A netlist of cells, wired through pins 1 (input) and 2 (output): a thousand subnodes and no
  // overflow set at all.
  std::vector<hhds::Node_class> cells;
  for (int i = 0; i < 1000; ++i) {
    auto node = graph->create_node();
    node.set_subnode(cell);
    cells.push_back(node);
  }
  for (size_t i = 1; i < cells.size(); ++i) {
    cells[i - 1].create_driver_pin(2).connect_sink(cells[i].create_sink_pin(1));
  }
  check(graph->debug_overflow_entry_count() == 0, "a chain of cells needs no overflow set");
  for (size_t i = 0; i < cells.size(); ++i) {
    check(cells[i].get_subnode_gid() == cell->get_gid(), "callee gid of an inline subnode");
    check(cells[i].create_sink_pin(1).get_driver_pins().size() == (i == 0 ? 0U : 1U), "chain edges");
    check(fanin(cells[i]).empty() && fanout(cells[i]).empty(), "no edge in the entry itself");
  }

  // Deleting a port-0 edge that does not exist must not disturb the gid or the mark (the mark decodes
  // as a self reference, the gid as an arbitrary id).
  auto hub = cells[500];
  cells[700].create_sink_pin(0).del_sink(hub.create_driver_pin(0));
  hub.create_sink_pin(0).del_sink(hub.create_driver_pin(0));
  check(hub.get_subnode_gid() == cell->get_gid() && fanout(hub).empty(), "absent delete is a no-op");

  // Re-targeting keeps the node inline.
  hub.set_subnode(other);
  check(hub.get_subnode_gid() == other->get_gid(), "set_subnode again");
  check(graph->debug_overflow_entry_count() == 0, "re-targeting allocates nothing");

  // The first port-0 edge moves the entry to an overflow set; the gid moves with it, and the edges are
  // visited in insertion order, as for any overflowed entry.
  std::multiset<hhds::Nid> expected;
  std::vector<hhds::Nid>   order;
  const auto               connect = [&](size_t to) {
    hub.create_driver_pin(0).connect_sink(cells[to].create_sink_pin(0));
    expected.insert(cells[to].get_debug_nid());
    order.push_back(cells[to].get_debug_nid());
  };
  connect(999);  // far first, then near: slot order would put the near ones first
  connect(501);
  connect(0);
  connect(502);
  check(graph->debug_overflow_entry_count() == 5, "a port-0 edge overflows the entries at both ends: the hub and its four sinks");
  check(hub.get_subnode_gid() == other->get_gid(), "the gid survives the move to overflow");
  check(fanout(hub) == expected, "port-0 edges of a subnode");
  {
    std::vector<hhds::Nid> seen;
    for (const auto& edge : hub.create_driver_pin(0).out_edges()) {
      seen.push_back(edge.sink.get_master_node().get_debug_nid());
    }
    check(seen == order, "port-0 edges of a subnode are visited in insertion order");
  }
  cells[501].create_sink_pin(0).del_sink(hub.create_driver_pin(0));
  expected.erase(expected.find(cells[501].get_debug_nid()));
  check(fanout(hub) == expected && hub.get_subnode_gid() == other->get_gid(), "delete keeps the gid");
  for (size_t to = 600; to < 640; ++to) {
    connect(to);
  }
  cells[499].create_driver_pin(0).connect_sink(hub.create_sink_pin(0));
  check(hub.get_subnode_gid() == other->get_gid() && fanout(hub) == expected && fanin(hub).size() == 1, "a wide fanout");
  check(graph->debug_back_index_consistent(), "back-edge index of an overflowed subnode");

  // A node that already has port-0 edges when it becomes a subnode overflows at once, as it always did.
  auto late = graph->create_node();
  for (const size_t to : {0U, 999U, 1U, 998U, 2U, 997U, 3U, 996U}) {  // far and near, enough to use every inline slot
    late.create_driver_pin(0).connect_sink(cells[to].create_sink_pin(0));
  }
  const auto late_fanout = fanout(late);
  late.set_subnode(cell);
  check(late.get_subnode_gid() == cell->get_gid() && fanout(late) == late_fanout, "set_subnode on a node full of edges");

  // A plain node is not a subnode, whatever its edges are.
  auto plain = graph->create_node();
  plain.create_driver_pin(0).connect_sink(cells[10].create_sink_pin(1));
  check(plain.get_subnode_gid() == hhds::Gid_invalid, "a plain node has no callee");

  // Save and load: the entry is stored as it is.
  const auto dir = fs::temp_directory_path() / ("hhds_inline_subnode_test_" + std::to_string(::getpid()));
  fs::remove_all(dir);
  lib.save(dir.string());
  hhds::GraphLibrary loaded;
  loaded.load(dir.string());
  auto   again    = loaded.find_io("top")->get_graph();
  size_t subnodes = 0, of_cell = 0;
  for (auto node : again->body().nodes()) {
    if (node.get_subnode_gid() != hhds::Gid_invalid) {
      ++subnodes;
      of_cell += node.get_subnode_gid() == cell->get_gid() ? 1 : 0;
    }
  }
  check(subnodes == 1001 && of_cell == 1000, "subnodes after save and load");
  check(again->debug_overflow_entry_count() == graph->debug_overflow_entry_count(), "the same entries overflow after load");
  check(again->debug_back_index_consistent(), "back-edge index after load");
  fs::remove_all(dir);
  std::puts("inline subnode: ok");
  return 0;
}
