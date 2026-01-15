#include "graph.hpp"
#include <strings.h>
#include <ctime>
#include <iostream>
#include <vector>

// TODO:
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
// 6-Add the graph_id class
// 7-Allow edges between graphs add_edge(pin>0, pin<0) or add_edge(pin<0, pin>0)

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
  // Remote edges (negative Vids) must not be packed.
  // Store full Vid in overflow.
  if (vid_is_negative(other_id)) {
    return overflow_handling(self_id, other_id);
  }

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

  // caller passed vid_self = (numeric_id << 2) | pin_flag
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

    // repack into a Vid: shift left 2, OR back the driver+pin bits
    Vid v = static_cast<Vid>((target_num << 2) | (driver ? DRIVER_BIT : 0) | (pin ? PIN_BIT : 0));
    set.insert(v);
  }

  // also remember any overflow residues
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
  if (vid_is_negative(other_id)) {
    return overflow_handling(self_id, other_id);
  }

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

  // caller passed vid_self = (numeric_id << 2) | pin_flag
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

Graph::Graph() { 
  clear_graph();
  (void)Graph_Library::instance().register_graph(this);
}

Graph::~Graph() {
  Graph_Library::instance().unregister_graph(this);
}

void Graph::clear_graph() {
  node_table.clear();
  pin_table.clear();

  node_table.reserve(NUM_NODES);
  pin_table.reserve(NUM_NODES * MAX_PINS_PER_NODE);

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

  const bool driver_remote = vid_is_negative(driver_id);
  const bool sink_remote   = vid_is_negative(sink_id);

  // Intra-graph: preserve old symmetric behavior.
  if (!driver_remote && !sink_remote) {
    add_edge_int(driver_id, sink_id);
    add_edge_int(sink_id, driver_id);
    return;
  }

  // Inter-graph: exactly one endpoint must be remote (negative Vid).
  if (driver_remote && sink_remote) {
    std::cerr << "Error: add_edge inter-graph cannot have both endpoints remote\n";
    return;
  }

  auto&     lib      = Graph_Library::instance();
  const Gid self_gid = lib.get_graph_id(this);
  if (self_gid == Gid_invalid) {
    std::cerr << "Error: current Graph not registered in Graph_Library\n";
    return;
  }

  if (sink_remote) {
    // Local driver (this graph) -> remote sink (other graph)
    const Gid other_gid = vid_get_gid(sink_id);
    Vid       sink_local = vid_get_local(sink_id);
    sink_local = sink_local & ~2ULL;  // ensure sink

    auto* other_graph = lib.ref_graph(other_gid);
    if (!other_graph) {
      std::cerr << "Error: unknown other graph gid=" << other_gid << "\n";
      return;
    }

    // Store local half-edge driver -> remote sink.
    add_edge_int(driver_id, sink_id);

    // Store reciprocal in the other graph: sink -> remote driver.
    const Vid driver_remote_id = vid_make_remote(self_gid, driver_id);
    other_graph->add_edge_int(sink_local, driver_remote_id);
    return;
  }

  // driver_remote == true
  {
    // Local sink (this graph) <- remote driver (other graph)
    const Gid other_gid = vid_get_gid(driver_id);
    Vid       driver_local = vid_get_local(driver_id);
    driver_local = driver_local | 2ULL;  // ensure driver

    auto* other_graph = lib.ref_graph(other_gid);
    if (!other_graph) {
      std::cerr << "Error: unknown other graph gid=" << other_gid << "\n";
      return;
    }

    // Store local half-edge sink -> remote driver.
    add_edge_int(sink_id, driver_id);

    // Store reciprocal in the other graph: driver -> remote sink.
    const Vid sink_remote_id = vid_make_remote(self_gid, sink_id);
    other_graph->add_edge_int(driver_local, sink_remote_id);
    return;
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

auto Graph_Library::instance() noexcept -> Graph_Library& {
  static Graph_Library lib;
  return lib;
}

auto Graph_Library::register_graph(Graph* g) noexcept -> Gid {
  if (g == nullptr) {
    return Gid_invalid;
  }
  if (g->gid_ != Gid_invalid) return g->gid_;         // already registered
  if (graphs_.empty()) {
    graphs_.push_back(nullptr);                       // gid 0 reserved
  }
  const Gid gid = static_cast<Gid>(graphs_.size());
  graphs_.push_back(g);
  g->gid_ = gid;
  return gid;
}

void Graph_Library::unregister_graph(Graph* g) noexcept {
  if (g == nullptr) {
    return;
  }
  Gid gid = g->gid_;
  if (gid == Gid_invalid) return;

  if (gid < graphs_.size() && graphs_[gid] == g) {
    graphs_[gid] = nullptr;
    g->gid_ = Gid_invalid;
  }
}

auto Graph_Library::ref_graph(Gid id) const noexcept -> Graph* {
  if (id == 0 || id == Gid_invalid) {
    return nullptr;
  }
  if (id >= graphs_.size()) {
    return nullptr;
  }
  return graphs_[id];
}

auto Graph_Library::get_graph_id(const Graph* g) const noexcept -> Gid {
  return g ? g->gid_ : Gid_invalid;
}

}  // namespace hhds
