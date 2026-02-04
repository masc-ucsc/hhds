#include "graph.hpp"

#include <strings.h>

#include <ctime>
#include <iostream>
#include <vector>

// TODO:
// 6-Add the graph_id class
// 7-Allow edges between graphs add_edge(pin>0, pin<0) or add_edge(pin<0, pin>0)
// 8-Benchmark against boost library for some example similar to hardware
// 9-Iterator single graph (fast, fwd, bwd)
// 10-Iterator hierarchical across graphs (fast, fwd, bwd)

// DONE:
// 1-Use namespace hhds like tree
// 2-Remove main from there, do a unit test in tests/graph_test.cpp?
// 3-Fix and use the constants from graph_sizing.hpp to avoid hardcode of 42, 22
// 4-Use odd/even in pin/node so that add_ege can work for pin 0 (pin0==node_id)
// 4.1-Missing to add node 1 and node 2 as input and output from graph at creation time.
// 5-Add a better unit test for add_pin/node/edge for single graph. Make sure that it does not have bugs

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
  e |= (mag << 2);
  if (other_id & 2) {
    e = e | 2;
  }
  if (other_id & 1) {
    e = e | 1;
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
  // if we reach here, insert into ledge0 or ledge1, or overflow
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

Pin::EdgeRange::EdgeRange(const Pin* pin, Pid pid) noexcept : pin_(pin), /* pid_(pid), */ set_(nullptr), own_(false) {
  if (pin->use_overflow) {
    set_ = acquire_set();
    set_->clear();
    for (auto raw : *pin->sedges_.set) {
      set_->insert(raw);
    }
  } else {
    set_ = acquire_set();
    own_ = true;
    set_->clear();
    populate_set(pin, *set_, pid);
  }
}

Pin::EdgeRange::EdgeRange(EdgeRange&& o) noexcept : pin_(o.pin_), /* pid_(o.pid_),*/ set_(o.set_), own_(o.own_) {
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
  constexpr uint64_t SLOT_MASK  = (1ULL << 14) - 1;  // grab 14 bits
  constexpr uint64_t SIGN_BIT   = 1ULL << 13;
  constexpr uint64_t DRIVER_BIT = 1ULL << 1;
  constexpr uint64_t PIN_BIT    = 1ULL << 0;
  constexpr uint64_t MAG_MASK   = (1ULL << 11) - 1;  // bits 12–2

  // our caller passed us vid_self = (numeric_id << 1) | pin_flag
  // strip off the low two bits to get the pure numeric node index:
  uint64_t self_num = static_cast<uint64_t>(pid) >> 2;

  uint64_t packed = pin->sedges_.sedges;
  for (int slot = 0; slot < 4; ++slot) {
    uint64_t raw = (packed >> (slot * 14)) & SLOT_MASK;
    if (raw == 0) {
      continue;
    }

    bool neg    = (raw & SIGN_BIT) != 0;
    bool driver = (raw & DRIVER_BIT) != 0;
    bool pin    = (raw & PIN_BIT) != 0;

    // grab only the magnitude bits
    uint64_t mag = (raw >> 2) & MAG_MASK;

    // reconstruct numeric target
    int64_t  delta      = neg ? -static_cast<int64_t>(mag) : static_cast<int64_t>(mag);
    uint64_t target_num = self_num - delta;
    // std::cout << "raw= " << raw << ", mag= " << mag << ", delta= " << delta
    //           << ", target_num= " << target_num << ", self_num= " << self_num << "\n";

    // repack into a Vid: shift left 2, OR back the driver+pin bits
    Vid v = static_cast<Vid>((target_num << 2) | (driver ? DRIVER_BIT : 0) | (pin ? PIN_BIT : 0));
    set.insert(v);
  }

  // also remember any “overflow” residues
  if (pin->ledge0) {
    set.insert(pin->ledge0);
  }
  if (pin->ledge1) {
    set.insert(pin->ledge1);
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

void Node::clear_node() {
  bzero(this, sizeof(Node));
}

void Node::set_type(Type t) { type = t; }
Nid  Node::get_nid() const { return nid; }
Type Node::get_type() const { return type; }
Pid  Node::get_next_pin_id() const { return next_pin_id; }
void Node::set_next_pin_id(Pid id) { next_pin_id = id; }

auto Node::overflow_handling(Nid self_id, Vid other_id) -> bool {
  if (use_overflow) {
    if(other_id) {
      sedges_.set->insert(other_id);
    }
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
    Vid target      = static_cast<Vid>(actual_self - diff);
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

  if(other_id) {
    hs->insert(other_id);
  }
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
  e |= (mag << 2);
  if (other_id & 2) {
    e = e | 2;
  }
  if (other_id & 1) {
    e = e | 1;
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
  // if we reach here, insert into ledge0 or ledge1, or overflow
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
    return sedges_.set != nullptr && !sedges_.set->empty();
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

void Node::set_subnode(Gid gid) {
  // Existence inside GraphLibrary must be checked by the caller (Node has no library pointer).
  if (gid == Gid_invalid) {
    return;
  }
  // Ensure overflow mode so ledge0 is no longer used as an edge spill slot.
  if (!use_overflow) {
    const Vid self_vid = (static_cast<Vid>(nid) << 2);  // nid stored as numeric id
    (void)overflow_handling(self_vid, 0);               // 0 => promote only, no edge insert
  } else {
    assert(sedges_.set != nullptr);
  }

  // Store gid in ledge0 as (gid + 1) so ledge0==0 means "no subnode".
  const uint64_t g = static_cast<uint64_t>(gid);
  assert(g != 0);
  assert(g < (1ULL << Nid_bits));                       // since ledge0 is Nid_bits wide
  ledge0 = static_cast<Nid>(g);
}

Gid Node::get_subnode() const noexcept {
  if (!use_overflow || ledge0 == 0) {
    return Gid_invalid;
  }
  return static_cast<Gid>(static_cast<uint64_t>(ledge0));
}

bool Node::has_subnode() const noexcept {
  return use_overflow && ledge0 != 0;
}

Node::EdgeRange::EdgeRange(const Node* node, Nid nid) noexcept : node_(node), /* nid_(nid), */ set_(nullptr), own_(false) {
  if (node->use_overflow) {
    set_ = acquire_set();
    set_->clear();
    for (auto raw : *node->sedges_.set) {
      set_->insert(raw);
    }
  } else {
    set_ = acquire_set();
    own_ = true;
    set_->clear();
    populate_set(node, *set_, nid);
  }
}

Node::EdgeRange::EdgeRange(EdgeRange&& o) noexcept : node_(o.node_), /* nid_(o.nid_), */ set_(o.set_), own_(o.own_) {
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
  constexpr uint64_t SLOT_MASK  = (1ULL << 14) - 1;  // grab 14 bits
  constexpr uint64_t SIGN_BIT   = 1ULL << 13;
  constexpr uint64_t DRIVER_BIT = 1ULL << 1;
  constexpr uint64_t PIN_BIT    = 1ULL << 0;
  constexpr uint64_t MAG_MASK   = (1ULL << 11) - 1;  // bits 12–2

  // our caller passed us vid_self = (numeric_id << 1) | pin_flag
  // strip off the low two bits to get the pure numeric node index:
  uint64_t self_num = static_cast<uint64_t>(nid) >> 2;

  uint64_t packed = node->sedges_.sedges;
  for (int slot = 0; slot < 4; ++slot) {
    uint64_t raw = (packed >> (slot * 14)) & SLOT_MASK;
    if (raw == 0) {
      continue;
    }

    bool neg    = (raw & SIGN_BIT) != 0;
    bool driver = (raw & DRIVER_BIT) != 0;
    bool pin    = (raw & PIN_BIT) != 0;

    // grab only the magnitude bits
    uint64_t mag = (raw >> 2) & MAG_MASK;

    // reconstruct numeric target
    int64_t  delta      = neg ? -static_cast<int64_t>(mag) : static_cast<int64_t>(mag);
    uint64_t target_num = self_num - delta;

    // repack into a Vid: shift left 2, OR back the driver+pin bits
    Vid v = static_cast<Vid>((target_num << 2) | (driver ? DRIVER_BIT : 0) | (pin ? PIN_BIT : 0));
    set.insert(v);
  }

  // also remember any “overflow” residues
  if (node->ledge0) {
    set.insert(node->ledge0);
  }
  if (node->ledge1) {
    set.insert(node->ledge1);
  }
}

auto Node::get_edges(Nid nid) const noexcept -> EdgeRange { return EdgeRange(this, nid); }

Graph::Graph() { clear_graph(); }

void Graph::clear_graph() {
  bzero(this, sizeof(Graph));
  node_table.clear();
  pin_table.clear();
  node_table.emplace_back(0);  // Invalid ID
  node_table.emplace_back(1);  // Input node (can have many pins to node 1)
  node_table.emplace_back(2);  // Output node
  node_table.emplace_back(3);  // Constant (common value/issue to handle for toposort) - Each const value is a pin in node3
  pin_table.emplace_back(0, 0);
}

auto Graph::create_node() -> Nid {
  Nid id = node_table.size();
  assert(id);
  node_table.emplace_back(id);
  return id << 2 | 0;
}

auto Graph::create_pin(Nid nid, Port_id pid) -> Pid {
  Pid id = pin_table.size();
  assert(id);
  pin_table.emplace_back(nid, pid);
  set_next_pin(nid, id);
  return id << 2 | 1;
}

void Graph::add_edge(Vid driver_id, Vid sink_id) {
  driver_id = driver_id | 2;
  sink_id   = sink_id & ~2;
  add_edge_int(driver_id, sink_id);
  add_edge_int(sink_id, driver_id);
}

void Graph::delete_node(Nid nid) {
  auto* node = ref_node(nid);
  if (!node) {
    return;
  }

  // Get all edges from this node
  auto edges = node->get_edges(nid);
  std::vector<Vid> edges_to_remove;
  for (auto edge : edges) {
    edges_to_remove.push_back(edge);
  }

  // For each connected node/pin, we need to remove the reverse edge
  for (auto other_vid : edges_to_remove) {
    bool is_pin = other_vid & 1;
    if (is_pin) {
      auto* other_pin = ref_pin(other_vid);
      if (other_pin && other_pin->has_edges()) {
        // Need to remove nid from other_pin's edge list
        if (other_pin->use_overflow) {
          other_pin->sedges_.set->erase(nid);
          other_pin->sedges_.set->erase(nid | 2);
        }
      }
    } else {
      auto* other_node = ref_node(other_vid);
      if (other_node && other_node->has_edges()) {
        // Need to remove nid from other_node's edge list
        if (other_node->use_overflow) {
          other_node->sedges_.set->erase(nid);
          other_node->sedges_.set->erase(nid | 2);
        }
      }
    }
  }

  // Also remove edges from pins of this node
  Pid cur_pin = node->get_next_pin_id();
  while (cur_pin != 0) {
    Pid actual_pin_id = cur_pin;
    auto* pin = &pin_table[cur_pin];

    // Get all edges from this pin
    Pid pin_vid = (actual_pin_id << 2) | 1;
    auto pin_edges = pin->get_edges(pin_vid);
    std::vector<Vid> pin_edges_to_remove;
    for (auto edge : pin_edges) {
      pin_edges_to_remove.push_back(edge);
    }

    // Remove reverse edges
    for (auto other_vid : pin_edges_to_remove) {
      bool is_pin = other_vid & 1;
      if (is_pin) {
        auto* other_pin = ref_pin(other_vid);
        if (other_pin && other_pin->has_edges()) {
          if (other_pin->use_overflow) {
            other_pin->sedges_.set->erase(pin_vid);
            other_pin->sedges_.set->erase(pin_vid | 2);
          }
        }
      } else {
        auto* other_node = ref_node(other_vid);
        if (other_node && other_node->has_edges()) {
          if (other_node->use_overflow) {
            other_node->sedges_.set->erase(pin_vid);
            other_node->sedges_.set->erase(pin_vid | 2);
          }
        }
      }
    }

    // Clear this pin's edges
    if (pin->use_overflow && pin->sedges_.set) {
      pin->sedges_.set->clear();
    } else {
      pin->sedges_.sedges = 0;
      pin->ledge0 = 0;
      pin->ledge1 = 0;
    }

    cur_pin = pin->get_next_pin_id();
  }

  // Clear the node's edges
  if (node->use_overflow && node->sedges_.set) {
    node->sedges_.set->clear();
  } else {
    node->sedges_.sedges = 0;
    node->ledge0 = 0;
    node->ledge1 = 0;
  }
}

auto Graph::ref_node(Nid id) const -> Node* {
  Nid actual_id = id >> 2;
  assert(actual_id < node_table.size());
  return (Node*)&node_table[actual_id];
}

auto Graph::ref_pin(Pid id) const -> Pin* {
  Pid actual_id = id >> 2;
  assert(actual_id < pin_table.size());
  return (Pin*)&pin_table[actual_id];
}

void Graph::add_edge_int(Vid self_id, Vid other_id) {
  // detect type of self_id and other_id
  bool self_type = false;
  if (self_id & 1) {
    // self is pin
    self_type = true;
  }
  if (!self_type) {
    auto* node = ref_node(self_id);
    if (!node->add_edge(self_id, other_id)) {
      std::cerr << "Error: Node " << node->get_nid() << " overflowed edges while adding edge from " << self_id << " to " << other_id
                << "\n";
    }
  } else {
    auto* pin = ref_pin(self_id);
    if (!pin->add_edge(self_id, other_id)) {
      std::cerr << "Error: Pin " << pin->get_master_nid() << ":" << pin->get_port_id()
                << " overflowed edges while adding edge from " << self_id << " to " << other_id << "\n";
    }
  }
}

void Graph::set_next_pin(Nid nid, Pid next_pin) {
  Nid  actual_nid = nid >> 2;
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
