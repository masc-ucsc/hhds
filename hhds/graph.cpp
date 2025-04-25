#include "graph.hpp"

#include <strings.h>

#include <ctime>
#include <iostream>
#include <vector>

// TODO:
// 2-Remove main from there, do a unit test in tests/graph_test.cpp?
// 3-Fix and use the constants from graph_sizing.hpp to avoid hardcode of 42, 22
// 4-Use odd/even in pin/node so that add_ege can work for pin 0 (pin0==node_id)
// 5-Add a better unit test for add_pin/node/edge for single graph. Make sure that it does not have bugs
// 6-Add the graph_id class
// 7-Allow edges between graphs add_edge(pin>0, pin<0) or add_edge(pin<0, pin>0)
// 8-Benchmark against boost library for some example similar to hardware
// 9-Iterator single graph (fast, fwd, bwd)
// 10-Iterator hierarchical across graphs (fast, fwd, bwd)

// DONE:
// 1-Use namespace hhds like tree

namespace hhds {

Pin::Pin() : master_nid(0), port_id(0), sedge(0), next_pin_id(0), use_overflow(0) {}

Pin::Pin(Nid mn, Port_id pid) : master_nid(mn), port_id(pid), sedge(0), next_pin_id(0), use_overflow(0) {}

Nid     Pin::get_master_nid() const { return master_nid; }
Port_id Pin::get_port_id() const { return port_id; }
Pid     Pin::get_next_pin_id() const { return next_pin_id; }
void    Pin::set_next_pin_id(Pid id) { next_pin_id = id; }

bool Pin::overflow_handling(Pid self_id, Pid other_id) {
  int64_t                temp_sedge  = sedge;
  emhash7::HashSet<Pid>* temp_ledges = nullptr;

  if (!use_overflow) {
    // stash the 4×12-bit shorts
    sedge       = reinterpret_cast<int64_t>(new emhash7::HashSet<Pid>());
    temp_ledges = reinterpret_cast<emhash7::HashSet<Pid>*>(sedge);
    for (int i = 0; i < 4; ++i) {
      int32_t edge = (temp_sedge >> (i * 12)) & 0xFFF;
      if (edge & 0x800) {
        edge |= 0xFFFFF000;
      }
      if (edge) {
        temp_ledges->insert(self_id + edge);
      }
    }
    use_overflow = 1;
  } else if (sedge) {
    temp_ledges = reinterpret_cast<emhash7::HashSet<Pid>*>(sedge);
  }

  temp_ledges->insert(other_id);
  return true;
}

auto Pin::add_edge(Pid self_id, Pid other_id) -> bool {
  assert(self_id != other_id);
  std::cout << "Adding edge " << self_id << "↔" << other_id << "\n";

  if (use_overflow) {
    std::cout << " overflow branch\n\n";
    return overflow_handling(self_id, other_id);
  }

  if (((sedge >> (3 * 12)) & 0xFFF) != 0) {
    std::cout << " short-edge full; overflow\n\n";
    return overflow_handling(self_id, other_id);
  }

  int64_t diff = static_cast<int32_t>(other_id) - static_cast<int32_t>(self_id);
  if (diff <= -(1 << 11) || diff >= (1 << 11)) {
    std::cout << " diff out-of-range; overflow\n\n";
    return overflow_handling(self_id, other_id);
  }

  for (int i = 0; i < 4; ++i) {
    int64_t mask = (0xFFFLL << (i * 12));
    if ((sedge & mask) == 0) {
      sedge |= (diff & 0xFFFLL) << (i * 12);
      std::cout << " added short edge at slot " << i << "\n\n";
      return true;
    }
  }
  return true;
}

bool Pin::has_edges() const { return use_overflow != 0 || sedge != 0; }

auto Pin::get_sedges(Pid pid) const -> std::array<int32_t, 4> {
  std::array<int32_t, 4> edges{};
  int                    cnt = 0;

  if (use_overflow) {
    auto hs = reinterpret_cast<emhash7::HashSet<Pid>*>(sedge);
    for (auto v : *hs) {
      if (cnt < 4) {
        edges[cnt++] = v;
      }
    }
    return edges;
  }

  for (int i = 0; i < 4; ++i) {
    int32_t e = (sedge >> (i * 12)) & 0xFFF;
    if (e & 0x800) {
      e |= 0xFFFFF000;
    }
    if (e) {
      edges[cnt++] = pid + e;
    }
  }
  return edges;
}

Node::Node() { clear_node(); }
Node::Node(Nid nid_val) {
  clear_node();
  nid = nid_val;
}

void Node::clear_node() { bzero(this, sizeof(Node)); }

void Node::set_type(Type t) { type = t; }
Nid  Node::get_nid() const { return nid; }
Type Node::get_type() const { return type; }
Pid  Node::get_next_pin_id() const { return next_pin_id; }
void Node::set_next_pin_id(Pid id) { next_pin_id = id; }

Graph::Graph() { clear_graph(); }

void Graph::clear_graph() {
  bzero(this, sizeof(Graph));
  node_table.emplace_back(0);
  pin_table.emplace_back(0, 0);
}

auto Graph::create_node() -> Nid {
  Nid id = node_table.size();
  assert(id);
  node_table.emplace_back(id);
  return id;
}

auto Graph::create_pin(Nid nid, Port_id pid) -> Pid {
  Pid id = pin_table.size();
  assert(id);
  pin_table.emplace_back(nid, pid);
  set_next_pin(nid, id);
  return id;
}

void Graph::add_edge(Pid d, Pid s) {
  add_edge_int(d, s);
  add_edge_int(s, d);
}

auto Graph::ref_node(Nid id) const -> Node* {
  assert(id < node_table.size());
  return (Node*)&node_table[id];
}

auto Graph::ref_pin(Pid id) const -> Pin* {
  assert(id < pin_table.size());
  return (Pin*)&pin_table[id];
}

void Graph::add_edge_int(Pid self_id, Pid other_id) {
  if (!ref_pin(self_id)->add_edge(self_id, other_id)) {
    std::cout << "add_edge_int failed: " << self_id << " " << other_id << "\n";
  }
}

void Graph::set_next_pin(Nid nid, Pid next_pin) {
  auto node = &node_table[nid];
  if (node->get_next_pin_id() == 0) {
    node->set_next_pin_id(next_pin);
    return;
  }
  Pid cur = node->get_next_pin_id();
  while (pin_table[cur].get_next_pin_id()) {
    cur = pin_table[cur].get_next_pin_id();
  }
  pin_table[cur].set_next_pin_id(next_pin);
}

void Graph::display_graph() const {
  for (Pid pid = 1; pid < pin_table.size(); ++pid) {
    auto p = ref_pin(pid);
    std::cout << "Pin " << pid << "  node=" << p->get_master_nid() << " port=" << p->get_port_id() << "\n";
    if (p->has_edges()) {
      auto sed = p->get_sedges(pid);
      std::cout << "  edges:";
      for (auto e : sed) {
        if (e) {
          std::cout << " " << e;
        }
      }
      std::cout << "\n";
    }
    std::cout << "  next_pin=" << p->get_next_pin_id() << "\n";
  }
}

void Graph::display_next_pin_of_node() const {
  for (Nid nid = 1; nid < node_table.size(); ++nid) {
    std::cout << "Node " << nid << " first_pin=" << node_table[nid].get_next_pin_id() << "\n";
  }
}

}  // namespace hhds
