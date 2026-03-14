#include "graph.hpp"

#include <strings.h>

#include <algorithm>
#include <ctime>
#include <functional>
#include <iostream>
#include <queue>
#include <vector>

#include "tree.hpp"

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
// 7_Add inter-graph connections (set_subnode)

namespace hhds {

Node_hier::Node_hier(Tid hier_tid_value, std::shared_ptr<std::vector<Gid>> hier_gids_value, Tree_pos hier_pos_value, Nid raw_nid_value)
    : hier_gids(std::move(hier_gids_value)), hier_tid(hier_tid_value), hier_pos(hier_pos_value), raw_nid(raw_nid_value) {}

auto Node_hier::get_root_gid() const noexcept -> Gid {
  if (!hier_gids || ROOT >= static_cast<Tree_pos>(hier_gids->size())) {
    return Gid_invalid;
  }
  return (*hier_gids)[ROOT];
}

auto Node_hier::get_current_gid() const noexcept -> Gid {
  if (!hier_gids || hier_pos >= static_cast<Tree_pos>(hier_gids->size())) {
    return Gid_invalid;
  }
  return (*hier_gids)[static_cast<size_t>(hier_pos)];
}

auto Pin_hier::get_root_gid() const noexcept -> Gid {
  if (!hier_gids || ROOT >= static_cast<Tree_pos>(hier_gids->size())) {
    return Gid_invalid;
  }
  return (*hier_gids)[ROOT];
}

auto Pin_hier::get_current_gid() const noexcept -> Gid {
  if (!hier_gids || hier_pos >= static_cast<Tree_pos>(hier_gids->size())) {
    return Gid_invalid;
  }
  return (*hier_gids)[static_cast<size_t>(hier_pos)];
}

auto to_class(const Node_hier& v) -> Node_class { return Node_class(nullptr, v.get_raw_nid() & ~static_cast<Nid>(2)); }

auto to_flat(const Node_hier& v) -> Node_flat {
  return Node_flat(v.get_root_gid(), v.get_current_gid(), v.get_raw_nid() & ~static_cast<Nid>(2));
}

auto to_class(const Node_flat& v) -> Node_class { return Node_class(nullptr, v.get_raw_nid() & ~static_cast<Nid>(2)); }

auto to_flat(const Node_class& v, Gid current_gid, Gid root_gid) -> Node_flat {
  if (root_gid == Gid_invalid) {
    root_gid = current_gid;
  }
  return Node_flat(root_gid, current_gid, v.get_raw_nid() & ~static_cast<Nid>(2));
}

auto to_class(const Pin_hier& v) -> Pin_class {
  return Pin_class(v.get_raw_nid() & ~static_cast<Nid>(2), v.get_port_id(), v.get_pin_pid());
}

auto to_flat(const Pin_hier& v) -> Pin_flat {
  return Pin_flat(v.get_root_gid(), v.get_current_gid(), v.get_raw_nid() & ~static_cast<Nid>(2), v.get_port_id(), v.get_pin_pid());
}

auto to_class(const Pin_flat& v) -> Pin_class {
  return Pin_class(v.get_raw_nid() & ~static_cast<Nid>(2), v.get_port_id(), v.get_pin_pid());
}

auto to_flat(const Pin_class& v, Gid current_gid, Gid root_gid) -> Pin_flat {
  if (root_gid == Gid_invalid) {
    root_gid = current_gid;
  }
  return Pin_flat(root_gid, current_gid, v.get_raw_nid() & ~static_cast<Nid>(2), v.get_port_id(), v.get_pin_pid());
}

auto to_flat(const Edge_class& e, Gid current_gid, Gid root_gid) -> Edge_flat {
  if (root_gid == Gid_invalid) {
    root_gid = current_gid;
  }
  Edge_flat out;
  out.driver = to_flat(e.driver_pin, current_gid, root_gid);
  out.sink   = to_flat(e.sink_pin, current_gid, root_gid);
  return out;
}

auto to_flat(const Edge_hier& e) -> Edge_flat {
  Edge_flat out;
  out.driver = to_flat(e.driver);
  out.sink   = to_flat(e.sink);
  return out;
}

auto to_hier(const Edge_class& e, Tid hier_tid, std::shared_ptr<std::vector<Gid>> hier_gids, Tree_pos hier_pos) -> Edge_hier {
  Edge_hier out;
  out.driver = Pin_hier(hier_tid, hier_gids, hier_pos, e.driver_pin.get_raw_nid(), e.driver_pin.get_port_id(),
                        e.driver_pin.get_pin_pid());
  out.sink = Pin_hier(hier_tid, std::move(hier_gids), hier_pos, e.sink_pin.get_raw_nid(), e.sink_pin.get_port_id(),
                      e.sink_pin.get_pin_pid());
  return out;
}

auto to_class(const Edge_flat& e) -> Edge_class {
  Edge_class out{};
  out.driver_pin = to_class(e.driver);
  out.sink_pin   = to_class(e.sink);
  out.driver     = Node_class(nullptr, out.driver_pin.get_raw_nid() | static_cast<Nid>(2));
  out.sink       = Node_class(nullptr, out.sink_pin.get_raw_nid() & ~static_cast<Nid>(2));
  out.type       = 2;  // p -> p
  return out;
}

auto to_class(const Edge_hier& e) -> Edge_class {
  Edge_class out{};
  out.driver_pin = to_class(e.driver);
  out.sink_pin   = to_class(e.sink);
  out.driver     = Node_class(nullptr, out.driver_pin.get_raw_nid() | static_cast<Nid>(2));
  out.sink       = Node_class(nullptr, out.sink_pin.get_raw_nid() & ~static_cast<Nid>(2));
  out.type       = 2;  // p -> p
  return out;
}

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

auto Pin::delete_edge(Pid self_id, Vid other_id) -> bool {
  auto* pin = this;
  if (!pin) {
    return false;
  }

  // get all edges from the pin
  auto edges = pin->get_edges(self_id);

  for (auto edge : edges) {
    bool is_pin    = edge & 1;
    bool is_driver = edge & 2;
    if (edge >> 2 == other_id >> 2) {
      if (pin->use_overflow) {
        return pin->sedges_.set->erase(edge) != 0;
      } else {
        // delete from packed edges
        constexpr int      SHIFT = 14;
        constexpr uint64_t SLOT  = (1ULL << SHIFT) - 1;
        for (int i = 0; i < 4; ++i) {
          uint64_t e = edge & (SLOT << (i * SHIFT));
          if (e >> 2 == (other_id >> 2)) {
            pin->sedges_.sedges &= ~(static_cast<int64_t>(SLOT) << (i * SHIFT));
            return true;
          }
        }
        // delete from ledges
        if (pin->ledge0 >> 2 == other_id >> 2) {
          pin->ledge0 = 0;
          return true;
        }
        if (pin->ledge1 >> 2 == other_id >> 2) {
          pin->ledge1 = 0;
          return true;
        }
        // edge not found
        return false;
      }
    }
  }
  return false;
}

Pin::EdgeRange::EdgeRange(const Pin* pin, Pid pid) noexcept : pin_(pin), set_(nullptr), own_(false) {
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

Pin::EdgeRange::EdgeRange(EdgeRange&& o) noexcept : pin_(o.pin_), set_(o.set_), own_(o.own_) {
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

void Node::clear_node() { bzero(this, sizeof(Node)); }

void Node::set_type(Type t) { type = t; }
Nid  Node::get_nid() const { return nid; }
Type Node::get_type() const { return type; }
Pid  Node::get_next_pin_id() const { return next_pin_id; }
void Node::set_next_pin_id(Pid id) { next_pin_id = id; }

auto Node::overflow_handling(Nid self_id, Vid other_id) -> bool {
  if (use_overflow) {
    if (other_id) {
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

  if (other_id) {
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

auto Node::delete_edge(Nid self_id, Vid other_id) -> bool {
  auto* node = this;
  if (!node) {
    return false;
  }

  // get all edges from the pin
  auto edges = node->get_edges(self_id);

  for (auto edge : edges) {
    bool is_pin    = edge & 1;
    bool is_driver = edge & 2;
    if (edge >> 2 == other_id >> 2) {
      if (node->use_overflow) {
        return node->sedges_.set->erase(edge) != 0;
      } else {
        // delete from packed edges
        constexpr int      SHIFT = 14;
        constexpr uint64_t SLOT  = (1ULL << SHIFT) - 1;
        for (int i = 0; i < 4; ++i) {
          uint64_t e = edge & (SLOT << (i * SHIFT));
          if (e >> 2 == (other_id >> 2)) {
            node->sedges_.sedges &= ~(static_cast<int64_t>(SLOT) << (i * SHIFT));
            return true;
          }
        }
        // delete from ledges
        if (node->ledge0 >> 2 == other_id >> 2) {
          node->ledge0 = 0;
          return true;
        }
        if (node->ledge1 >> 2 == other_id >> 2) {
          node->ledge1 = 0;
          return true;
        }
        // edge not found
        return false;
      }
    }
  }
  return false;
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
  assert(g < (1ULL << Nid_bits));  // since ledge0 is Nid_bits wide
  ledge0 = static_cast<Nid>(g);
}

Gid Node::get_subnode() const noexcept {
  if (!use_overflow || ledge0 == 0) {
    return Gid_invalid;
  }
  return static_cast<Gid>(static_cast<uint64_t>(ledge0));
}

bool Node::has_subnode() const noexcept { return use_overflow && ledge0 != 0; }

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

void Graph::assert_accessible() const noexcept { assert(!deleted_ && "graph is no longer valid"); }

void Node_class::assert_accessible_handle() const noexcept {
  if (graph != nullptr) {
    graph->assert_accessible();
  }
}

void Pin_class::assert_accessible_handle() const noexcept {
  if (graph != nullptr) {
    graph->assert_accessible();
  }
}

void Graph::assert_compatible(const Node_class& node) const noexcept {
  assert_accessible();
  if (node.graph != nullptr) {
    node.graph->assert_accessible();
    assert(node.graph == this && "node handle belongs to a different graph");
  }
}

void Graph::assert_compatible(const Pin_class& pin) const noexcept {
  assert_accessible();
  if (pin.graph != nullptr) {
    pin.graph->assert_accessible();
    assert(pin.graph == this && "pin handle belongs to a different graph");
  }
}

void Graph::invalidate_from_library() noexcept {
  if (deleted_) {
    return;
  }
  deleted_   = true;
  owner_lib_ = nullptr;
  node_table.clear();
  pin_table.clear();
  fast_class_cache_.clear();
  fast_flat_cache_.clear();
  fast_hier_cache_.clear();
  forward_class_cache_.clear();
  forward_flat_cache_.clear();
  forward_hier_cache_.clear();
  fast_class_cache_valid_    = false;
  fast_hier_cache_valid_     = false;
  forward_class_cache_valid_ = false;
  forward_flat_cache_valid_  = false;
  forward_hier_cache_valid_  = false;
  fast_hier_tree_cache_.reset();
  forward_hier_tree_cache_.reset();
}

void Graph::clear_graph() {
  assert_accessible();
  node_table.clear();
  pin_table.clear();
  node_table.emplace_back(0);  // Invalid ID
  node_table.emplace_back(1);  // Input node (can have many pins to node 1)
  node_table.emplace_back(2);  // Output node
  node_table.emplace_back(3);  // Constant (common value/issue to handle for toposort) - Each const value is a pin in node3
  pin_table.emplace_back(0, 0);
  invalidate_traversal_caches();
}

void Graph::bind_library(const GraphLibrary* owner, Gid self_gid) noexcept {
  owner_lib_ = owner;
  self_gid_  = self_gid;
  deleted_   = false;
}

void Graph::invalidate_traversal_caches() noexcept {
  fast_class_cache_valid_    = false;
  fast_hier_cache_valid_     = false;
  forward_class_cache_valid_ = false;
  forward_flat_cache_valid_  = false;
  forward_hier_cache_valid_  = false;
  fast_hier_tree_cache_.reset();
  fast_hier_gid_cache_.reset();
  forward_hier_tree_cache_.reset();
  forward_hier_gid_cache_.reset();
  if (owner_lib_ != nullptr) {
    owner_lib_->note_graph_mutation();
  }
}

void Graph::rebuild_fast_class_cache() const {
  fast_class_cache_.clear();
  if (node_table.size() > 1) {
    fast_class_cache_.reserve(node_table.size() - 1);
    for (size_t i = 1; i < node_table.size(); ++i) {
      const Nid raw_nid = static_cast<Nid>(i) << 2;
      fast_class_cache_.emplace_back(const_cast<Graph*>(this), raw_nid);
    }
  }
  fast_class_cache_valid_ = true;
}

void Graph::rebuild_fast_flat_cache() const {
  fast_flat_cache_.clear();
  const auto traversal = fast_iter(true, 0, 0);
  fast_flat_cache_.reserve(traversal.size());
  for (const auto& it : traversal) {
    fast_flat_cache_.emplace_back(it.get_top_graph(), it.get_curr_graph(), it.get_node_id());
  }
}

void Graph::rebuild_fast_hier_cache() const {
  fast_hier_cache_.clear();
  fast_hier_tree_cache_ = std::make_shared<Tree>();
  fast_hier_gid_cache_  = std::make_shared<std::vector<Gid>>();
  const Tid hier_tid    = self_gid_ != Gid_invalid ? static_cast<Tid>(self_gid_) : static_cast<Tid>(-1);

  Gid root_gid = self_gid_;
  if (root_gid == Gid_invalid) {
    root_gid = 0;
  }
  const Tree_pos root_pos = fast_hier_tree_cache_->add_root();
  fast_hier_gid_cache_->resize(static_cast<size_t>(root_pos + 1), Gid_invalid);
  (*fast_hier_gid_cache_)[static_cast<size_t>(root_pos)] = root_gid;

  ankerl::unordered_dense::set<Gid> active_graphs;
  if (self_gid_ != Gid_invalid) {
    active_graphs.insert(self_gid_);
  }
  fast_hier_impl(fast_hier_tree_cache_, hier_tid, fast_hier_gid_cache_, root_pos, active_graphs, fast_hier_cache_);

  fast_hier_cache_epoch_ = owner_lib_ != nullptr ? owner_lib_->mutation_epoch() : 0;
  fast_hier_cache_valid_ = true;
}

void Graph::rebuild_forward_class_cache() const {
  constexpr size_t first_user_node_idx = 4;  // 0:invalid, 1:INPUT, 2:OUTPUT, 3:CONST
  forward_class_cache_.clear();
  if (node_table.size() <= first_user_node_idx) {
    forward_class_cache_valid_ = true;
    return;
  }

  const size_t                                      node_count = node_table.size();
  std::vector<ankerl::unordered_dense::set<size_t>> adjacency(node_count);
  std::vector<uint32_t>                             indegree(node_count, 0);

  auto add_dependency = [&](size_t driver_idx, Vid sink_vid) {
    Nid sink_nid = 0;
    if (sink_vid & static_cast<Vid>(1)) {
      const Pid sink_pid = (static_cast<Pid>(sink_vid) & ~static_cast<Pid>(2)) | static_cast<Pid>(1);
      sink_nid           = ref_pin(sink_pid)->get_master_nid();
    } else {
      sink_nid = static_cast<Nid>(sink_vid);
    }

    sink_nid &= ~static_cast<Nid>(3);
    const size_t sink_idx = static_cast<size_t>(sink_nid >> 2);
    if (sink_idx < first_user_node_idx || sink_idx >= node_count) {
      return;
    }

    if (adjacency[driver_idx].insert(sink_idx).second) {
      ++indegree[sink_idx];
    }
  };

  for (size_t driver_idx = first_user_node_idx; driver_idx < node_count; ++driver_idx) {
    const Nid driver_nid = static_cast<Nid>(driver_idx) << 2;

    auto node_edges = node_table[driver_idx].get_edges(driver_nid);
    for (auto vid : node_edges) {
      if (vid & static_cast<Vid>(2)) {
        continue;
      }
      add_dependency(driver_idx, vid);
    }

    for (Pid pin_vid = node_table[driver_idx].get_next_pin_id(); pin_vid != 0;) {
      const Pid canonical_pin = (pin_vid & ~static_cast<Pid>(2)) | static_cast<Pid>(1);
      auto      pin_edges     = ref_pin(canonical_pin)->get_edges(canonical_pin);
      for (auto vid : pin_edges) {
        if (vid & static_cast<Vid>(2)) {
          continue;
        }
        add_dependency(driver_idx, vid);
      }
      pin_vid = ref_pin(canonical_pin)->get_next_pin_id();
    }
  }

  std::priority_queue<size_t, std::vector<size_t>, std::greater<size_t>> ready;
  for (size_t idx = first_user_node_idx; idx < node_count; ++idx) {
    if (indegree[idx] == 0) {
      ready.push(idx);
    }
  }

  std::vector<bool> emitted(node_count, false);
  forward_class_cache_.reserve(node_count - first_user_node_idx);
  while (!ready.empty()) {
    const size_t node_idx = ready.top();
    ready.pop();
    if (emitted[node_idx]) {
      continue;
    }

    emitted[node_idx] = true;
    forward_class_cache_.emplace_back(const_cast<Graph*>(this), static_cast<Nid>(node_idx) << 2);

    for (auto sink_idx : adjacency[node_idx]) {
      if (indegree[sink_idx] == 0) {
        continue;
      }
      --indegree[sink_idx];
      if (indegree[sink_idx] == 0) {
        ready.push(sink_idx);
      }
    }
  }

  // Preserve determinism under cycles by appending unresolved nodes by ID.
  for (size_t idx = first_user_node_idx; idx < node_count; ++idx) {
    if (!emitted[idx]) {
      forward_class_cache_.emplace_back(const_cast<Graph*>(this), static_cast<Nid>(idx) << 2);
    }
  }

  forward_class_cache_valid_ = true;
}

void Graph::forward_flat_impl(Gid top_graph, ankerl::unordered_dense::set<Gid>& active_graphs, std::vector<Node_flat>& out) const {
  for (const auto& node : forward_class()) {
    const Nid   node_nid = node.get_raw_nid();
    const auto& node_ref = node_table[static_cast<size_t>(node_nid >> 2)];

    if (node_ref.has_subnode() && owner_lib_ != nullptr) {
      const Gid other_graph_id = node_ref.get_subnode();
      if (owner_lib_->has_graph(other_graph_id) && active_graphs.find(other_graph_id) == active_graphs.end()) {
        active_graphs.insert(other_graph_id);
        owner_lib_->get_graph(other_graph_id)->forward_flat_impl(top_graph, active_graphs, out);
        active_graphs.erase(other_graph_id);
        continue;
      }
    }

    out.emplace_back(top_graph, self_gid_, node_nid);
  }
}

void Graph::fast_hier_impl(std::shared_ptr<Tree> hier_tree, Tid hier_tid, std::shared_ptr<std::vector<Gid>> hier_gids, Tree_pos hier_pos,
                           ankerl::unordered_dense::set<Gid>& active_graphs, std::vector<Node_hier>& out) const {
  for (size_t i = 1; i < node_table.size(); ++i) {
    const Nid   node_id = static_cast<Nid>(i) << 2;
    const auto& node    = node_table[i];

    if (node.has_subnode() && owner_lib_ != nullptr) {
      const Gid other_graph_id = node.get_subnode();
      if (owner_lib_->has_graph(other_graph_id) && active_graphs.find(other_graph_id) == active_graphs.end()) {
        const Tree_pos child_hier_pos = hier_tree->add_child(hier_pos);
        if (static_cast<size_t>(child_hier_pos) >= hier_gids->size()) {
          hier_gids->resize(static_cast<size_t>(child_hier_pos + 1), Gid_invalid);
        }
        (*hier_gids)[static_cast<size_t>(child_hier_pos)] = other_graph_id;
        active_graphs.insert(other_graph_id);
        owner_lib_->get_graph(other_graph_id)->fast_hier_impl(hier_tree, hier_tid, hier_gids, child_hier_pos, active_graphs, out);
        active_graphs.erase(other_graph_id);
        continue;
      }
    }

    out.emplace_back(hier_tid, hier_gids, hier_pos, node_id);
  }
}

void Graph::forward_hier_impl(std::shared_ptr<Tree> hier_tree, Tid hier_tid, std::shared_ptr<std::vector<Gid>> hier_gids, Tree_pos hier_pos,
                              ankerl::unordered_dense::set<Gid>& active_graphs, std::vector<Node_hier>& out) const {
  for (const auto& node : forward_class()) {
    const Nid   node_nid = node.get_raw_nid();
    const auto& node_ref = node_table[static_cast<size_t>(node_nid >> 2)];

    if (node_ref.has_subnode() && owner_lib_ != nullptr) {
      const Gid other_graph_id = node_ref.get_subnode();
      if (owner_lib_->has_graph(other_graph_id) && active_graphs.find(other_graph_id) == active_graphs.end()) {
        const Tree_pos child_hier_pos = hier_tree->add_child(hier_pos);
        if (static_cast<size_t>(child_hier_pos) >= hier_gids->size()) {
          hier_gids->resize(static_cast<size_t>(child_hier_pos + 1), Gid_invalid);
        }
        (*hier_gids)[static_cast<size_t>(child_hier_pos)] = other_graph_id;
        active_graphs.insert(other_graph_id);
        owner_lib_->get_graph(other_graph_id)->forward_hier_impl(hier_tree, hier_tid, hier_gids, child_hier_pos, active_graphs, out);
        active_graphs.erase(other_graph_id);
        continue;
      }
    }

    out.emplace_back(hier_tid, hier_gids, hier_pos, node_nid);
  }
}

void Graph::rebuild_forward_flat_cache() const {
  forward_flat_cache_.clear();

  const Gid                         top_graph = self_gid_;
  ankerl::unordered_dense::set<Gid> active_graphs;
  if (self_gid_ != Gid_invalid) {
    active_graphs.insert(self_gid_);
  }
  forward_flat_impl(top_graph, active_graphs, forward_flat_cache_);

  forward_flat_cache_epoch_ = owner_lib_ != nullptr ? owner_lib_->mutation_epoch() : 0;
  forward_flat_cache_valid_ = true;
}

void Graph::rebuild_forward_hier_cache() const {
  forward_hier_cache_.clear();
  forward_hier_tree_cache_ = std::make_shared<Tree>();
  forward_hier_gid_cache_  = std::make_shared<std::vector<Gid>>();
  const Tid hier_tid       = self_gid_ != Gid_invalid ? static_cast<Tid>(self_gid_) : static_cast<Tid>(-1);

  Gid root_gid = self_gid_;
  if (root_gid == Gid_invalid) {
    root_gid = 0;
  }
  const Tree_pos root_pos = forward_hier_tree_cache_->add_root();
  forward_hier_gid_cache_->resize(static_cast<size_t>(root_pos + 1), Gid_invalid);
  (*forward_hier_gid_cache_)[static_cast<size_t>(root_pos)] = root_gid;

  ankerl::unordered_dense::set<Gid> active_graphs;
  if (self_gid_ != Gid_invalid) {
    active_graphs.insert(self_gid_);
  }

  forward_hier_impl(forward_hier_tree_cache_, hier_tid, forward_hier_gid_cache_, root_pos, active_graphs, forward_hier_cache_);

  forward_hier_cache_epoch_ = owner_lib_ != nullptr ? owner_lib_->mutation_epoch() : 0;
  forward_hier_cache_valid_ = true;
}

auto Graph::fast_iter(bool hierarchy, Gid top_graph, uint32_t tree_node_num) const -> std::vector<FastIterator> {
  assert_accessible();
  if (top_graph == 0) {
    top_graph = self_gid_;
  }
  if (tree_node_num == 0) {
    tree_node_num = 1;
  }

  std::vector<FastIterator> result;
  result.reserve(node_table.size());

  ankerl::unordered_dense::set<Gid> active_graphs;
  if (self_gid_ != Gid_invalid) {
    active_graphs.insert(self_gid_);
  }

  uint32_t next_tree_node_num = tree_node_num;
  fast_iter_impl(hierarchy, top_graph, tree_node_num, next_tree_node_num, active_graphs, result);
  return result;
}

void Graph::fast_iter_impl(bool hierarchy, Gid top_graph, uint32_t tree_node_num, uint32_t& next_tree_node_num,
                           ankerl::unordered_dense::set<Gid>& active_graphs, std::vector<FastIterator>& out) const {
  const Gid curr_graph = self_gid_;
  for (size_t i = 1; i < node_table.size(); ++i) {
    const Nid   node_id = static_cast<Nid>(i) << 2;
    const auto& node    = node_table[i];

    if (hierarchy && node.has_subnode() && owner_lib_ != nullptr) {
      const Gid other_graph_id = node.get_subnode();
      if (owner_lib_->has_graph(other_graph_id) && active_graphs.find(other_graph_id) == active_graphs.end()) {
        active_graphs.insert(other_graph_id);
        const uint32_t child_tree_node_num = ++next_tree_node_num;
        owner_lib_->get_graph(other_graph_id)
            ->fast_iter_impl(hierarchy, top_graph, child_tree_node_num, next_tree_node_num, active_graphs, out);
        active_graphs.erase(other_graph_id);
        continue;
      }
    }

    out.emplace_back(node_id, top_graph, curr_graph, tree_node_num);
  }
}

auto Graph::create_node() -> Node_class {
  assert_accessible();
  Nid id = node_table.size();
  assert(id);
  node_table.emplace_back(id);
  invalidate_traversal_caches();
  Nid raw_nid = id << 2 | 0;
  return Node_class(this, raw_nid);
}

auto Graph::create_pin(Nid nid, Port_id pid) -> Pid {
  assert_accessible();
  // nid is << 2 here but port_id is not << 2 id (here but pin id in actual) is also not << 2
  nid &= ~static_cast<Nid>(2);  // Pin ownership is by node identity, independent of edge role bit.
  Pid id = pin_table.size();
  assert(id);
  pin_table.emplace_back(nid, pid);
  set_next_pin(nid, id);
  invalidate_traversal_caches();
  return id << 2 | 1;
}

auto Graph::make_pin_class(Pid pin_pid) const -> Pin_class {
  const auto* pin = ref_pin(pin_pid);
  return Pin_class(const_cast<Graph*>(this), pin->get_master_nid(), pin->get_port_id(), pin_pid);
}

auto Pin_class::get_master_node() const -> Node_class {
  assert_accessible_handle();
  if (graph != nullptr && pin_pid != 0) {
    const auto* pin = graph->ref_pin(pin_pid);
    return Node_class(graph, pin->get_master_nid());
  }
  return Node_class(graph, raw_nid);
}

auto Node_class::create_pin(Port_id port_id_value) const -> Pin_class {
  assert_accessible_handle();
  assert(graph != nullptr);
  const Pid pin_pid = graph->create_pin(raw_nid, port_id_value);
  return Pin_class(graph, raw_nid, port_id_value, pin_pid);
}

void Graph::set_subnode(Node_class node, Gid gid) {
  assert_compatible(node);
  set_subnode(node.get_raw_nid(), gid);
}

void Graph::set_subnode(Nid nid, Gid gid) {
  assert_accessible();
  if (gid == Gid_invalid) {
    return;
  }

  if (owner_lib_ != nullptr) {
    assert(owner_lib_->has_graph(gid));
    if (!owner_lib_->has_graph(gid)) {
      return;
    }
  }

  nid &= ~static_cast<Nid>(3);
  ref_node(nid)->set_subnode(gid);
  invalidate_traversal_caches();
}

void Graph::add_edge(Vid driver_id, Vid sink_id) {
  assert_accessible();
  driver_id = driver_id | 2;
  sink_id   = sink_id & ~2;
  add_edge_int(driver_id, sink_id);
  add_edge_int(sink_id, driver_id);
  invalidate_traversal_caches();
}

void Graph::del_edge(Node_class node1, Node_class node2) {
  assert_accessible();
  assert_compatible(node1);
  assert_compatible(node2);
  del_edge_int(node1.get_raw_nid(), node2.get_raw_nid());
  invalidate_traversal_caches();
}

void Graph::del_edge(Node_class node, Pin_class pin) {
  assert_accessible();
  assert_compatible(node);
  assert_compatible(pin);
  del_edge_int(node.get_raw_nid(), pin.get_pin_pid());
  invalidate_traversal_caches();
}

void Graph::del_edge(Pin_class pin, Node_class node) {
  assert_accessible();
  assert_compatible(pin);
  assert_compatible(node);
  del_edge_int(pin.get_pin_pid(), node.get_raw_nid());
  invalidate_traversal_caches();
}

void Graph::del_edge(Pin_class pin1, Pin_class pin2) {
  assert_accessible();
  assert_compatible(pin1);
  assert_compatible(pin2);
  del_edge_int(pin1.get_pin_pid(), pin2.get_pin_pid());
  invalidate_traversal_caches();
}

auto Graph::fast_class() const -> std::span<const Node_class> {
  assert_accessible();
  const size_t expected_size = node_table.size() > 0 ? node_table.size() - 1 : 0;
  if (!fast_class_cache_valid_ || fast_class_cache_.size() != expected_size) {
    rebuild_fast_class_cache();
  }
  return std::span<const Node_class>(fast_class_cache_.data(), fast_class_cache_.size());
}

auto Graph::forward_class() const -> std::span<const Node_class> {
  assert_accessible();
  constexpr size_t first_user_node_idx = 4;  // 0:invalid, 1:INPUT, 2:OUTPUT, 3:CONST
  const size_t     expected_size       = node_table.size() > first_user_node_idx ? node_table.size() - first_user_node_idx : 0;
  if (!forward_class_cache_valid_ || forward_class_cache_.size() != expected_size) {
    rebuild_forward_class_cache();
  }
  return std::span<const Node_class>(forward_class_cache_.data(), forward_class_cache_.size());
}

auto Graph::fast_flat() const -> std::span<const Node_flat> {
  assert_accessible();
  rebuild_fast_flat_cache();
  return std::span<const Node_flat>(fast_flat_cache_.data(), fast_flat_cache_.size());
}

auto Graph::fast_hier() const -> std::span<const Node_hier> {
  assert_accessible();
  const uint64_t expected_epoch = owner_lib_ != nullptr ? owner_lib_->mutation_epoch() : 0;
  if (!fast_hier_cache_valid_ || (owner_lib_ != nullptr && fast_hier_cache_epoch_ != expected_epoch)) {
    rebuild_fast_hier_cache();
  }
  return std::span<const Node_hier>(fast_hier_cache_.data(), fast_hier_cache_.size());
}

auto Graph::forward_flat() const -> std::span<const Node_flat> {
  assert_accessible();
  const uint64_t expected_epoch = owner_lib_ != nullptr ? owner_lib_->mutation_epoch() : 0;
  if (!forward_flat_cache_valid_ || (owner_lib_ != nullptr && forward_flat_cache_epoch_ != expected_epoch)) {
    rebuild_forward_flat_cache();
  }
  return std::span<const Node_flat>(forward_flat_cache_.data(), forward_flat_cache_.size());
}

auto Graph::forward_hier() const -> std::span<const Node_hier> {
  assert_accessible();
  const uint64_t expected_epoch = owner_lib_ != nullptr ? owner_lib_->mutation_epoch() : 0;
  if (!forward_hier_cache_valid_ || (owner_lib_ != nullptr && forward_hier_cache_epoch_ != expected_epoch)) {
    rebuild_forward_hier_cache();
  }
  return std::span<const Node_hier>(forward_hier_cache_.data(), forward_hier_cache_.size());
}

void Graph::del_edge_int(Vid driver_id, Vid sink_id) {
  if (driver_id & 1) {
    (void)ref_pin(driver_id)->delete_edge(driver_id, sink_id);
  } else {
    (void)ref_node(driver_id)->delete_edge(driver_id, sink_id);
  }

  if (sink_id & 1) {
    (void)ref_pin(sink_id)->delete_edge(sink_id, driver_id);
  } else {
    (void)ref_node(sink_id)->delete_edge(sink_id, driver_id);
  }
}

auto Graph::out_edges(Node_class node) -> std::vector<Edge_class> {
  assert_accessible();
  assert_compatible(node);
  std::vector<Edge_class> out;
  const Nid               self_nid = node.get_raw_nid() & ~static_cast<Nid>(2);
  auto*                   self     = ref_node(self_nid);
  auto                    edges    = self->get_edges(self_nid);
  const Node_class        self_driver(this, self_nid | static_cast<Nid>(2));

  for (auto vid : edges) {
    if (vid & 2) {
      continue;
    }
    if (vid & 1) {
      const Pid sink_pid = static_cast<Pid>(vid);

      Edge_class e{};
      e.type     = 3;  // n -> p
      e.driver   = self_driver;
      e.sink_pin = make_pin_class(sink_pid);
      // e.sink / e.driver_pin remain default
      out.push_back(e);
      continue;
    } else {
      const Nid sink_nid = static_cast<Nid>(vid);

      Edge_class e{};
      e.type   = 1;  // n -> n
      e.driver = self_driver;
      e.sink   = Node_class(this, sink_nid);  // keep your existing ctor usage
      // e.driver_pin / e.sink_pin remain default
      out.push_back(e);
    }
  }
  return out;
}

auto Graph::inp_edges(Node_class node) -> std::vector<Edge_class> {
  assert_accessible();
  assert_compatible(node);
  std::vector<Edge_class> out;
  const Nid               self_nid = node.get_raw_nid() & ~static_cast<Nid>(2);
  auto*                   self     = ref_node(self_nid);
  auto                    edges    = self->get_edges(self_nid);
  const Node_class        self_sink(this, self_nid & ~static_cast<Nid>(2));

  for (auto vid : edges) {
    if (!(vid & 2)) {
      continue;
    }
    if (vid & 1) {
      const Pid driver_pid = static_cast<Pid>(vid);

      Edge_class e{};
      e.type       = 4;  // p -> n
      e.driver_pin = make_pin_class(driver_pid);
      e.sink       = self_sink;
      // e.driver / e.sink_pin remain default
      out.push_back(e);
      continue;
    } else {
      const Nid driver_nid = static_cast<Nid>(vid);

      Edge_class e{};
      e.type   = 1;  // n -> n
      e.driver = Node_class(this, driver_nid);
      e.sink   = self_sink;
      // e.driver_pin / e.sink_pin remain default
      out.push_back(e);
    }
  }
  return out;
}

auto Graph::out_edges(Pin_class pin) -> std::vector<Edge_class> {
  assert_accessible();
  assert_compatible(pin);
  std::vector<Edge_class> out;
  const Pid               self_pid        = pin.get_pin_pid();
  const Pid               self_pid_lookup = (self_pid & ~static_cast<Pid>(2)) | static_cast<Pid>(1);
  auto*                   self            = ref_pin(self_pid_lookup);
  auto                    edges           = self->get_edges(self_pid_lookup);
  const Pin_class         self_driver_pin = make_pin_class(self_pid_lookup | static_cast<Pid>(2));

  for (auto vid : edges) {
    if (vid & 2) {
      continue;
    }
    if (vid & 1) {
      const Pid sink_pid = static_cast<Pid>(vid);

      Edge_class e{};
      e.type       = 2;  // p -> p
      e.driver_pin = self_driver_pin;
      e.sink_pin   = make_pin_class(sink_pid);
      out.push_back(e);
      continue;
    }

    const Nid sink_nid = static_cast<Nid>(vid);

    Edge_class e{};
    e.type       = 4;  // p -> n
    e.driver_pin = self_driver_pin;
    e.sink       = Node_class(this, sink_nid);
    out.push_back(e);
  }

  return out;
}

auto Graph::inp_edges(Pin_class pin) -> std::vector<Edge_class> {
  assert_accessible();
  assert_compatible(pin);
  std::vector<Edge_class> out;
  const Pid               self_pid      = pin.get_pin_pid();
  const Pid               self_pid_sink = (self_pid & ~static_cast<Pid>(2)) | static_cast<Pid>(1);
  auto*                   self          = ref_pin(self_pid_sink);
  auto                    edges         = self->get_edges(self_pid_sink);
  const Pin_class         self_sink_pin = make_pin_class(self_pid_sink);

  for (auto vid : edges) {
    if (!(vid & 2)) {
      continue;
    }
    if (vid & 1) {
      const Pid driver_pid = static_cast<Pid>(vid);

      Edge_class e{};
      e.type       = 2;  // p -> p
      e.driver_pin = make_pin_class(driver_pid);
      e.sink_pin   = self_sink_pin;
      out.push_back(e);
      continue;
    }

    const Nid driver_nid = static_cast<Nid>(vid);

    Edge_class e{};
    e.type     = 3;  // n -> p
    e.driver   = Node_class(this, driver_nid);
    e.sink_pin = self_sink_pin;
    out.push_back(e);
  }

  return out;
}

auto Graph::get_pins(Node_class node) -> std::vector<Pin_class> {
  assert_accessible();
  assert_compatible(node);
  std::vector<Pin_class> out;
  const Nid              self_nid = node.get_raw_nid() & ~static_cast<Nid>(2);
  auto*                  self     = ref_node(self_nid);

  Pid cur_pin = self->get_next_pin_id();
  while (cur_pin != 0) {
    const Pid canonical_pin = (cur_pin & ~static_cast<Pid>(2)) | static_cast<Pid>(1);
    out.push_back(make_pin_class(canonical_pin));
    cur_pin = ref_pin(canonical_pin)->get_next_pin_id();
  }

  return out;
}

auto Graph::get_driver_pins(Node_class node) -> std::vector<Pin_class> {
  assert_accessible();
  assert_compatible(node);
  std::vector<Pin_class> out;
  for (const auto& pin : get_pins(node)) {
    const Pid pid_lookup = (pin.get_pin_pid() & ~static_cast<Pid>(2)) | static_cast<Pid>(1);
    auto*     self       = ref_pin(pid_lookup);
    auto      edges      = self->get_edges(pid_lookup);

    for (auto vid : edges) {
      // Driver pin: edge is outgoing (bit1=0) and target is a pin (bit0=1).
      if (!(vid & static_cast<Vid>(2)) && (vid & static_cast<Vid>(1))) {
        out.push_back(make_pin_class(pid_lookup));
        break;
      }
    }
  }
  return out;
}

auto Graph::get_sink_pins(Node_class node) -> std::vector<Pin_class> {
  assert_accessible();
  assert_compatible(node);
  std::vector<Pin_class> out;
  for (const auto& pin : get_pins(node)) {
    const Pid pid_lookup = (pin.get_pin_pid() & ~static_cast<Pid>(2)) | static_cast<Pid>(1);
    auto*     self       = ref_pin(pid_lookup);
    auto      edges      = self->get_edges(pid_lookup);

    for (auto vid : edges) {
      // Sink pin: edge is incoming (bit1=1) and source is a pin (bit0=1).
      if ((vid & static_cast<Vid>(3)) == static_cast<Vid>(3)) {
        out.push_back(make_pin_class(pid_lookup));
        break;
      }
    }
  }
  return out;
}

void Graph::delete_node(Nid nid) {
  assert_accessible();
  invalidate_traversal_caches();

  auto* node = ref_node(nid);
  if (!node) {
    return;
  }

  // Get all edges from this node
  auto             edges = node->get_edges(nid);
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
        } else {
          // we need to check sedges_.sedges and ledge0/ledge1 for the edge to remove
          constexpr int      SHIFT      = 14;
          constexpr uint64_t SLOT_MASK  = (1ULL << SHIFT) - 1;
          const uint64_t     self_num   = static_cast<uint64_t>(other_vid) >> 2;
          const Vid          actual_nid = nid >> 2;

          for (int i = 0; i < 4; ++i) {
            uint64_t raw = (other_pin->sedges_.sedges >> (i * SHIFT)) & SLOT_MASK;
            if (raw == 0) {
              continue;
            }
            bool     neg    = raw & (1ULL << 13);
            bool     driver = raw & (1ULL << 1);
            bool     pin    = raw & (1ULL << 0);
            uint64_t mag    = (raw >> 2) & ((1ULL << 11) - 1);
            int64_t  diff   = neg ? -static_cast<int64_t>(mag) : static_cast<int64_t>(mag);

            // self_num - actual_nid = diff => actual_nid = self_num - diff
            uint64_t actual_nid = nid >> 2;
            uint64_t target_num = self_num - diff;

            if (target_num != actual_nid) {
              continue;
            } else {
              // we found the edge to remove, now check if driver/pin bits match
              // delete
              other_pin->sedges_.sedges &= ~(SLOT_MASK << (i * SHIFT));
              break;  // break after finding the edge to remove
            }
          }
          if (other_pin->ledge0 == nid || other_pin->ledge0 == (nid | static_cast<Vid>(2))) {
            other_pin->ledge0 = 0;
          } else if (other_pin->ledge1 == nid || other_pin->ledge1 == (nid | static_cast<Vid>(2))) {
            other_pin->ledge1 = 0;
          }
        }
      } else {
        // This can happen if the other pin was already deleted as part of this node deletion process, so we can ignore it.
        std::cerr << "Warning: Connected pin " << (other_vid >> 2) << " has no edges while deleting node " << nid << "\n";
      }
    } else {
      auto* other_node = ref_node(other_vid);
      if (other_node && other_node->has_edges()) {
        // Need to remove nid from other_node's edge list
        if (other_node->use_overflow) {
          other_node->sedges_.set->erase(nid);
          other_node->sedges_.set->erase(nid | 2);
        } else {
          // we need to check sedges_.sedges and ledge0/ledge1 for the edge to remove
          constexpr int      SHIFT     = 14;
          constexpr uint64_t SLOT_MASK = (1ULL << SHIFT) - 1;
          const uint64_t     self_num  = static_cast<uint64_t>(other_vid) >> 2;

          for (int i = 0; i < 4; ++i) {
            uint64_t raw = (other_node->sedges_.sedges >> (i * SHIFT)) & SLOT_MASK;
            if (raw == 0) {
              continue;
            }
            bool     neg    = raw & (1ULL << 13);
            bool     driver = raw & (1ULL << 1);
            bool     pin    = raw & (1ULL << 0);
            uint64_t mag    = (raw >> 2) & ((1ULL << 11) - 1);
            int64_t  diff   = neg ? -static_cast<int64_t>(mag) : static_cast<int64_t>(mag);

            // self_num - actual_nid = diff => actual_nid = self_num - diff
            uint64_t actual_nid = nid >> 2;
            uint64_t target_num = self_num - diff;
            if (target_num != actual_nid) {
              continue;
            } else {
              other_node->sedges_.sedges &= ~(SLOT_MASK << (i * SHIFT));
              break;  // break after finding the edge to remove
            }
          }
          if (other_node->ledge0 == nid || other_node->ledge0 == (nid | static_cast<Vid>(2))) {
            other_node->ledge0 = 0;
          } else if (other_node->ledge1 == nid || other_node->ledge1 == (nid | static_cast<Vid>(2))) {
            other_node->ledge1 = 0;
          }
        }
      } else {
        // This can happen if the other node was already deleted as part of this node deletion process, so we can ignore it.
        std::cerr << "Warning: Connected node " << (other_vid >> 2) << " has no edges while deleting node " << nid << "\n";
      }
    }
  }

  // Also remove edges from pins of this node
  Pid cur_pin = node->get_next_pin_id();
  while (cur_pin != 0) {
    // cur_pin is encoded vid; decode before indexing pin table.
    Pid   pin_vid       = cur_pin;
    Pid   actual_pin_id = pin_vid >> 2;
    auto* pin           = &pin_table[actual_pin_id];

    // Get all edges from this pin
    auto             pin_edges = pin->get_edges(pin_vid);
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
          } else {
            // we need to check sedges_.sedges and ledge0/ledge1 for the edge to remove
            constexpr int      SHIFT          = 14;
            constexpr uint64_t SLOT_MASK      = (1ULL << SHIFT) - 1;
            const uint64_t     self_num       = static_cast<uint64_t>(other_vid) >> 2;
            const Vid          actual_pin_vid = actual_pin_id;

            for (int i = 0; i < 4; ++i) {
              uint64_t raw = (other_pin->sedges_.sedges >> (i * SHIFT)) & SLOT_MASK;
              if (raw == 0) {
                continue;
              }
              bool     neg    = raw & (1ULL << 13);
              bool     driver = raw & (1ULL << 1);
              bool     pin    = raw & (1ULL << 0);
              uint64_t mag    = (raw >> 2) & ((1ULL << 11) - 1);
              int64_t  diff   = neg ? -static_cast<int64_t>(mag) : static_cast<int64_t>(mag);

              // self_num - actual_pin_vid = diff => actual_pin_vid = self_num - diff
              uint64_t target_num = self_num - diff;

              if (target_num != actual_pin_vid) {
                continue;
              } else {
                other_pin->sedges_.sedges &= ~(SLOT_MASK << (i * SHIFT));
                break;  // break after finding the edge to remove
              }
            }
            if (other_pin->ledge0 == pin_vid || other_pin->ledge0 == (pin_vid | static_cast<Vid>(2))) {
              other_pin->ledge0 = 0;
            } else if (other_pin->ledge1 == pin_vid || other_pin->ledge1 == (pin_vid | static_cast<Vid>(2))) {
              other_pin->ledge1 = 0;
            }
          }
        } else {
          // This can happen if the other pin was already deleted as part of this node deletion process, so we can ignore it.
          std::cerr << "Warning: Connected pin " << (other_vid >> 2) << " has no edges while deleting node " << nid << "\n";
        }
      } else {
        auto* other_node = ref_node(other_vid);
        if (other_node && other_node->has_edges()) {
          if (other_node->use_overflow) {
            other_node->sedges_.set->erase(pin_vid);
            other_node->sedges_.set->erase(pin_vid | 2);
          } else {
            // we need to check sedges_.sedges and ledge0/ledge1 for the edge to remove
            constexpr int      SHIFT          = 14;
            constexpr uint64_t SLOT_MASK      = (1ULL << SHIFT) - 1;
            const uint64_t     self_num       = static_cast<uint64_t>(other_vid) >> 2;
            const Vid          actual_pin_vid = actual_pin_id;

            for (int i = 0; i < 4; ++i) {
              uint64_t raw = (other_node->sedges_.sedges >> (i * SHIFT)) & SLOT_MASK;
              if (raw == 0) {
                continue;
              }
              bool     neg    = raw & (1ULL << 13);
              bool     driver = raw & (1ULL << 1);
              bool     pin    = raw & (1ULL << 0);
              uint64_t mag    = (raw >> 2) & ((1ULL << 11) - 1);
              int64_t  diff   = neg ? -static_cast<int64_t>(mag) : static_cast<int64_t>(mag);

              // self_num - actual_pin_vid = diff => actual_pin_vid = self_num - diff
              uint64_t target_num = self_num - diff;

              if (target_num != actual_pin_vid) {
                continue;
              } else {
                other_node->sedges_.sedges &= ~(SLOT_MASK << (i * SHIFT));
                break;  // break after finding the edge to remove
              }
            }
            if (other_node->ledge0 == pin_vid || other_node->ledge0 == (pin_vid | static_cast<Vid>(2))) {
              other_node->ledge0 = 0;
            } else if (other_node->ledge1 == pin_vid || other_node->ledge1 == (pin_vid | static_cast<Vid>(2))) {
              other_node->ledge1 = 0;
            }
          }
        } else {
          // This can happen if the other node was already deleted as part of this node deletion process, so we can ignore it.
          std::cerr << "Warning: Connected node " << (other_vid >> 2) << " has no edges while deleting node " << nid << "\n";
        }
      }
    }

    // Clear this pin's edges
    if (pin->use_overflow && pin->sedges_.set) {
      pin->sedges_.set->clear();
    } else {
      pin->sedges_.sedges = 0;
      pin->ledge0         = 0;
      pin->ledge1         = 0;
    }

    cur_pin = pin->get_next_pin_id();
  }

  // Clear the node's edges
  if (node->use_overflow && node->sedges_.set) {
    node->sedges_.set->clear();
  } else {
    node->sedges_.sedges = 0;
    node->ledge0         = 0;
    node->ledge1         = 0;
  }
}

auto Graph::ref_node(Nid id) const -> Node* {
  assert_accessible();
  Nid actual_id = id >> 2;
  assert(actual_id < node_table.size());
  return (Node*)&node_table[actual_id];
}

auto Graph::ref_pin(Pid id) const -> Pin* {
  assert_accessible();
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
  // here next_pin is not << 2 but nid is << 2
  Nid  actual_nid = nid >> 2;
  auto node       = &node_table[actual_nid];
  if (node->get_next_pin_id() == 0) {
    node->set_next_pin_id(next_pin << 2 | 1);
    return;
  }
  Pid cur = node->get_next_pin_id();
  cur     = cur >> 2;
  while (pin_table[cur].get_next_pin_id()) {
    cur = pin_table[cur].get_next_pin_id();
    cur = cur >> 2;
  }
  pin_table[cur].set_next_pin_id(next_pin << 2 | 1);
}

void Graph::display_graph() const {
  assert_accessible();
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
  assert_accessible();
  for (Nid nid = 1; nid < node_table.size(); ++nid) {
    std::cout << "Node " << nid << " first_pin=" << node_table[nid].get_next_pin_id() << "\n";
  }
}

}  // namespace hhds
