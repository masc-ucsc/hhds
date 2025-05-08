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

Pin::Pin() : master_nid(0), port_id(0), next_pin_id(0), use_overflow(0), ledge0(0), ledge1(0), sedges_{.sedges = 0} {}

Pin::Pin(Nid mn, Port_id pid)
    : master_nid(mn), port_id(pid), next_pin_id(0), use_overflow(0), ledge0(0), ledge1(0), sedges_{.sedges = 0} {}

Nid     Pin::get_master_nid() const { return master_nid; }
Port_id Pin::get_port_id() const { return port_id; }
Pid     Pin::get_next_pin_id() const { return next_pin_id; }
void    Pin::set_next_pin_id(Pid id) { next_pin_id = id; }

auto Pin::overflow_handling(Pid self_id, Pid other_id) -> bool {
  if (use_overflow) {
    sedges_.set->insert(other_id);
    return true;
  }

  int64_t diff = static_cast<int64_t>(other_id) - static_cast<int64_t>(self_id);
  if (-(1 << 11) <= diff && diff < (1 << 11)) {
    for (int i = 0; i < 4; ++i) {
      int64_t mask = 0xFFFLL << (i * 12);
      if ((sedges_.sedges & mask) == 0) {
        sedges_.sedges |= (diff & 0xFFFLL) << (i * 12);
        return true;
      }
    }
  }

  if (ledge0 == 0) {
    ledge0 = other_id;
    return true;
  }
  if (ledge1 == 0) {
    ledge1 = other_id;
    return true;
  }
  auto* hs = new emhash7::HashSet<Pid>();
  for (int i = 0; i < 4; ++i) {
    int64_t slot = (sedges_.sedges >> (i * 12)) & 0xFFF;
    if (slot) {
      if (slot & 0x800) {
        slot |= 0xFFFFF000;
      }
      hs->insert(self_id + slot);
    }
  }
  hs->insert(ledge0);
  hs->insert(ledge1);
  sedges_.set    = hs;
  use_overflow   = 1;
  sedges_.sedges = 0;
  ledge0         = 0;
  ledge1         = 0;

  hs->insert(other_id);
  return true;
}

auto Pin::add_edge(Pid self_id, Pid other_id) -> bool {
  assert(self_id != other_id);
  std::cout << "Adding edge " << self_id << "↔" << other_id << "\n";

  if (use_overflow) {
    std::cout << " overflow branch\n\n";
    return overflow_handling(self_id, other_id);
  }

  int64_t diff = static_cast<int64_t>(other_id) - static_cast<int64_t>(self_id);
  if (-(1 << 11) <= diff && diff < (1 << 11)) {
    for (int i = 0; i < 4; ++i) {
      int64_t mask = 0xFFFLL << (i * 12);
      if ((sedges_.sedges & mask) == 0) {
        sedges_.sedges |= (diff & 0xFFFLL) << (i * 12);
        std::cout << " added short edge at slot " << i << "\n\n";
        return true;
      }
    }
  }

  if (ledge0 == 0) {
    ledge0 = other_id;
    std::cout << " placed far-edge in ledge0\n\n";
    return true;
  }
  if (ledge1 == 0) {
    ledge1 = other_id;
    std::cout << " placed far-edge in ledge1\n\n";
    return true;
  }

  std::cout << " overflow via set\n\n";
  return overflow_handling(self_id, other_id);
}

bool Pin::has_edges() const { return use_overflow != 0 || sedges_.sedges != 0; }

auto Pin::get_sedges(Pid pid) const -> std::array<int64_t, 4> {
  std::array<int64_t, 4> edges{};
  int                    cnt = 0;
  if (use_overflow) {
    for (auto v : *sedges_.set) {
      if (cnt < 4) {
        edges[cnt++] = v;
      }
    }
    return edges;
  }

  int64_t packed = sedges_.sedges;
  for (int i = 0; i < 4; ++i) {
    int64_t e = (packed >> (i * 12)) & 0xFFF;
    // sign-extend 12→32 bits:
    if (e & 0x800) {
      e |= 0xFFFFF000;
    }
    if (e) {
      edges[cnt++] = pid + e;
      if (cnt == 4) {
        return edges;
      }
    }
  }

  // 3) If there’s room, include ledge0 / ledge1:
  if (ledge0 && cnt < 4) {
    edges[cnt++] = ledge0;
  }
  if (ledge1 && cnt < 4) {
    edges[cnt++] = ledge1;
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

bool Node::overflow_handling(Pid self_id, Pid other_id) {
  if (use_overflow) {
    sedges_.set->insert(other_id);
    return true;
  }

  int64_t diff = static_cast<int64_t>(other_id) - static_cast<int64_t>(self_id);
  if (-(1 << 11) <= diff && diff < (1 << 11)) {
    for (int i = 0; i < 4; ++i) {
      int64_t mask = 0xFFFLL << (i * 12);
      if ((sedges_.sedges & mask) == 0) {
        sedges_.sedges |= (diff & 0xFFFLL) << (i * 12);
        return true;
      }
    }
  }

  if (ledge0 == 0) {
    ledge0 = other_id;
    return true;
  }
  if (ledge1 == 0) {
    ledge1 = other_id;
    return true;
  }

  auto* hs = new emhash7::HashSet<Pid>();
  for (int i = 0; i < 4; ++i) {
    int64_t slot = (sedges_.sedges >> (i * 12)) & 0xFFF;
    if (slot) {
      if (slot & 0x800) {
        slot |= 0xFFFFF000;
      }
      hs->insert(self_id + slot);
    }
  }

  hs->insert(ledge0);
  hs->insert(ledge1);

  sedges_.sedges = 0;
  ledge0 = ledge1 = 0;

  sedges_.set  = hs;
  use_overflow = 1;

  hs->insert(other_id);
  return true;
}

auto Node::add_edge(Pid self_id, Pid other_id) -> bool {
  assert(self_id != other_id);

  if (use_overflow) {
    return overflow_handling(self_id, other_id);
  }

  int64_t diff = static_cast<int64_t>(other_id) - static_cast<int64_t>(self_id);
  if (-(1 << 11) <= diff && diff < (1 << 11)) {
    for (int i = 0; i < 4; ++i) {
      int64_t mask = 0xFFFLL << (i * 12);
      if ((sedges_.sedges & mask) == 0) {
        sedges_.sedges |= (diff & 0xFFFLL) << (i * 12);
        return true;
      }
    }
  }

  if (ledge0 == 0) {
    ledge0 = other_id;
    return true;
  }
  if (ledge1 == 0) {
    ledge1 = other_id;
    return true;
  }

  return overflow_handling(self_id, other_id);
}

bool Node::has_edges() const {
  if (use_overflow) {
    return true;
  }
  if (sedges_.sedges != 0) {
    return true;
  }
  if (ledge0 != 0) {
    return true;
  }
  if (ledge1 != 0) {
    return true;
  }
  return false;
}

auto Node::get_sedges(Pid pid) const -> std::array<int64_t, 4> {
  std::array<int64_t, 4> edges{};
  int                    cnt = 0;

  if (use_overflow) {
    for (auto v : *sedges_.set) {
      if (cnt < 4) {
        edges[cnt++] = v;
      }
    }
    return edges;
  }

  int64_t packed = sedges_.sedges;
  for (int i = 0; i < 4; ++i) {
    int32_t e = (packed >> (i * 12)) & 0xFFF;
    if (e & 0x800) {
      e |= 0xFFFFF000;
    }
    if (e && cnt < 4) {
      edges[cnt++] = pid + e;
    }
  }

  if (ledge0 && cnt < 4) {
    edges[cnt++] = ledge0;
  }
  if (ledge1 && cnt < 4) {
    edges[cnt++] = ledge1;
  }

  return edges;
}

Graph::Graph() { clear_graph(); }

void Graph::clear_graph() {
  bzero(this, sizeof(Graph));
  node_table.emplace_back(0);
  node_table.emplace_back(1);
  node_table.emplace_back(2);
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
