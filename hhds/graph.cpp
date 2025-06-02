#include "graph.hpp"

#include <strings.h>

#include <ctime>
#include <iostream>
#include <vector>

// TODO:
// 2-Remove main from there, do a unit test in tests/graph_test.cpp?
// 3-Fix and use the constants from graph_sizing.hpp to avoid hardcode of 42, 22
// 4-Use odd/even in pin/node so that add_ege can work for pin 0 (pin0==node_id)
// 4.1-Missing to add node 1 and node 2 as input and output from graph at creation time.
// 5-Add a better unit test for add_pin/node/edge for single graph. Make sure that it does not have bugs
// 6-Add the graph_id class
// 7-Allow edges between graphs add_edge(pin>0, pin<0) or add_edge(pin<0, pin>0)
// 8-Benchmark against boost library for some example similar to hardware
// 9-Iterator single graph (fast, fwd, bwd)
// 10-Iterator hierarchical across graphs (fast, fwd, bwd)

// DONE:
// 1-Use namespace hhds like tree

namespace hhds {

Pin::Pin() : master_nid(0), port_id(0), next_pin_id(0), ledge0(0), ledge1(0), use_overflow(0), sedges_{.set = nullptr} {}

Pin::Pin(Nid mn, Port_id pid)
    : master_nid(mn), port_id(pid), next_pin_id(0), ledge0(0), ledge1(0), use_overflow(0), sedges_{.set = nullptr} {}

Nid     Pin::get_master_nid() const { return master_nid; }
Port_id Pin::get_port_id() const { return port_id; }
Pid     Pin::get_next_pin_id() const { return next_pin_id; }
void    Pin::set_next_pin_id(Pid id) { next_pin_id = id; }

auto Pin::overflow_handling(Pid self_id, Vid other_id) -> bool {
  if (use_overflow) {
    sedges_.set->insert(other_id);
    return true;
  }
  auto*              hs        = new ankerl::unordered_dense::set<Vid>();
  constexpr int      SHIFT     = 14;
  constexpr uint64_t SLOT_MASK = (1ULL << SHIFT) - 1;

  for (int i = 0; i < 4; ++i) {
    uint64_t raw = (sedges_.sedges >> (i * SHIFT)) & SLOT_MASK;
    if (!raw) {
      continue;
    }

    bool is_driver = raw & (1ULL << 1);
    bool is_pin    = raw & (1ULL << 0);
    bool neg       = raw & (1ULL << 13);

    uint64_t mag  = (raw >> 2) & ((1ULL << 11) - 1);
    int64_t  diff = neg ? -static_cast<int64_t>(mag) : static_cast<int64_t>(mag);

    Nid actual_self = self_id >> 2;
    Vid nid         = static_cast<Vid>(actual_self - diff);
    Vid target      = (nid << 2) | (is_driver ? 2 : 0) | (is_pin ? 1 : 0);
    hs->insert(target);
  }
  if (ledge0) {
    hs->insert(static_cast<Vid>(ledge0));
  }
  if (ledge1) {
    hs->insert(static_cast<Vid>(ledge1));
  }
  use_overflow   = true;
  sedges_.sedges = 0;
  sedges_.set    = hs;
  ledge0 = ledge1 = 0;

  hs->insert(other_id);
  if (sedges_.set == nullptr) {
    return false;
  }
  return true;
}

auto Pin::add_edge(Pid self_id, Vid other_id) -> bool {
  if (use_overflow) {
    return overflow_handling(self_id, other_id);
  }
  Nid actual_self  = self_id >> 2;
  Vid actual_other = other_id >> 2;

  int64_t  diff  = static_cast<int64_t>(actual_self) - static_cast<int64_t>(actual_other);
  bool     isNeg = diff < 0;
  uint64_t mag   = static_cast<uint64_t>(isNeg ? -diff : diff);

  constexpr uint64_t MAX_MAG = (1ULL << 11) - 1;
  if (mag > MAX_MAG) {
    return overflow_handling(self_id, other_id);
  }

  uint64_t e = (mag & MAX_MAG) << 2;
  e |= (other_id & 2) ? (1ULL << 1) : 0;
  e |= (other_id & 1) ? (1ULL << 0) : 0;
  if (isNeg) {
    e |= (1ULL << 13);
  }
  constexpr int      SHIFT = 14;
  constexpr uint64_t SLOT  = (1ULL << SHIFT) - 1;
  for (int i = 0; i < 4; ++i) {
    uint64_t mask = SLOT << (i * SHIFT);
    if ((sedges_.sedges & mask) == 0) {
      sedges_.sedges |= (e & SLOT) << (i * SHIFT);
      return true;
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

Pin::EdgeRange::EdgeRange(const Pin* pin, Pid pid) noexcept : pin_(pin), pid_(pid), set_(nullptr), own_(false) {

  if (pin->use_overflow) {
    // set_ = pin->sedges_.set;
    set_ = acquire_set();
    set_->clear();
    for (auto raw : *pin->sedges_.set) {
      bool is_driver = raw & (1ULL << 1);
      bool is_pin    = raw & (1ULL << 0);
      Vid  actual_id = raw >> 2;  // get the actual Node ID
      actual_id      = actual_id << 1;
      if (is_pin) {
        actual_id = actual_id | 1;  // set the pin bit
      }
      set_->insert(actual_id);
    }
  } else {
    set_ = acquire_set();
    own_ = true;
    set_->clear();
    populate_set(pin, *set_, pid);
  }
}

Pin::EdgeRange::EdgeRange(EdgeRange&& o) noexcept : pin_(o.pin_), pid_(o.pid_), set_(o.set_), own_(o.own_) {
  o.pin_ = nullptr;
  o.set_ = nullptr;
}

Pin::EdgeRange::~EdgeRange() noexcept {
  if (own_) {
    release_set(set_);
  }
}

ankerl::unordered_dense::set<Vid>* Pin::EdgeRange::acquire_set() noexcept {
  static thread_local std::vector<ankerl::unordered_dense::set<Vid>*> pool;
  if (!pool.empty()) {
    ankerl::unordered_dense::set<Vid>* set = pool.back();
    pool.pop_back();
    return set;
  } else {
    auto* set = new ankerl::unordered_dense::set<Vid>();
    set->reserve(MAX_EDGES);
    return set;
  }
}

void Pin::EdgeRange::release_set(ankerl::unordered_dense::set<Vid>* set) noexcept {
  static thread_local std::vector<ankerl::unordered_dense::set<Vid>*> pool;
  pool.push_back(set);
}

void Pin::EdgeRange::populate_set(const Pin* pin, ankerl::unordered_dense::set<Vid>& set, Pid pid) noexcept {
  constexpr uint64_t SLOT_MASK = (1ULL << 14) - 1;
  uint64_t           packed    = pin->sedges_.sedges;
  for (int i = 0; i < 4; i++) {
    uint64_t e = (packed >> (i * 14)) & SLOT_MASK;
    if (e == 0) {
      continue;
    }
    bool isNeg = e & (1ULL << 13);
    bool isPin = e & 1;
    e &= ~(1ULL << 13);  // unset the 13th bit
    if (isNeg) {
      e = ((pid >> 1) + (e >> 2));
    } else {
      e = ((pid >> 1) - (e >> 2));
    }
    e = e << 1;  // shift to make it a Vid
    if (isPin) {
      e = e | 1;  // set the pin bit
    }
    set.insert(e);
  }
  if (pin->ledge0) {
    bool is_pin  = pin->ledge0 & 1;
    Vid  ledge0_ = pin->ledge0 >> 1;
    if (is_pin) {
      ledge0_ = ledge0_ | 1;
    } else {
      ledge0_ = ledge0_ & ~1;
    }
    set.insert(ledge0_);
  }
  if (pin->ledge1) {
    bool is_pin  = pin->ledge1 & 1;
    Vid  ledge1_ = pin->ledge1 >> 1;
    if (is_pin) {
      ledge1_ = ledge1_ | 1;
    } else {
      ledge1_ = ledge1_ & ~1;
    }
    set.insert(ledge1_);
  }
}

auto Pin::get_edges(Pid pid) const noexcept -> EdgeRange { return EdgeRange(this, pid); }

bool Pin::has_edges() const {
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

auto Node::overflow_handling(Nid self_id, Vid other_id) -> bool {
  if (use_overflow) {
    sedges_.set->insert(other_id);
    return true;
  }

  auto*              hs        = new ankerl::unordered_dense::set<Vid>();
  constexpr int      SHIFT     = 14;
  constexpr uint64_t SLOT_MASK = (1ULL << SHIFT) - 1;

  for (int i = 0; i < 4; ++i) {
    uint64_t raw = (sedges_.sedges >> (i * SHIFT)) & SLOT_MASK;
    if (!raw) {
      continue;
    }
    bool is_driver = raw & 2;
    bool is_pin    = raw & 1;

    bool     neg  = raw & (1ULL << 13);
    uint64_t mag  = (raw >> 2) & ((1ULL << 11) - 1);
    int64_t  diff = neg ? -static_cast<int64_t>(mag) : static_cast<int64_t>(mag);

    Nid actual_self = static_cast<Nid>(self_id >> 2);
    Vid target      = static_cast<Vid>(actual_self + diff);
    target          = (target << 2);
    if (is_driver) {
      target |= 2;
    }
    if (is_pin) {
      target |= 1;
    }
    hs->insert(target);
  }

  if (ledge0) {
    hs->insert(static_cast<Vid>(ledge0));
  }
  if (ledge1) {
    hs->insert(static_cast<Vid>(ledge1));
  }

  use_overflow   = 1;
  sedges_.sedges = 0;
  sedges_.set    = hs;
  ledge0 = ledge1 = 0;

  hs->insert(other_id);
  return true;
}

auto Node::add_edge(Nid self_id, Vid other_id) -> bool {
  if (use_overflow) {
    return overflow_handling(self_id, other_id);
  }

  Nid     actual_self  = self_id >> 2;
  Vid     actual_other = other_id >> 2;
  int64_t diff         = static_cast<int64_t>(actual_self) - static_cast<int64_t>(actual_other);

  bool     isNeg = diff < 0;
  uint64_t mag   = static_cast<uint64_t>(isNeg ? -diff : diff);

  constexpr uint64_t MAX_MAG = (1ULL << 11) - 1;
  if (mag > MAX_MAG) {
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

  uint64_t e = 0;
  if (isNeg) {
    e |= 1ULL << 13;
  }
  e |= mag << 2;
  if (other_id & 2) {
    uint64_t temp2 = 1ULL << 1;
    e              = e | 2;
  }
  if (other_id & 1) {
    uint64_t temp1 = 1ULL << 0;
    e              = e | 1;
  }
  constexpr int      SHIFT = 14;
  constexpr uint64_t SLOT  = (1ULL << SHIFT) - 1;
  for (int i = 0; i < 4; ++i) {
    uint64_t mask = SLOT << (i * SHIFT);
    if ((sedges_.sedges & mask) == 0) {
      sedges_.sedges |= (e & SLOT) << (i * SHIFT);
      return true;
    }
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

Node::EdgeRange::EdgeRange(const Node* node, Nid nid) noexcept : node_(node), nid_(nid), set_(nullptr), own_(false) {
  if (node->use_overflow) {
    set_ = acquire_set();
    set_->clear();
    for (auto raw : *node->sedges_.set) {
      bool is_driver = raw & (1ULL << 1);
      bool is_pin    = raw & (1ULL << 0);
      Vid  actual_id = raw >> 2;  // get the actual Node ID
      actual_id      = actual_id << 1;
      if (is_pin) {
        actual_id = actual_id | 1;  // set the pin bit
      }
      set_->insert(actual_id);
    }
  } else {
    set_ = acquire_set();
    own_ = true;
    set_->clear();
    populate_set(node, *set_, nid);
  }
}

Node::EdgeRange::EdgeRange(EdgeRange&& o) noexcept : node_(o.node_), nid_(o.nid_), set_(o.set_), own_(o.own_) {
  o.node_ = nullptr;
  o.set_  = nullptr;
}

Node::EdgeRange::~EdgeRange() noexcept {
  if (own_) {
    release_set(set_);
  }
}

ankerl::unordered_dense::set<Nid>* Node::EdgeRange::acquire_set() noexcept {
  static thread_local std::vector<ankerl::unordered_dense::set<Nid>*> pool;
  if (!pool.empty()) {
    ankerl::unordered_dense::set<Nid>* set = pool.back();
    pool.pop_back();
    return set;
  } else {
    auto* set = new ankerl::unordered_dense::set<Nid>();
    set->reserve(MAX_EDGES);
    return set;
  }
}

void Node::EdgeRange::release_set(ankerl::unordered_dense::set<Nid>* set) noexcept {
  static thread_local std::vector<ankerl::unordered_dense::set<Nid>*> pool;
  pool.push_back(set);
}
void Node::EdgeRange::populate_set(const Node* node, ankerl::unordered_dense::set<Nid>& set, Nid nid) noexcept {
  constexpr uint64_t SLOT_MASK = (1ULL << 14) - 1;
  uint64_t           packed    = node->sedges_.sedges;
  for (int i = 0; i < 4; i++) {
    uint64_t e = (packed >> (i * 14)) & SLOT_MASK;
    if (e == 0) {
      continue;
    }
    bool isNeg = e & (1ULL << 13);
    bool isPin = e & 1;
    e &= ~(1ULL << 13);  // unset the 13th bit
    if (isNeg) {
      e = ((nid >> 1) + (e >> 2));
    } else {
      e = ((nid >> 1) - (e >> 2));
    }
    e = e << 1;  // shift to make it a Vid
    if (isPin) {
      e = e | 1;  // set the pin bit
    }

    set.insert(e);
  }
  if (node->ledge0) {
    bool is_pin  = node->ledge0 & 1;
    Vid  ledge0_ = node->ledge0 >> 1;
    if (is_pin) {
      ledge0_ = ledge0_ | 1;
    } else {
      ledge0_ = ledge0_ & ~1;
    }
    set.insert(ledge0_);
  }
  if (node->ledge1) {
    bool is_pin  = node->ledge1 & 1;
    Vid  ledge1_ = node->ledge1 >> 1;
    if (is_pin) {
      ledge1_ = ledge1_ | 1;
    } else {
      ledge1_ = ledge1_ & ~1;
    }
    set.insert(ledge1_);
  }
}

auto Node::get_edges(Nid nid) const noexcept -> EdgeRange { return EdgeRange(this, nid); }

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
  return id << 1 | 0;
}

auto Graph::create_pin(Nid nid, Port_id pid) -> Pid {
  Pid id = pin_table.size();
  assert(id);
  pin_table.emplace_back(nid, pid);
  set_next_pin(nid, id);
  return id << 1 | 1;
}

void Graph::add_edge(Vid driver_id, Vid sink_id) {
  bool driver_type, sink_type;
  if (driver_id & 1) {
    // driver is pin
    driver_type = true;
  } else {
    // driver is node
    driver_type = false;
  }
  if (sink_id & 1) {
    // sink is pin
    sink_type = true;
  } else {
    // sink is node
    sink_type = false;
  }

  driver_id = driver_id | 1;
  sink_id   = sink_id & ~1;
  driver_id = driver_id << 1;
  sink_id   = sink_id << 1;
  if (driver_type) {
    driver_id = driver_id | 1;
  }
  if (sink_type) {
    sink_id = sink_id | 1;
  }

  add_edge_int(driver_id, sink_id);
  add_edge_int(sink_id, driver_id);
}

auto Graph::ref_node(Nid id) const -> Node* {
  Nid actual_id = id >> 1;
  assert(actual_id < node_table.size());
  return (Node*)&node_table[actual_id];
}

auto Graph::ref_pin(Pid id) const -> Pin* {
  Pid actual_id = id >> 1;
  assert(actual_id < pin_table.size());
  return (Pin*)&pin_table[actual_id];
}

void Graph::add_edge_int(Vid self_id, Vid other_id) {
  bool self_type;
  Vid  actual_self  = self_id >> 2;
  Vid  actual_other = other_id >> 2;
  if ((self_id & 1)) {
    self_type = true;
  } else {
    self_type = false;
  }

  // if both are of same type => ensure their actua values are different
  if ((self_id & 1) == (other_id & 1)) {
    assert(actual_self != actual_other);
  }
  Vid ref_self = actual_self << 1;
  if (self_type) {
    ref_self = ref_self | 1;
  }
  // calling respective class instances
  if (!self_type) {
    auto* node = ref_node(ref_self);
    node->add_edge(self_id, other_id);
  } else {
    auto* pin = ref_pin(ref_self);
    pin->add_edge(self_id, other_id);
  }
}

void Graph::set_next_pin(Nid nid, Pid next_pin) {
  Nid  actual_nid = nid >> 1;
  auto node       = &node_table[actual_nid];
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
      auto sed = p->get_edges(pid);
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
