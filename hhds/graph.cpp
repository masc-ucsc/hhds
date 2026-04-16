#include "graph.hpp"

#include <strings.h>

#include <algorithm>
#include <ctime>
#include <functional>
#include <iostream>
#include <queue>
#include <sstream>
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

Node_hier::Node_hier(Tid hier_tid_value, std::shared_ptr<std::vector<Gid>> hier_gids_value, Tree_pos hier_pos_value,
                     Nid raw_nid_value)
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

auto to_class(const Node_hier& v) -> Node_class { return Node_class(v.get_raw_nid() & ~static_cast<Nid>(2)); }

auto to_flat(const Node_hier& v) -> Node_flat {
  return Node_flat(v.get_root_gid(), v.get_current_gid(), v.get_raw_nid() & ~static_cast<Nid>(2));
}

auto to_class(const Node_flat& v) -> Node_class { return Node_class(v.get_raw_nid() & ~static_cast<Nid>(2)); }

auto to_flat(const Node_class& v, Gid current_gid, Gid root_gid) -> Node_flat {
  if (root_gid == Gid_invalid) {
    root_gid = current_gid;
  }
  return Node_flat(root_gid, current_gid, v.get_raw_nid() & ~static_cast<Nid>(2));
}

auto Node_class::get_root_gid() const noexcept -> Gid {
  if (context_ == Context::Flat) {
    return root_gid_;
  }
  if (context_ == Context::Hier) {
    if (!hier_gids_ || ROOT >= static_cast<Tree_pos>(hier_gids_->size())) {
      return Gid_invalid;
    }
    return (*hier_gids_)[ROOT];
  }
  return graph_ != nullptr ? graph_->get_gid() : Gid_invalid;
}

auto Node_class::get_current_gid() const noexcept -> Gid {
  if (context_ == Context::Flat) {
    return current_gid_;
  }
  if (context_ == Context::Hier) {
    if (!hier_gids_ || hier_pos_ >= static_cast<Tree_pos>(hier_gids_->size())) {
      return Gid_invalid;
    }
    return (*hier_gids_)[static_cast<size_t>(hier_pos_)];
  }
  return graph_ != nullptr ? graph_->get_gid() : Gid_invalid;
}

auto Pin_class::get_root_gid() const noexcept -> Gid {
  if (context_ == Handle_context::Flat) {
    return root_gid_;
  }
  if (context_ == Handle_context::Hier) {
    if (!hier_gids_ || ROOT >= static_cast<Tree_pos>(hier_gids_->size())) {
      return Gid_invalid;
    }
    return (*hier_gids_)[ROOT];
  }
  return graph_ != nullptr ? graph_->get_gid() : Gid_invalid;
}

auto Pin_class::get_current_gid() const noexcept -> Gid {
  if (context_ == Handle_context::Flat) {
    return current_gid_;
  }
  if (context_ == Handle_context::Hier) {
    if (!hier_gids_ || hier_pos_ >= static_cast<Tree_pos>(hier_gids_->size())) {
      return Gid_invalid;
    }
    return (*hier_gids_)[static_cast<size_t>(hier_pos_)];
  }
  return graph_ != nullptr ? graph_->get_gid() : Gid_invalid;
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
  out.driver = to_flat(e.driver_pin(), current_gid, root_gid);
  out.sink   = to_flat(e.sink_pin(), current_gid, root_gid);
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
  out.driver = Pin_hier(hier_tid,
                        hier_gids,
                        hier_pos,
                        e.driver_pin().get_raw_nid(),
                        e.driver_pin().get_port_id(),
                        e.driver_pin().get_pin_pid());
  out.sink   = Pin_hier(hier_tid,
                        std::move(hier_gids),
                        hier_pos,
                        e.sink_pin().get_raw_nid(),
                        e.sink_pin().get_port_id(),
                        e.sink_pin().get_pin_pid());
  return out;
}

auto to_class(const Edge_flat& e) -> Edge_class {
  Edge_class out{};
  out.driver_pin_ = to_class(e.driver);
  out.sink_pin_   = to_class(e.sink);
  out.driver_     = Node_class(out.driver_pin_.get_raw_nid() | static_cast<Nid>(2));
  out.sink_       = Node_class(out.sink_pin_.get_raw_nid() & ~static_cast<Nid>(2));
  out.type        = 2;  // p -> p
  return out;
}

auto to_class(const Edge_hier& e) -> Edge_class {
  Edge_class out{};
  out.driver_pin_ = to_class(e.driver);
  out.sink_pin_   = to_class(e.sink);
  out.driver_     = Node_class(out.driver_pin_.get_raw_nid() | static_cast<Nid>(2));
  out.sink_       = Node_class(out.sink_pin_.get_raw_nid() & ~static_cast<Nid>(2));
  out.type        = 2;  // p -> p
  return out;
}

PinEntry::PinEntry() : master_nid(0), port_id(0), next_pin_id(0), ledge0(0), ledge1(0), use_overflow(0), sedges_{.sedges = 0} {}

PinEntry::PinEntry(Nid mn, Port_id pid)
    : master_nid(mn), port_id(pid), next_pin_id(0), ledge0(0), ledge1(0), use_overflow(0), sedges_{.sedges = 0} {}

Nid     PinEntry::get_master_nid() const { return master_nid; }
Port_id PinEntry::get_port_id() const { return port_id; }
Pid     PinEntry::get_next_pin_id() const { return next_pin_id; }
void    PinEntry::set_next_pin_id(Pid id) { next_pin_id = id; }

auto PinEntry::overflow_handling(Pid self_id, Vid other_id, OverflowPool& pool) -> bool {
  if (use_overflow) {
    pool.sets[sedges_.overflow_idx].insert(other_id);
    return true;
  }
  uint32_t idx = pool.alloc();
  auto&              hs        = pool.sets[idx];
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
    hs.insert(target);
  }
  if (ledge0) {
    hs.insert(static_cast<Vid>(ledge0));
  }
  if (ledge1) {
    hs.insert(static_cast<Vid>(ledge1));
  }
  use_overflow         = true;
  sedges_.overflow_idx = idx;
  ledge0 = ledge1 = 0;

  hs.insert(other_id);
  return true;
}

auto PinEntry::add_edge(Pid self_id, Vid other_id, OverflowPool& pool) -> bool {
  if (use_overflow) {
    return overflow_handling(self_id, other_id, pool);
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
    return overflow_handling(self_id, other_id, pool);
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
  return overflow_handling(self_id, other_id, pool);
}

auto PinEntry::delete_edge(Pid self_id, Vid other_id, OverflowPool& pool) -> bool {
  if (use_overflow) {
    return pool.sets[sedges_.overflow_idx].erase(other_id) != 0;
  }

  std::vector<Vid> keep;
  bool             removed = false;
  for (auto edge : get_edges(self_id, pool.sets)) {
    if (edge == other_id) {
      removed = true;
      continue;
    }
    keep.push_back(edge);
  }
  if (!removed) {
    return false;
  }

  sedges_.sedges = 0;
  ledge0         = 0;
  ledge1         = 0;
  for (auto edge : keep) {
    (void)add_edge(self_id, edge, pool);
  }
  return true;
}

PinEntry::EdgeRange::EdgeRange(const PinEntry* pin, Pid pid, const OverflowVec& overflow) noexcept
    : pin_(pin), set_(nullptr), own_(false) {
  if (pin->use_overflow) {
    set_ = acquire_set();
    set_->clear();
    for (auto raw : overflow[pin->sedges_.overflow_idx]) {
      set_->insert(raw);
    }
  } else {
    set_ = acquire_set();
    own_ = true;
    set_->clear();
    populate_set(pin, *set_, pid);
  }
}

PinEntry::EdgeRange::EdgeRange(EdgeRange&& o) noexcept : pin_(o.pin_), set_(o.set_), own_(o.own_) {
  o.pin_ = nullptr;
  o.set_ = nullptr;
}

PinEntry::EdgeRange::~EdgeRange() noexcept {
  if (own_) {
    release_set(set_);
  }
}

ankerl::unordered_dense::set<Vid>* PinEntry::EdgeRange::acquire_set() noexcept {
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

void PinEntry::EdgeRange::release_set(ankerl::unordered_dense::set<Vid>* set) noexcept {
  static thread_local std::vector<ankerl::unordered_dense::set<Vid>*> pool;
  pool.push_back(set);
}

void PinEntry::EdgeRange::populate_set(const PinEntry* pin, ankerl::unordered_dense::set<Vid>& set, Pid pid) noexcept {
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

auto PinEntry::get_edges(Pid pid, const OverflowVec& overflow) const noexcept -> EdgeRange {
  return EdgeRange(this, pid, overflow);
}

bool PinEntry::has_edges() const {
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

NodeEntry::NodeEntry() { clear_node(); }
NodeEntry::NodeEntry(Nid nid_val) {
  clear_node();
  nid = nid_val;
}

void NodeEntry::clear_node() { bzero(this, sizeof(NodeEntry)); }

void NodeEntry::set_type(Type t) { type = t; }
Nid  NodeEntry::get_nid() const { return nid; }
Type NodeEntry::get_type() const { return type; }
Pid  NodeEntry::get_next_pin_id() const { return next_pin_id; }
void NodeEntry::set_next_pin_id(Pid id) { next_pin_id = id; }

auto NodeEntry::overflow_handling(Nid self_id, Vid other_id, OverflowPool& pool) -> bool {
  if (use_overflow) {
    if (other_id) {
      pool.sets[sedges_.overflow_idx].insert(other_id);
    }
    return true;
  }

  uint32_t idx = pool.alloc();
  auto&              hs        = pool.sets[idx];
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
    hs.insert(target);
  }

  if (ledge0) {
    hs.insert(static_cast<Vid>(ledge0));
  }
  if (ledge1) {
    hs.insert(static_cast<Vid>(ledge1));
  }

  use_overflow         = 1;
  sedges_.overflow_idx = idx;
  ledge0 = ledge1 = 0;

  if (other_id) {
    hs.insert(other_id);
  }
  return true;
}

auto NodeEntry::add_edge(Nid self_id, Vid other_id, OverflowPool& pool) -> bool {
  if (use_overflow) {
    return overflow_handling(self_id, other_id, pool);
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
    return overflow_handling(self_id, other_id, pool);
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
  return overflow_handling(self_id, other_id, pool);
}

auto NodeEntry::delete_edge(Nid self_id, Vid other_id, OverflowPool& pool) -> bool {
  if (use_overflow) {
    return pool.sets[sedges_.overflow_idx].erase(other_id) != 0;
  }

  std::vector<Vid> keep;
  bool             removed = false;
  for (auto edge : get_edges(self_id, pool.sets)) {
    if (edge == other_id) {
      removed = true;
      continue;
    }
    keep.push_back(edge);
  }
  if (!removed) {
    return false;
  }

  sedges_.sedges = 0;
  ledge0         = 0;
  ledge1         = 0;
  for (auto edge : keep) {
    (void)add_edge(self_id, edge, pool);
  }
  return true;
}

bool NodeEntry::has_edges(const OverflowVec& overflow) const {
  if (use_overflow) {
    return !overflow[sedges_.overflow_idx].empty();
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

void NodeEntry::set_subnode(Gid gid, OverflowPool& pool) {
  // Existence inside GraphLibrary must be checked by the caller (NodeEntry has no library pointer).
  if (gid == Gid_invalid) {
    return;
  }
  // Ensure overflow mode so ledge0 is no longer used as an edge spill slot.
  if (!use_overflow) {
    const Vid self_vid = (static_cast<Vid>(nid) << 2);  // nid stored as numeric id
    (void)overflow_handling(self_vid, 0, pool);          // 0 => promote only, no edge insert
  }

  // Store gid in ledge0 as (gid + 1) so ledge0==0 means "no subnode".
  const uint64_t g = static_cast<uint64_t>(gid);
  assert(g < (1ULL << Nid_bits));  // since ledge0 is Nid_bits wide
  ledge0 = static_cast<Nid>(g);
}

Gid NodeEntry::get_subnode() const noexcept {
  if (!use_overflow || ledge0 == 0) {
    return Gid_invalid;
  }
  return static_cast<Gid>(static_cast<uint64_t>(ledge0));
}

bool NodeEntry::has_subnode() const noexcept { return use_overflow && ledge0 != 0; }

NodeEntry::EdgeRange::EdgeRange(const NodeEntry* node, Nid nid, const OverflowVec& overflow) noexcept
    : node_(node), set_(nullptr), own_(false) {
  if (node->use_overflow) {
    set_ = acquire_set();
    set_->clear();
    for (auto raw : overflow[node->sedges_.overflow_idx]) {
      set_->insert(raw);
    }
  } else {
    set_ = acquire_set();
    own_ = true;
    set_->clear();
    populate_set(node, *set_, nid);
  }
}

NodeEntry::EdgeRange::EdgeRange(EdgeRange&& o) noexcept : node_(o.node_), /* nid_(o.nid_), */ set_(o.set_), own_(o.own_) {
  o.node_ = nullptr;
  o.set_  = nullptr;
}

NodeEntry::EdgeRange::~EdgeRange() noexcept {
  if (own_) {
    release_set(set_);
  }
}

ankerl::unordered_dense::set<Nid>* NodeEntry::EdgeRange::acquire_set() noexcept {
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

void NodeEntry::EdgeRange::release_set(ankerl::unordered_dense::set<Nid>* set) noexcept {
  static thread_local std::vector<ankerl::unordered_dense::set<Nid>*> pool;
  pool.push_back(set);
}

void NodeEntry::EdgeRange::populate_set(const NodeEntry* node, ankerl::unordered_dense::set<Nid>& set, Nid nid) noexcept {
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

auto NodeEntry::get_edges(Nid nid, const OverflowVec& overflow) const noexcept -> EdgeRange {
  return EdgeRange(this, nid, overflow);
}

Graph::Graph() { clear_graph(); }

void Graph::assert_accessible() const noexcept { assert(!deleted_ && "graph is no longer valid"); }

bool Graph::is_node_valid(Nid nid) const noexcept {
  if (deleted_) {
    return false;
  }
  const Nid actual_id = (nid & ~static_cast<Nid>(3)) >> 2;
  return actual_id > 0 && actual_id < node_table.size() && node_table[actual_id].get_nid() != 0;
}

bool Graph::is_pin_valid(Pid pid) const noexcept {
  if (deleted_) {
    return false;
  }
  if (!(pid & static_cast<Pid>(1))) {
    return is_node_valid(pid);
  }
  const Pid actual_id = pid >> 2;
  return actual_id > 0 && actual_id < pin_table.size() && pin_table[actual_id].get_master_nid() != 0;
}

void Graph::assert_node_exists(const Node_class& node) const noexcept {
  assert_accessible();
  const Nid raw_nid   = node.get_raw_nid();
  const Nid actual_id = raw_nid >> 2;
  assert(node.get_graph() == this && "node handle belongs to a different graph");
  assert((raw_nid & static_cast<Nid>(1)) == 0 && "node handle is not a node");
  assert(actual_id > 0 && actual_id < node_table.size() && "node handle is invalid for this graph");
  assert(node_table[actual_id].get_nid() != 0 && "node handle refers to a deleted node");
}

void Graph::assert_pin_exists(const Pin_class& pin) const noexcept {
  assert_accessible();
  assert(pin.get_graph() == this && "pin handle belongs to a different graph");
  const Pid raw_pid = pin.get_pin_pid();
  if (!(raw_pid & static_cast<Pid>(1))) {
    // port_id == 0: node-as-pin — validate as a node
    const Nid actual_id = raw_pid >> 2;
    assert(actual_id > 0 && actual_id < node_table.size() && "pin(0) node handle is invalid for this graph");
    assert(node_table[actual_id].get_nid() != 0 && "pin(0) refers to a deleted node");
    return;
  }
  const Pid actual_id = raw_pid >> 2;
  assert(actual_id > 0 && actual_id < pin_table.size() && "pin handle is invalid for this graph");
  assert(pin_table[actual_id].get_master_nid() != 0 && "pin handle refers to a deleted pin");
}

void Graph::release_storage() noexcept {
  overflow_sets_.clear();
  overflow_free_.clear();
}

void Graph::invalidate_from_library() noexcept {
  if (deleted_) {
    return;
  }
  release_storage();
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
  input_pins_.clear();
  output_pins_.clear();
}

void Graph::clear_graph() {
  assert_accessible();
  release_storage();
  node_table.clear();
  pin_table.clear();
  input_pins_.clear();
  output_pins_.clear();
  node_table.emplace_back(0);  // Invalid ID
  node_table.emplace_back(1);  // Input node (can have many pins to node 1)
  node_table.emplace_back(2);  // Output node
  node_table.emplace_back(3);  // Constant (common value/issue to handle for toposort) - Each const value is a pin in node3
  pin_table.emplace_back(0, 0);
  invalidate_traversal_caches();
}

void Graph::clear() {
  assert_accessible();

  overflow_sets_.clear();
  overflow_free_.clear();

  for (auto& pin : pin_table) {
    pin = PinEntry();
  }
  if (pin_table.empty()) {
    pin_table.emplace_back(0, 0);
  } else {
    pin_table[0] = PinEntry(0, 0);
  }

  if (node_table.size() < 4) {
    node_table.clear();
    node_table.emplace_back(0);
    node_table.emplace_back(1);
    node_table.emplace_back(2);
    node_table.emplace_back(3);
  } else {
    for (size_t idx = 0; idx < node_table.size(); ++idx) {
      auto& node = node_table[idx];
      if (idx < 4) {
        node = NodeEntry(static_cast<Nid>(idx));
      } else {
        node = NodeEntry();
      }
    }
  }

  input_pins_.clear();
  output_pins_.clear();
  invalidate_traversal_caches();
  if (auto graphio = get_io()) {
    for (const auto& input : graphio->input_pin_decls_) {
      (void)materialize_declared_io_pin(input.name, input.port_id, INPUT_NODE, input_pins_);
    }
    for (const auto& output : graphio->output_pin_decls_) {
      (void)materialize_declared_io_pin(output.name, output.port_id, OUTPUT_NODE, output_pins_);
    }
  }
}

void Graph::bind_library(const GraphLibrary* owner, Gid self_gid) noexcept {
  owner_lib_ = owner;
  self_gid_  = self_gid;
  deleted_   = false;
}

void Graph::invalidate_traversal_caches() noexcept {
  dirty_                     = true;
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
      if (node_table[i].get_nid() == 0) {
        continue;
      }
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
    fast_flat_cache_.emplace_back(const_cast<Graph*>(this), it.get_top_graph(), it.get_curr_graph(), it.get_node_id());
  }
}

void Graph::rebuild_fast_hier_cache() const {
  fast_hier_cache_.clear();
  fast_hier_tree_cache_ = Tree::create();
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
    if (node_table[driver_idx].get_nid() == 0) {
      continue;
    }
    const Nid driver_nid = static_cast<Nid>(driver_idx) << 2;

    auto node_edges = node_table[driver_idx].get_edges(driver_nid, overflow_sets_);
    for (auto vid : node_edges) {
      if (vid & static_cast<Vid>(2)) {
        continue;
      }
      add_dependency(driver_idx, vid);
    }

    for (Pid pin_vid = node_table[driver_idx].get_next_pin_id(); pin_vid != 0;) {
      const Pid canonical_pin = (pin_vid & ~static_cast<Pid>(2)) | static_cast<Pid>(1);
      auto      pin_edges     = ref_pin(canonical_pin)->get_edges(canonical_pin, overflow_sets_);
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
    if (node_table[idx].get_nid() != 0 && indegree[idx] == 0) {
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
    if (node_table[idx].get_nid() != 0 && !emitted[idx]) {
      forward_class_cache_.emplace_back(const_cast<Graph*>(this), static_cast<Nid>(idx) << 2);
    }
  }

  forward_class_cache_valid_ = true;
}

void Graph::forward_flat_impl(Gid top_graph, ankerl::unordered_dense::set<Gid>& active_graphs, std::vector<Node>& out) const {
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

    out.emplace_back(const_cast<Graph*>(this), top_graph, self_gid_, node_nid);
  }
}

void Graph::fast_hier_impl(std::shared_ptr<Tree> hier_tree, Tid hier_tid, std::shared_ptr<std::vector<Gid>> hier_gids,
                           Tree_pos hier_pos, ankerl::unordered_dense::set<Gid>& active_graphs, std::vector<Node>& out) const {
  for (size_t i = 1; i < node_table.size(); ++i) {
    if (node_table[i].get_nid() == 0) {
      continue;
    }
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

    out.emplace_back(const_cast<Graph*>(this), hier_tid, hier_gids, hier_pos, node_id);
  }
}

void Graph::forward_hier_impl(std::shared_ptr<Tree> hier_tree, Tid hier_tid, std::shared_ptr<std::vector<Gid>> hier_gids,
                              Tree_pos hier_pos, ankerl::unordered_dense::set<Gid>& active_graphs, std::vector<Node>& out) const {
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
        owner_lib_->get_graph(other_graph_id)
            ->forward_hier_impl(hier_tree, hier_tid, hier_gids, child_hier_pos, active_graphs, out);
        active_graphs.erase(other_graph_id);
        continue;
      }
    }

    out.emplace_back(const_cast<Graph*>(this), hier_tid, hier_gids, hier_pos, node_nid);
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
  forward_hier_tree_cache_ = Tree::create();
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
    if (node_table[i].get_nid() == 0) {
      continue;
    }
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

auto Graph::create_node() -> Node {
  assert_accessible();
  Nid id = node_table.size();
  assert(id);
  node_table.emplace_back(id);
  invalidate_traversal_caches();
  Nid raw_nid = id << 2 | 0;
  return Node_class(this, raw_nid);
}

auto Graph::create_pin(Node_class node, Port_id port_id) -> Pin_class {
  assert_node_exists(node);
  const Pid pin_pid = create_pin(node.get_raw_nid(), port_id);
  return Pin_class(this, node.get_raw_nid(), port_id, pin_pid);
}

auto Graph::create_pin(Nid nid, Port_id pid) -> Pid {
  assert_accessible();
  nid &= ~static_cast<Nid>(2);  // PinEntry ownership is by node identity, independent of edge role bit.
  const Nid actual_nid = nid >> 2;
  assert(actual_nid > 0 && actual_nid < node_table.size() && "create_pin: node handle is invalid for this graph");
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

void inherit_pin_context(Pin_class& pin, const Node_class& node) {
  pin.context_     = node.context_;
  pin.root_gid_    = node.root_gid_;
  pin.current_gid_ = node.current_gid_;
  pin.hier_gids_   = node.hier_gids_;
  pin.hier_tid_    = node.hier_tid_;
  pin.hier_pos_    = node.hier_pos_;
}

auto Graph::find_pin(Node_class node, Port_id port_id, bool driver) const -> Pin_class {
  assert_node_exists(node);
  if (port_id == 0) {
    // port_id == 0 is the node itself acting as a pin
    const Nid nid = node.get_raw_nid() & ~static_cast<Nid>(2);
    Pid       pid = nid;
    if (driver) {
      pid |= static_cast<Pid>(2);
    }
    return Pin_class(const_cast<Graph*>(this), nid, 0, pid);
  }
  const Nid self_nid = node.get_raw_nid() & ~static_cast<Nid>(2);
  auto*     self     = ref_node(self_nid);
  for (Pid cur_pin = self->get_next_pin_id(); cur_pin != 0;) {
    const Pid canonical_pin = (cur_pin & ~static_cast<Pid>(2)) | static_cast<Pid>(1);
    auto*     pin           = ref_pin(canonical_pin);
    if (pin->get_port_id() == port_id) {
      return make_pin_class(canonical_pin);
    }
    cur_pin = pin->get_next_pin_id();
  }
  assert(false && "get_pin: requested pin was not created");
  return {};
}

auto Graph::find_or_create_pin(Node_class node, Port_id port_id) -> Pin_class {
  assert_node_exists(node);
  assert(port_id != 0 && "find_or_create_pin: port_id 0 is the node itself");
  const Nid self_nid = node.get_raw_nid() & ~static_cast<Nid>(2);
  auto*     self     = ref_node(self_nid);
  for (Pid cur_pin = self->get_next_pin_id(); cur_pin != 0;) {
    const Pid canonical_pin = (cur_pin & ~static_cast<Pid>(2)) | static_cast<Pid>(1);
    auto*     pin           = ref_pin(canonical_pin);
    if (pin->get_port_id() == port_id) {
      return make_pin_class(canonical_pin);
    }
    cur_pin = pin->get_next_pin_id();
  }
  return create_pin(node, port_id);
}

auto Graph::resolve_driver_port(Node_class node, std::string_view name) const -> Port_id {
  assert_node_exists(node);
  const auto* entry = ref_node(node.get_raw_nid());
  assert(entry->has_subnode() && "create_driver_pin: string form requires a subnode GraphIO");
  assert(owner_lib_ != nullptr && "create_driver_pin: graph has no GraphLibrary");
  auto gio = owner_lib_->graph_ios_[static_cast<size_t>(entry->get_subnode())];
  assert(gio != nullptr && gio->has_output(name) && "create_driver_pin: output name not found in subnode GraphIO");
  return gio->get_output_port_id(name);
}

auto Graph::resolve_sink_port(Node_class node, std::string_view name) const -> Port_id {
  assert_node_exists(node);
  const auto* entry = ref_node(node.get_raw_nid());
  assert(entry->has_subnode() && "create_sink_pin: string form requires a subnode GraphIO");
  assert(owner_lib_ != nullptr && "create_sink_pin: graph has no GraphLibrary");
  auto gio = owner_lib_->graph_ios_[static_cast<size_t>(entry->get_subnode())];
  assert(gio != nullptr && gio->has_input(name) && "create_sink_pin: input name not found in subnode GraphIO");
  return gio->get_input_port_id(name);
}

auto Graph::pin_name(Pin_class pin) const -> std::string_view {
  assert_pin_exists(pin);

  const Pid  raw_pid        = pin.get_pin_pid();
  const bool is_node_as_pin = !(raw_pid & static_cast<Pid>(1));

  // Determine the owning node and port_id
  Nid     owner_nid;
  Port_id pin_port_id;
  if (is_node_as_pin) {
    // port_id == 0: the node itself acts as pin
    owner_nid   = raw_pid & ~static_cast<Nid>(3);
    pin_port_id = 0;
  } else {
    const auto* pin_entry = ref_pin(raw_pid);
    owner_nid             = pin_entry->get_master_nid() & ~static_cast<Nid>(3);
    pin_port_id           = pin_entry->get_port_id();
  }

  if (owner_nid == INPUT_NODE && !is_node_as_pin) {
    for (const auto& [name, pid] : input_pins_) {
      if (((pid & ~static_cast<Pid>(2)) | static_cast<Pid>(1)) == ((raw_pid & ~static_cast<Pid>(2)) | static_cast<Pid>(1))) {
        return name;
      }
    }
  }
  if (owner_nid == OUTPUT_NODE && !is_node_as_pin) {
    for (const auto& [name, pid] : output_pins_) {
      if (((pid & ~static_cast<Pid>(2)) | static_cast<Pid>(1)) == ((raw_pid & ~static_cast<Pid>(2)) | static_cast<Pid>(1))) {
        return name;
      }
    }
  }

  const auto* owner = ref_node(owner_nid);
  if (owner->has_subnode() && owner_lib_ != nullptr) {
    auto gio = owner_lib_->graph_ios_[static_cast<size_t>(owner->get_subnode())];
    if (gio) {
      if (raw_pid & static_cast<Pid>(2)) {
        for (const auto& decl : gio->output_pin_decls_) {
          if (decl.port_id == pin_port_id) {
            return decl.name;
          }
        }
      } else {
        for (const auto& decl : gio->input_pin_decls_) {
          if (decl.port_id == pin_port_id) {
            return decl.name;
          }
        }
      }
    }
  }

  static const std::string empty;
  return empty;
}

auto Graph::get_input_pin(std::string_view name) const -> Pin_class {
  assert_accessible();
  const auto it = input_pins_.find(std::string(name));
  assert(it != input_pins_.end() && "get_input_pin: declared input name not found");
  if (it == input_pins_.end()) {
    return {};
  }
  return make_pin_class(it->second | static_cast<Pid>(2));
}

auto Graph::get_output_pin(std::string_view name) const -> Pin_class {
  assert_accessible();
  const auto it = output_pins_.find(std::string(name));
  assert(it != output_pins_.end() && "get_output_pin: declared output name not found");
  if (it == output_pins_.end()) {
    return {};
  }
  return make_pin_class(it->second);
}

auto Graph::materialize_declared_io_pin(std::string_view name, Port_id port_id, Nid owner_nid,
                                        ankerl::unordered_dense::map<std::string, Pid>& pins_by_name) -> Pid {
  assert_accessible();
  assert(!name.empty() && "materialize_declared_io_pin: name is required");

  const auto it = pins_by_name.find(std::string(name));
  if (it != pins_by_name.end()) {
    return it->second;
  }

  const Pid pin_pid = create_pin(owner_nid, port_id);
  pins_by_name.emplace(std::string(name), pin_pid);
  return pin_pid;
}

void Graph::erase_declared_io_pin(std::string_view name, ankerl::unordered_dense::map<std::string, Pid>& pins_by_name) {
  assert_accessible();
  const auto it = pins_by_name.find(std::string(name));
  assert(it != pins_by_name.end() && "erase_declared_io_pin: declared pin name not found");
  if (it == pins_by_name.end()) {
    return;
  }

  delete_pin(it->second);
  pins_by_name.erase(it);
}

void Graph::delete_pin(Pid pin_pid) {
  assert_accessible();

  const Pid pin_lookup = (pin_pid & ~static_cast<Pid>(2)) | static_cast<Pid>(1);
  const Pid actual_id  = pin_lookup >> 2;
  assert(actual_id > 0 && actual_id < pin_table.size() && "delete_pin: pin handle is invalid for this graph");

  auto* pin = &pin_table[actual_id];
  assert(pin->get_master_nid() != 0 && "delete_pin: pin already deleted");

  std::vector<Vid> edges_to_remove;
  for (auto edge : pin->get_edges(pin_lookup, overflow_sets_)) {
    edges_to_remove.push_back(edge);
  }

  for (auto other_vid : edges_to_remove) {
    if (other_vid & static_cast<Vid>(2)) {
      // if other vid is source
      const Vid reverse_edge = pin_lookup;
      if (other_vid & static_cast<Vid>(1)) {
        // other is a pin
        auto* other_pin = ref_pin(other_vid);
        if (other_pin->use_overflow) {
          (void)overflow_sets_[other_pin->get_overflow_idx()].erase(reverse_edge);
        } else {
          constexpr int      SHIFT      = 14;
          constexpr uint64_t SLOT       = (1ULL << SHIFT) - 1;
          constexpr uint64_t SIGN_BIT   = 1ULL << 13;
          constexpr uint64_t DRIVER_BIT = 1ULL << 1;
          constexpr uint64_t PIN_BIT    = 1ULL << 0;
          constexpr uint64_t MAG_MASK   = (1ULL << 11) - 1;
          const uint64_t     self_num   = static_cast<uint64_t>(other_vid) >> 2;
          for (int i = 0; i < 4; ++i) {
            const uint64_t raw = (other_pin->sedges_.sedges >> (i * SHIFT)) & SLOT;
            if (raw == 0) {
              continue;
            }

            const bool     neg        = (raw & SIGN_BIT) != 0;
            const bool     driver     = (raw & DRIVER_BIT) != 0;
            const bool     is_pin     = (raw & PIN_BIT) != 0;
            const uint64_t magnitude  = (raw >> 2) & MAG_MASK;
            const int64_t  delta      = neg ? -static_cast<int64_t>(magnitude) : static_cast<int64_t>(magnitude);
            const uint64_t target_num = self_num - delta;
            const Vid      candidate  = static_cast<Vid>((target_num << 2) | (driver ? DRIVER_BIT : 0) | (is_pin ? PIN_BIT : 0));

            if (candidate == reverse_edge) {
              other_pin->sedges_.sedges &= ~(static_cast<int64_t>(SLOT) << (i * SHIFT));
              break;
            }
          }
          if (other_pin->ledge0 == reverse_edge) {
            other_pin->ledge0 = 0;
          } else if (other_pin->ledge1 == reverse_edge) {
            other_pin->ledge1 = 0;
          }
        }
      } else {
        // if other_vid is a node
        auto* other_node = ref_node(other_vid);
        if (other_node->use_overflow) {
          (void)overflow_sets_[other_node->get_overflow_idx()].erase(reverse_edge);
        } else {
          constexpr int      SHIFT      = 14;
          constexpr uint64_t SLOT       = (1ULL << SHIFT) - 1;
          constexpr uint64_t SIGN_BIT   = 1ULL << 13;
          constexpr uint64_t DRIVER_BIT = 1ULL << 1;
          constexpr uint64_t PIN_BIT    = 1ULL << 0;
          constexpr uint64_t MAG_MASK   = (1ULL << 11) - 1;
          const uint64_t     self_num   = static_cast<uint64_t>(other_vid) >> 2;
          for (int i = 0; i < 4; ++i) {
            const uint64_t raw = (other_node->sedges_.sedges >> (i * SHIFT)) & SLOT;
            if (raw == 0) {
              continue;
            }

            const bool     neg        = (raw & SIGN_BIT) != 0;
            const bool     driver     = (raw & DRIVER_BIT) != 0;
            const bool     is_pin     = (raw & PIN_BIT) != 0;
            const uint64_t magnitude  = (raw >> 2) & MAG_MASK;
            const int64_t  delta      = neg ? -static_cast<int64_t>(magnitude) : static_cast<int64_t>(magnitude);
            const uint64_t target_num = self_num - delta;
            const Vid      candidate  = static_cast<Vid>((target_num << 2) | (driver ? DRIVER_BIT : 0) | (is_pin ? PIN_BIT : 0));

            if (candidate == reverse_edge) {
              other_node->sedges_.sedges &= ~(static_cast<int64_t>(SLOT) << (i * SHIFT));
              break;
            }
          }
          if (other_node->ledge0 == reverse_edge) {
            other_node->ledge0 = 0;
          } else if (other_node->ledge1 == reverse_edge) {
            other_node->ledge1 = 0;
          }
        }
      }
    } else {
      // other vid is sink
      const Vid reverse_edge = pin_lookup | static_cast<Pid>(2);
      if (other_vid & static_cast<Vid>(1)) {
        // other is a pin
        auto* other_pin = ref_pin(other_vid);
        if (other_pin->use_overflow) {
          (void)overflow_sets_[other_pin->get_overflow_idx()].erase(reverse_edge);
        } else {
          constexpr int      SHIFT      = 14;
          constexpr uint64_t SLOT       = (1ULL << SHIFT) - 1;
          constexpr uint64_t SIGN_BIT   = 1ULL << 13;
          constexpr uint64_t DRIVER_BIT = 1ULL << 1;
          constexpr uint64_t PIN_BIT    = 1ULL << 0;
          constexpr uint64_t MAG_MASK   = (1ULL << 11) - 1;
          const uint64_t     self_num   = static_cast<uint64_t>(other_vid) >> 2;
          for (int i = 0; i < 4; ++i) {
            const uint64_t raw = (other_pin->sedges_.sedges >> (i * SHIFT)) & SLOT;
            if (raw == 0) {
              continue;
            }

            const bool     neg        = (raw & SIGN_BIT) != 0;
            const bool     driver     = (raw & DRIVER_BIT) != 0;
            const bool     is_pin     = (raw & PIN_BIT) != 0;
            const uint64_t magnitude  = (raw >> 2) & MAG_MASK;
            const int64_t  delta      = neg ? -static_cast<int64_t>(magnitude) : static_cast<int64_t>(magnitude);
            const uint64_t target_num = self_num - delta;
            const Vid      candidate  = static_cast<Vid>((target_num << 2) | (driver ? DRIVER_BIT : 0) | (is_pin ? PIN_BIT : 0));

            if (candidate == reverse_edge) {
              other_pin->sedges_.sedges &= ~(static_cast<int64_t>(SLOT) << (i * SHIFT));
              break;
            }
          }
          if (other_pin->ledge0 == reverse_edge) {
            other_pin->ledge0 = 0;
          } else if (other_pin->ledge1 == reverse_edge) {
            other_pin->ledge1 = 0;
          }
        }
      } else {
        // other is a node
        auto* other_node = ref_node(other_vid);
        if (other_node->use_overflow) {
          (void)overflow_sets_[other_node->get_overflow_idx()].erase(reverse_edge);
        } else {
          constexpr int      SHIFT      = 14;
          constexpr uint64_t SLOT       = (1ULL << SHIFT) - 1;
          constexpr uint64_t SIGN_BIT   = 1ULL << 13;
          constexpr uint64_t DRIVER_BIT = 1ULL << 1;
          constexpr uint64_t PIN_BIT    = 1ULL << 0;
          constexpr uint64_t MAG_MASK   = (1ULL << 11) - 1;
          const uint64_t     self_num   = static_cast<uint64_t>(other_vid) >> 2;
          for (int i = 0; i < 4; ++i) {
            const uint64_t raw = (other_node->sedges_.sedges >> (i * SHIFT)) & SLOT;
            if (raw == 0) {
              continue;
            }

            const bool     neg        = (raw & SIGN_BIT) != 0;
            const bool     driver     = (raw & DRIVER_BIT) != 0;
            const bool     is_pin     = (raw & PIN_BIT) != 0;
            const uint64_t magnitude  = (raw >> 2) & MAG_MASK;
            const int64_t  delta      = neg ? -static_cast<int64_t>(magnitude) : static_cast<int64_t>(magnitude);
            const uint64_t target_num = self_num - delta;
            const Vid      candidate  = static_cast<Vid>((target_num << 2) | (driver ? DRIVER_BIT : 0) | (is_pin ? PIN_BIT : 0));

            if (candidate == reverse_edge) {
              other_node->sedges_.sedges &= ~(static_cast<int64_t>(SLOT) << (i * SHIFT));
              break;
            }
          }
          if (other_node->ledge0 == reverse_edge) {
            other_node->ledge0 = 0;
          } else if (other_node->ledge1 == reverse_edge) {
            other_node->ledge1 = 0;
          }
        }
      }
    }
  }

  const Nid owner_nid = pin->get_master_nid() & ~static_cast<Nid>(2);
  auto*     owner     = ref_node(owner_nid);
  if (owner->get_next_pin_id() == pin_lookup) {
    owner->set_next_pin_id(pin->get_next_pin_id());
  } else {
    Pid current = owner->get_next_pin_id();
    while (current != 0) {
      auto* current_pin = ref_pin(current);
      if (current_pin->get_next_pin_id() == pin_lookup) {
        current_pin->set_next_pin_id(pin->get_next_pin_id());
        break;
      }
      current = current_pin->get_next_pin_id();
    }
  }

  if (pin->check_overflow()) {
    overflow_free_.push_back(pin->get_overflow_idx());
    overflow_sets_[pin->get_overflow_idx()].clear();
  }
  pin_table[actual_id] = PinEntry();
  invalidate_traversal_caches();
}

auto Pin_class::get_master_node() const -> Node_class {
  if (context_ == Handle_context::Flat) {
    return Node_class(graph_, root_gid_, current_gid_, raw_nid);
  }
  if (context_ == Handle_context::Hier) {
    return Node_class(graph_, hier_tid_, hier_gids_, hier_pos_, raw_nid);
  }
  return Node_class(graph_, raw_nid);
}

bool Pin_class::is_valid() const noexcept { return graph_ != nullptr && graph_->is_pin_valid(pin_pid); }

std::string_view Pin_class::get_pin_name() const {
  assert(graph_ != nullptr && "get_pin_name: pin is not attached to a graph");
  return graph_->pin_name(*this);
}

void Pin_class::connect_driver(Pin_class driver_pin) const {
  assert(graph_ != nullptr && "connect_driver: pin is not attached to a graph");
  graph_->add_edge(driver_pin, *this);
}

void Pin_class::connect_driver(Node_class driver_node) const {
  assert(graph_ != nullptr && "connect_driver: pin is not attached to a graph");
  graph_->add_edge(driver_node, *this);
}

void Pin_class::connect_sink(Pin_class sink_pin) const {
  assert(graph_ != nullptr && "connect_sink: pin is not attached to a graph");
  graph_->add_edge(*this, sink_pin);
}

void Pin_class::connect_sink(Node_class sink_node) const {
  assert(graph_ != nullptr && "connect_sink: pin is not attached to a graph");
  graph_->add_edge(*this, sink_node);
}

void Pin_class::del_sink(Pin_class driver_pin) const {
  assert(graph_ != nullptr && "del_sink: pin is not attached to a graph");
  graph_->del_edge(driver_pin, *this);
}

void Pin_class::del_sink() const {
  assert(graph_ != nullptr && "del_sink: pin is not attached to a graph");
  auto edges = graph_->inp_edges(*this);
  for (const auto& edge : edges) {
    if (edge.driver_pin().is_valid()) {
      graph_->del_edge(edge.driver_pin(), edge.sink_pin());
    } else {
      graph_->del_edge(edge.driver_node(), edge.sink_pin());
    }
  }
}

void Pin_class::del_driver() const {
  assert(graph_ != nullptr && "del_driver: pin is not attached to a graph");
  auto edges = graph_->out_edges(*this);
  for (const auto& edge : edges) {
    if (edge.sink_pin().is_valid()) {
      graph_->del_edge(edge.driver_pin(), edge.sink_pin());
    } else {
      graph_->del_edge(edge.driver_pin(), edge.sink_node());
    }
  }
}

void Pin_class::del_node() const {
  assert(graph_ != nullptr && "del_node: pin is not attached to a graph");
  graph_->delete_node(raw_nid);
}

auto Pin_class::out_edges() const -> std::vector<Edge_class> {
  assert(graph_ != nullptr && "out_edges: pin is not attached to a graph");
  return graph_->out_edges(*this);
}

auto Pin_class::inp_edges() const -> std::vector<Edge_class> {
  assert(graph_ != nullptr && "inp_edges: pin is not attached to a graph");
  return graph_->inp_edges(*this);
}

bool Node_class::is_valid() const noexcept { return graph_ != nullptr && graph_->is_node_valid(raw_nid); }

void Node_class::set_subnode(const std::shared_ptr<GraphIO>& graphio) const {
  assert(graph_ != nullptr && "set_subnode: node is not attached to a graph");
  assert(graphio != nullptr && "set_subnode: null GraphIO");
  graph_->set_subnode(*this, graphio->get_gid());
}

void Node_class::set_type(Type type) const {
  assert(graph_ != nullptr && "set_type: node is not attached to a graph");
  graph_->ref_node(raw_nid)->set_type(type);
}

auto Node_class::create_driver_pin() const -> Pin_class { return create_driver_pin(0); }
auto Node_class::create_driver_pin(Port_id port_id) const -> Pin_class {
  assert(graph_ != nullptr && "create_driver_pin: node is not attached to a graph");
  if (port_id == 0) {
    // Node itself acts as driver pin(0)
    const Nid nid = raw_nid & ~static_cast<Nid>(2);
    auto      pin = Pin_class(graph_, nid, 0, nid | static_cast<Pid>(2));
    inherit_pin_context(pin, *this);
    return pin;
  }
  auto pin = graph_->find_or_create_pin(*this, port_id);
  pin.pin_pid |= static_cast<Pid>(2);
  inherit_pin_context(pin, *this);
  return pin;
}
auto Node_class::create_driver_pin(std::string_view name) const -> Pin_class {
  assert(graph_ != nullptr && "create_driver_pin: node is not attached to a graph");
  return create_driver_pin(graph_->resolve_driver_port(*this, name));
}
auto Node_class::create_sink_pin() const -> Pin_class { return create_sink_pin(0); }
auto Node_class::create_sink_pin(Port_id port_id) const -> Pin_class {
  assert(graph_ != nullptr && "create_sink_pin: node is not attached to a graph");
  if (port_id == 0) {
    // Node itself acts as sink pin(0)
    const Nid nid = raw_nid & ~static_cast<Nid>(2);
    auto      pin = Pin_class(graph_, nid, 0, nid);
    inherit_pin_context(pin, *this);
    return pin;
  }
  auto pin = graph_->find_or_create_pin(*this, port_id);
  pin.pin_pid &= ~static_cast<Pid>(2);
  inherit_pin_context(pin, *this);
  return pin;
}
auto Node_class::create_sink_pin(std::string_view name) const -> Pin_class {
  assert(graph_ != nullptr && "create_sink_pin: node is not attached to a graph");
  return create_sink_pin(graph_->resolve_sink_port(*this, name));
}

auto Node_class::get_driver_pin(Port_id port_id) const -> Pin_class {
  assert(graph_ != nullptr && "get_driver_pin: node is not attached to a graph");
  auto pin = graph_->find_pin(*this, port_id, true);
  pin.pin_pid |= static_cast<Pid>(2);
  inherit_pin_context(pin, *this);
  return pin;
}
auto Node_class::get_driver_pin(std::string_view name) const -> Pin_class {
  assert(graph_ != nullptr && "get_driver_pin: node is not attached to a graph");
  return get_driver_pin(graph_->resolve_driver_port(*this, name));
}
auto Node_class::get_sink_pin(Port_id port_id) const -> Pin_class {
  assert(graph_ != nullptr && "get_sink_pin: node is not attached to a graph");
  auto pin = graph_->find_pin(*this, port_id, false);
  inherit_pin_context(pin, *this);
  return pin;
}
auto Node_class::get_sink_pin(std::string_view name) const -> Pin_class {
  assert(graph_ != nullptr && "get_sink_pin: node is not attached to a graph");
  return get_sink_pin(graph_->resolve_sink_port(*this, name));
}

void Node_class::connect_driver(Pin_class driver_pin) const {
  assert(graph_ != nullptr && "connect_driver: node is not attached to a graph");
  graph_->add_edge(driver_pin, *this);
}

void Node_class::connect_driver(Node_class driver_node) const {
  assert(graph_ != nullptr && "connect_driver: node is not attached to a graph");
  graph_->add_edge(driver_node, *this);
}

void Node_class::connect_sink(Pin_class sink_pin) const {
  assert(graph_ != nullptr && "connect_sink: node is not attached to a graph");
  graph_->add_edge(*this, sink_pin);
}

void Node_class::connect_sink(Node_class sink_node) const {
  assert(graph_ != nullptr && "connect_sink: node is not attached to a graph");
  graph_->add_edge(*this, sink_node);
}

void Node_class::del_node() const {
  assert(graph_ != nullptr && "del_node: node is not attached to a graph");
  graph_->delete_node(raw_nid);
}

auto Node_class::out_edges() const -> std::vector<Edge_class> {
  assert(graph_ != nullptr && "out_edges: node is not attached to a graph");
  return graph_->out_edges(*this);
}

auto Node_class::inp_edges() const -> std::vector<Edge_class> {
  assert(graph_ != nullptr && "inp_edges: node is not attached to a graph");
  return graph_->inp_edges(*this);
}

auto Node_class::out_pins() const -> std::vector<Pin_class> {
  assert(graph_ != nullptr && "out_pins: node is not attached to a graph");
  return graph_->get_driver_pins(*this);
}

auto Node_class::inp_pins() const -> std::vector<Pin_class> {
  assert(graph_ != nullptr && "inp_pins: node is not attached to a graph");
  return graph_->get_sink_pins(*this);
}

void Graph::set_subnode(Node_class node, Gid gid) {
  assert_node_exists(node);
  set_subnode(node.get_raw_nid(), gid);
}

void Graph::set_subnode(Nid nid, Gid gid) {
  assert_accessible();
  if (gid == Gid_invalid) {
    return;
  }

  if (owner_lib_ != nullptr) {
    const size_t idx = static_cast<size_t>(gid);
    assert(idx < owner_lib_->graph_ios_.size() && owner_lib_->graph_ios_[idx] != nullptr);
    if (idx >= owner_lib_->graph_ios_.size() || owner_lib_->graph_ios_[idx] == nullptr) {
      return;
    }
  }

  nid &= ~static_cast<Nid>(3);
  auto pool = get_overflow_pool();
  ref_node(nid)->set_subnode(gid, pool);
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
  assert_node_exists(node1);
  assert_node_exists(node2);
  del_edge_int(node1.get_raw_nid(), node2.get_raw_nid());
  invalidate_traversal_caches();
}

void Graph::del_edge(Node_class node, Pin_class pin) {
  assert_accessible();
  assert_node_exists(node);
  assert_pin_exists(pin);
  del_edge_int(node.get_raw_nid(), pin.get_pin_pid());
  invalidate_traversal_caches();
}

void Graph::del_edge(Pin_class pin, Node_class node) {
  assert_accessible();
  assert_pin_exists(pin);
  assert_node_exists(node);
  del_edge_int(pin.get_pin_pid(), node.get_raw_nid());
  invalidate_traversal_caches();
}

void Graph::del_edge(Pin_class pin1, Pin_class pin2) {
  assert_accessible();
  assert_pin_exists(pin1);
  assert_pin_exists(pin2);
  del_edge_int(pin1.get_pin_pid(), pin2.get_pin_pid());
  invalidate_traversal_caches();
}

auto Graph::fast_class() const -> std::span<const Node> {
  assert_accessible();
  if (!fast_class_cache_valid_) {
    rebuild_fast_class_cache();
  }
  return std::span<const Node>(fast_class_cache_.data(), fast_class_cache_.size());
}

auto Graph::forward_class() const -> std::span<const Node> {
  assert_accessible();
  if (!forward_class_cache_valid_) {
    rebuild_forward_class_cache();
  }
  return std::span<const Node>(forward_class_cache_.data(), forward_class_cache_.size());
}

auto Graph::fast_flat() const -> std::span<const Node> {
  assert_accessible();
  rebuild_fast_flat_cache();
  return std::span<const Node>(fast_flat_cache_.data(), fast_flat_cache_.size());
}

auto Graph::fast_hier() const -> std::span<const Node> {
  assert_accessible();
  const uint64_t expected_epoch = owner_lib_ != nullptr ? owner_lib_->mutation_epoch() : 0;
  if (!fast_hier_cache_valid_ || (owner_lib_ != nullptr && fast_hier_cache_epoch_ != expected_epoch)) {
    rebuild_fast_hier_cache();
  }
  return std::span<const Node>(fast_hier_cache_.data(), fast_hier_cache_.size());
}

auto Graph::forward_flat() const -> std::span<const Node> {
  assert_accessible();
  const uint64_t expected_epoch = owner_lib_ != nullptr ? owner_lib_->mutation_epoch() : 0;
  if (!forward_flat_cache_valid_ || (owner_lib_ != nullptr && forward_flat_cache_epoch_ != expected_epoch)) {
    rebuild_forward_flat_cache();
  }
  return std::span<const Node>(forward_flat_cache_.data(), forward_flat_cache_.size());
}

auto Graph::forward_hier() const -> std::span<const Node> {
  assert_accessible();
  const uint64_t expected_epoch = owner_lib_ != nullptr ? owner_lib_->mutation_epoch() : 0;
  if (!forward_hier_cache_valid_ || (owner_lib_ != nullptr && forward_hier_cache_epoch_ != expected_epoch)) {
    rebuild_forward_hier_cache();
  }
  return std::span<const Node>(forward_hier_cache_.data(), forward_hier_cache_.size());
}

void Graph::del_edge_int(Vid driver_id, Vid sink_id) {
  auto pool = get_overflow_pool();
  if (driver_id & 1) {
    (void)ref_pin(driver_id)->delete_edge(driver_id, sink_id, pool);
  } else {
    (void)ref_node(driver_id)->delete_edge(driver_id, sink_id, pool);
  }

  if (sink_id & 1) {
    (void)ref_pin(sink_id)->delete_edge(sink_id, driver_id, pool);
  } else {
    (void)ref_node(sink_id)->delete_edge(sink_id, driver_id, pool);
  }
}

auto Graph::out_edges(Node_class node) -> std::vector<Edge_class> {
  assert_accessible();
  assert_node_exists(node);
  std::vector<Edge_class> out;
  const Nid               self_nid = node.get_raw_nid() & ~static_cast<Nid>(2);
  auto*                   self     = ref_node(self_nid);
  auto                    edges    = self->get_edges(self_nid, overflow_sets_);
  const Node_class        self_driver(this, self_nid | static_cast<Nid>(2));

  for (auto vid : edges) {
    if (vid & 2) {
      continue;
    }
    if (vid & 1) {
      const Pid sink_pid = static_cast<Pid>(vid);

      Edge_class e{};
      e.type      = 3;  // n -> p
      e.driver_   = self_driver;
      e.sink_pin_ = make_pin_class(sink_pid);
      inherit_pin_context(e.sink_pin_, node);
      out.push_back(e);
      continue;
    } else {
      const Nid sink_nid = static_cast<Nid>(vid);

      Edge_class e{};
      e.type    = 1;  // n -> n
      e.driver_ = self_driver;
      e.sink_   = Node_class(this, sink_nid);
      out.push_back(e);
    }
  }
  return out;
}

auto Graph::inp_edges(Node_class node) -> std::vector<Edge_class> {
  assert_accessible();
  assert_node_exists(node);
  std::vector<Edge_class> out;
  const Nid               self_nid = node.get_raw_nid() & ~static_cast<Nid>(2);
  auto*                   self     = ref_node(self_nid);
  auto                    edges    = self->get_edges(self_nid, overflow_sets_);
  const Node_class        self_sink(this, self_nid & ~static_cast<Nid>(2));

  for (auto vid : edges) {
    if (!(vid & 2)) {
      continue;
    }
    if (vid & 1) {
      const Pid driver_pid = static_cast<Pid>(vid);

      Edge_class e{};
      e.type        = 4;  // p -> n
      e.driver_pin_ = make_pin_class(driver_pid);
      inherit_pin_context(e.driver_pin_, node);
      e.sink_ = self_sink;
      out.push_back(e);
      continue;
    } else {
      const Nid driver_nid = static_cast<Nid>(vid);

      Edge_class e{};
      e.type    = 1;  // n -> n
      e.driver_ = Node_class(this, driver_nid);
      e.sink_   = self_sink;
      out.push_back(e);
    }
  }
  return out;
}

auto Graph::out_edges(Pin_class pin) -> std::vector<Edge_class> {
  assert_accessible();
  assert_pin_exists(pin);

  // port_id == 0: read edges from NodeEntry, build pin-aware results
  if (!(pin.get_pin_pid() & static_cast<Pid>(1))) {
    const Nid self_nid = pin.get_pin_pid() & ~static_cast<Nid>(2);
    auto*     self     = ref_node(self_nid);
    auto      edges    = self->get_edges(self_nid, overflow_sets_);
    Pin_class self_driver_pin(this, self_nid, 0, self_nid | static_cast<Pid>(2));
    self_driver_pin.context_     = pin.context_;
    self_driver_pin.root_gid_    = pin.root_gid_;
    self_driver_pin.current_gid_ = pin.current_gid_;
    self_driver_pin.hier_gids_   = pin.hier_gids_;
    self_driver_pin.hier_tid_    = pin.hier_tid_;
    self_driver_pin.hier_pos_    = pin.hier_pos_;
    Node_class self_driver(this, self_nid | static_cast<Nid>(2));

    std::vector<Edge_class> out;
    for (auto vid : edges) {
      if (vid & 2) {
        continue;  // skip back edges
      }
      if (vid & 1) {
        Edge_class e{};
        e.type                   = 2;  // p -> p
        e.driver_                = self_driver;
        e.driver_pin_            = self_driver_pin;
        e.sink_pin_              = make_pin_class(static_cast<Pid>(vid));
        e.sink_pin_.context_     = pin.context_;
        e.sink_pin_.root_gid_    = pin.root_gid_;
        e.sink_pin_.current_gid_ = pin.current_gid_;
        e.sink_pin_.hier_gids_   = pin.hier_gids_;
        e.sink_pin_.hier_tid_    = pin.hier_tid_;
        e.sink_pin_.hier_pos_    = pin.hier_pos_;
        out.push_back(e);
      } else {
        const Nid  sink_nid = static_cast<Nid>(vid);
        Edge_class e{};
        e.type                   = 1;  // n -> n
        e.driver_                = self_driver;
        e.driver_pin_            = self_driver_pin;
        e.sink_                  = Node_class(this, sink_nid);
        e.sink_pin_              = Pin_class(this, sink_nid, 0, sink_nid);
        e.sink_pin_.context_     = pin.context_;
        e.sink_pin_.root_gid_    = pin.root_gid_;
        e.sink_pin_.current_gid_ = pin.current_gid_;
        e.sink_pin_.hier_gids_   = pin.hier_gids_;
        e.sink_pin_.hier_tid_    = pin.hier_tid_;
        e.sink_pin_.hier_pos_    = pin.hier_pos_;
        out.push_back(e);
      }
    }
    return out;
  }

  std::vector<Edge_class> out;
  const Pid               self_pid           = pin.get_pin_pid();
  const Pid               self_pid_lookup    = (self_pid & ~static_cast<Pid>(2)) | static_cast<Pid>(1);
  auto*                   self               = ref_pin(self_pid_lookup);
  auto                    edges              = self->get_edges(self_pid_lookup, overflow_sets_);
  const Pin_class         self_driver_pin    = make_pin_class(self_pid_lookup | static_cast<Pid>(2));
  Pin_class               context_driver_pin = self_driver_pin;
  context_driver_pin.context_                = pin.context_;
  context_driver_pin.root_gid_               = pin.root_gid_;
  context_driver_pin.current_gid_            = pin.current_gid_;
  context_driver_pin.hier_gids_              = pin.hier_gids_;
  context_driver_pin.hier_tid_               = pin.hier_tid_;
  context_driver_pin.hier_pos_               = pin.hier_pos_;

  for (auto vid : edges) {
    if (vid & 2) {
      continue;
    }
    if (vid & 1) {
      const Pid sink_pid = static_cast<Pid>(vid);

      Edge_class e{};
      e.type                   = 2;  // p -> p
      e.driver_pin_            = context_driver_pin;
      e.sink_pin_              = make_pin_class(sink_pid);
      e.sink_pin_.context_     = pin.context_;
      e.sink_pin_.root_gid_    = pin.root_gid_;
      e.sink_pin_.current_gid_ = pin.current_gid_;
      e.sink_pin_.hier_gids_   = pin.hier_gids_;
      e.sink_pin_.hier_tid_    = pin.hier_tid_;
      e.sink_pin_.hier_pos_    = pin.hier_pos_;
      out.push_back(e);
      continue;
    }

    const Nid sink_nid = static_cast<Nid>(vid);

    Edge_class e{};
    e.type        = 4;  // p -> n
    e.driver_pin_ = context_driver_pin;
    e.sink_       = Node_class(this, sink_nid);
    out.push_back(e);
  }

  return out;
}

auto Graph::inp_edges(Pin_class pin) -> std::vector<Edge_class> {
  assert_accessible();
  assert_pin_exists(pin);

  // port_id == 0: read edges from NodeEntry, build pin-aware results
  if (!(pin.get_pin_pid() & static_cast<Pid>(1))) {
    const Nid self_nid = pin.get_pin_pid() & ~static_cast<Nid>(2);
    auto*     self     = ref_node(self_nid);
    auto      edges    = self->get_edges(self_nid, overflow_sets_);
    Pin_class self_sink_pin(this, self_nid, 0, self_nid);
    self_sink_pin.context_     = pin.context_;
    self_sink_pin.root_gid_    = pin.root_gid_;
    self_sink_pin.current_gid_ = pin.current_gid_;
    self_sink_pin.hier_gids_   = pin.hier_gids_;
    self_sink_pin.hier_tid_    = pin.hier_tid_;
    self_sink_pin.hier_pos_    = pin.hier_pos_;
    Node_class self_sink(this, self_nid & ~static_cast<Nid>(2));

    std::vector<Edge_class> out;
    for (auto vid : edges) {
      if (!(vid & 2)) {
        continue;  // skip local/forward edges
      }
      if (vid & 1) {
        Edge_class e{};
        e.type                     = 2;  // p -> p
        e.driver_pin_              = make_pin_class(static_cast<Pid>(vid));
        e.driver_pin_.context_     = pin.context_;
        e.driver_pin_.root_gid_    = pin.root_gid_;
        e.driver_pin_.current_gid_ = pin.current_gid_;
        e.driver_pin_.hier_gids_   = pin.hier_gids_;
        e.driver_pin_.hier_tid_    = pin.hier_tid_;
        e.driver_pin_.hier_pos_    = pin.hier_pos_;
        e.sink_                    = self_sink;
        e.sink_pin_                = self_sink_pin;
        out.push_back(e);
      } else {
        const Nid  driver_nid = static_cast<Nid>(vid);
        Edge_class e{};
        e.type                     = 1;  // n -> n
        e.driver_                  = Node_class(this, driver_nid);
        e.driver_pin_              = Pin_class(this, driver_nid & ~static_cast<Nid>(2), 0, driver_nid);
        e.driver_pin_.context_     = pin.context_;
        e.driver_pin_.root_gid_    = pin.root_gid_;
        e.driver_pin_.current_gid_ = pin.current_gid_;
        e.driver_pin_.hier_gids_   = pin.hier_gids_;
        e.driver_pin_.hier_tid_    = pin.hier_tid_;
        e.driver_pin_.hier_pos_    = pin.hier_pos_;
        e.sink_                    = self_sink;
        e.sink_pin_                = self_sink_pin;
        out.push_back(e);
      }
    }
    return out;
  }

  std::vector<Edge_class> out;
  const Pid               self_pid         = pin.get_pin_pid();
  const Pid               self_pid_sink    = (self_pid & ~static_cast<Pid>(2)) | static_cast<Pid>(1);
  auto*                   self             = ref_pin(self_pid_sink);
  auto                    edges            = self->get_edges(self_pid_sink, overflow_sets_);
  const Pin_class         self_sink_pin    = make_pin_class(self_pid_sink);
  Pin_class               context_sink_pin = self_sink_pin;
  context_sink_pin.context_                = pin.context_;
  context_sink_pin.root_gid_               = pin.root_gid_;
  context_sink_pin.current_gid_            = pin.current_gid_;
  context_sink_pin.hier_gids_              = pin.hier_gids_;
  context_sink_pin.hier_tid_               = pin.hier_tid_;
  context_sink_pin.hier_pos_               = pin.hier_pos_;

  for (auto vid : edges) {
    if (!(vid & 2)) {
      continue;
    }
    if (vid & 1) {
      const Pid driver_pid = static_cast<Pid>(vid);

      Edge_class e{};
      e.type                     = 2;  // p -> p
      e.driver_pin_              = make_pin_class(driver_pid);
      e.driver_pin_.context_     = pin.context_;
      e.driver_pin_.root_gid_    = pin.root_gid_;
      e.driver_pin_.current_gid_ = pin.current_gid_;
      e.driver_pin_.hier_gids_   = pin.hier_gids_;
      e.driver_pin_.hier_tid_    = pin.hier_tid_;
      e.driver_pin_.hier_pos_    = pin.hier_pos_;
      e.sink_pin_                = context_sink_pin;
      out.push_back(e);
      continue;
    }

    const Nid driver_nid = static_cast<Nid>(vid);

    Edge_class e{};
    e.type      = 3;  // n -> p
    e.driver_   = Node_class(this, driver_nid);
    e.sink_pin_ = context_sink_pin;
    out.push_back(e);
  }

  return out;
}

auto Graph::get_pins(Node_class node) -> std::vector<Pin_class> {
  assert_accessible();
  assert_node_exists(node);
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
  assert_node_exists(node);
  std::vector<Pin_class> out;
  for (const auto& pin : get_pins(node)) {
    const Pid pid_lookup = (pin.get_pin_pid() & ~static_cast<Pid>(2)) | static_cast<Pid>(1);
    auto*     self       = ref_pin(pid_lookup);
    auto      edges      = self->get_edges(pid_lookup, overflow_sets_);

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
  assert_node_exists(node);
  std::vector<Pin_class> out;
  for (const auto& pin : get_pins(node)) {
    const Pid pid_lookup = (pin.get_pin_pid() & ~static_cast<Pid>(2)) | static_cast<Pid>(1);
    auto*     self       = ref_pin(pid_lookup);
    auto      edges      = self->get_edges(pid_lookup, overflow_sets_);

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
  nid &= ~static_cast<Nid>(3);
  const Nid actual_id = nid >> 2;
  assert(actual_id >= 4 && "delete_node: built-in graph IO nodes cannot be deleted through node handles");
  assert(actual_id < node_table.size() && node_table[actual_id].get_nid() != 0 && "delete_node: node handle is invalid");

  auto* node = ref_node(nid);

  std::vector<Pid> pins_to_delete;
  for (Pid cur_pin = node->get_next_pin_id(); cur_pin != 0;) {
    const Pid canonical_pin = (cur_pin & ~static_cast<Pid>(2)) | static_cast<Pid>(1);
    pins_to_delete.push_back(canonical_pin);
    cur_pin = ref_pin(canonical_pin)->get_next_pin_id();
  }

  std::vector<std::pair<Vid, Vid>> edges_to_remove;
  for (auto edge : node->get_edges(nid, overflow_sets_)) {
    if (edge & static_cast<Vid>(2)) {
      edges_to_remove.emplace_back(edge, nid);
    } else {
      edges_to_remove.emplace_back(nid | static_cast<Nid>(2), edge);
    }
  }
  for (auto pin_pid : pins_to_delete) {
    for (auto edge : ref_pin(pin_pid)->get_edges(pin_pid, overflow_sets_)) {
      if (edge & static_cast<Vid>(2)) {
        edges_to_remove.emplace_back(edge, pin_pid);
      } else {
        edges_to_remove.emplace_back(pin_pid | static_cast<Pid>(2), edge);
      }
    }
  }

  for (const auto& [driver, sink] : edges_to_remove) {
    del_edge_int(driver, sink);
  }

  for (auto pin_pid : pins_to_delete) {
    const Pid actual_pin_id = pin_pid >> 2;
    auto*     pin           = &pin_table[actual_pin_id];
    if (pin->use_overflow) {
      overflow_free_.push_back(pin->get_overflow_idx());
      overflow_sets_[pin->get_overflow_idx()].clear();
    }
    pin_table[actual_pin_id] = PinEntry();
  }

  if (node->use_overflow) {
    overflow_free_.push_back(node->get_overflow_idx());
    overflow_sets_[node->get_overflow_idx()].clear();
  }
  node_table[actual_id] = NodeEntry();
  invalidate_traversal_caches();
}

auto Graph::ref_node(Nid id) const -> NodeEntry* {
  assert_accessible();
  Nid actual_id = id >> 2;
  assert(actual_id < node_table.size());
  return (NodeEntry*)&node_table[actual_id];
}

auto Graph::ref_pin(Pid id) const -> PinEntry* {
  assert_accessible();
  Pid actual_id = id >> 2;
  assert(actual_id < pin_table.size());
  return (PinEntry*)&pin_table[actual_id];
}

void Graph::add_edge_int(Vid self_id, Vid other_id) {
  auto pool = get_overflow_pool();
  // detect type of self_id and other_id
  bool self_type = false;
  if (self_id & 1) {
    // self is pin
    self_type = true;
  }
  if (!self_type) {
    auto* node = ref_node(self_id);
    if (!node->add_edge(self_id, other_id, pool)) {
      std::cerr << "Error: NodeEntry " << node->get_nid() << " overflowed edges while adding edge from " << self_id << " to "
                << other_id << "\n";
    }
  } else {
    auto* pin = ref_pin(self_id);
    if (!pin->add_edge(self_id, other_id, pool)) {
      std::cerr << "Error: PinEntry " << pin->get_master_nid() << ":" << pin->get_port_id()
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
    std::cout << "PinEntry " << pid << "  node=" << p->get_master_nid() << " port=" << p->get_port_id() << "\n";
    if (p->has_edges()) {
      auto sed = p->get_edges(pid, overflow_sets_);
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
    std::cout << "NodeEntry " << nid << " first_pin=" << node_table[nid].get_next_pin_id() << "\n";
  }
}

// --------------------------------------------------------------------------
// Binary persistence
// --------------------------------------------------------------------------

static constexpr uint32_t GRAPH_BODY_MAGIC   = 0x48484742;  // "HHGB"
static constexpr uint32_t GRAPH_BODY_VERSION = 1;
static constexpr uint32_t ENDIAN_CHECK       = 0x01020304;

void Graph::save_body(const std::string& dir_path) const {
  namespace fs = std::filesystem;
  fs::create_directories(dir_path);

  // --- body.bin ---
  {
    const auto   path = fs::path(dir_path) / "body.bin";
    std::ofstream ofs(path, std::ios::binary);
    assert(ofs.good() && "save_body: cannot open body.bin for writing");

    const uint64_t node_count     = node_table.size();
    const uint64_t pin_count      = pin_table.size();
    const uint64_t overflow_count = overflow_sets_.size();

    ofs.write(reinterpret_cast<const char*>(&GRAPH_BODY_MAGIC), sizeof(GRAPH_BODY_MAGIC));
    ofs.write(reinterpret_cast<const char*>(&GRAPH_BODY_VERSION), sizeof(GRAPH_BODY_VERSION));
    ofs.write(reinterpret_cast<const char*>(&ENDIAN_CHECK), sizeof(ENDIAN_CHECK));
    ofs.write(reinterpret_cast<const char*>(&node_count), sizeof(node_count));
    ofs.write(reinterpret_cast<const char*>(&pin_count), sizeof(pin_count));
    ofs.write(reinterpret_cast<const char*>(&overflow_count), sizeof(overflow_count));

    // Bulk write node_table and pin_table — pointer-free POD arrays.
    ofs.write(reinterpret_cast<const char*>(node_table.data()), static_cast<std::streamsize>(node_count * sizeof(NodeEntry)));
    ofs.write(reinterpret_cast<const char*>(pin_table.data()), static_cast<std::streamsize>(pin_count * sizeof(PinEntry)));
  }

  // --- overflow_<idx>.bin (one per overflow set) ---
  for (uint32_t i = 0; i < overflow_sets_.size(); ++i) {
    const auto& oset = overflow_sets_[i];
    const auto  path = fs::path(dir_path) / ("overflow_" + std::to_string(i) + ".bin");
    std::ofstream ofs(path, std::ios::binary);
    assert(ofs.good() && "save_body: cannot open overflow file for writing");

    // Use the values() API — contiguous Vid vector, no bucket data needed.
    const auto&    vals = oset.values();
    const uint64_t count = vals.size();
    ofs.write(reinterpret_cast<const char*>(&count), sizeof(count));
    if (count > 0) {
      ofs.write(reinterpret_cast<const char*>(vals.data()), static_cast<std::streamsize>(count * sizeof(Vid)));
    }
  }

  dirty_ = false;
}

void Graph::load_body(const std::string& dir_path) {
  namespace fs = std::filesystem;

  // --- body.bin ---
  {
    const auto   path = fs::path(dir_path) / "body.bin";
    std::ifstream ifs(path, std::ios::binary);
    assert(ifs.good() && "load_body: cannot open body.bin for reading");

    uint32_t magic = 0, version = 0, endian = 0;
    ifs.read(reinterpret_cast<char*>(&magic), sizeof(magic));
    ifs.read(reinterpret_cast<char*>(&version), sizeof(version));
    ifs.read(reinterpret_cast<char*>(&endian), sizeof(endian));
    assert(magic == GRAPH_BODY_MAGIC && "load_body: bad magic");
    assert(version == GRAPH_BODY_VERSION && "load_body: unsupported version");
    assert(endian == ENDIAN_CHECK && "load_body: endian mismatch — file from different platform");

    uint64_t node_count = 0, pin_count = 0, overflow_count = 0;
    ifs.read(reinterpret_cast<char*>(&node_count), sizeof(node_count));
    ifs.read(reinterpret_cast<char*>(&pin_count), sizeof(pin_count));
    ifs.read(reinterpret_cast<char*>(&overflow_count), sizeof(overflow_count));

    // Bulk read node_table and pin_table.
    node_table.resize(node_count);
    ifs.read(reinterpret_cast<char*>(node_table.data()), static_cast<std::streamsize>(node_count * sizeof(NodeEntry)));

    pin_table.resize(pin_count);
    ifs.read(reinterpret_cast<char*>(pin_table.data()), static_cast<std::streamsize>(pin_count * sizeof(PinEntry)));

    // Prepare overflow_sets_ vector.
    overflow_sets_.resize(overflow_count);
  }

  // --- overflow_<idx>.bin ---
  for (uint32_t i = 0; i < overflow_sets_.size(); ++i) {
    const auto path = std::filesystem::path(dir_path) / ("overflow_" + std::to_string(i) + ".bin");
    std::ifstream ifs(path, std::ios::binary);
    assert(ifs.good() && "load_body: cannot open overflow file for reading");

    uint64_t count = 0;
    ifs.read(reinterpret_cast<char*>(&count), sizeof(count));
    if (count > 0) {
      std::vector<Vid> vals(count);
      ifs.read(reinterpret_cast<char*>(vals.data()), static_cast<std::streamsize>(count * sizeof(Vid)));
      overflow_sets_[i].replace(std::move(vals));
    }
  }

  invalidate_traversal_caches();
  dirty_ = false;
}

// --------------------------------------------------------------------------
// GraphLibrary persistence
// --------------------------------------------------------------------------

void GraphLibrary::save(const std::string& db_path) const {
  namespace fs = std::filesystem;
  fs::create_directories(db_path);

  // --- library.txt (declarations, text format) ---
  {
    std::ofstream ofs(fs::path(db_path) / "library.txt");
    assert(ofs.good() && "GraphLibrary::save: cannot open library.txt");
    ofs << "hhds_graphlib 1\n";
    for (size_t i = 1; i < graph_ios_.size(); ++i) {
      const auto& gio = graph_ios_[i];
      if (!gio) {
        continue;
      }
      ofs << "graph_io " << i << " " << gio->get_name() << "\n";
      for (const auto& pin : gio->input_pin_decls_) {
        ofs << "  input " << pin.port_id << " " << pin.name;
        if (pin.loop_last) {
          ofs << " loop_last";
        }
        ofs << "\n";
      }
      for (const auto& pin : gio->output_pin_decls_) {
        ofs << "  output " << pin.port_id << " " << pin.name;
        if (pin.loop_last) {
          ofs << " loop_last";
        }
        ofs << "\n";
      }
    }
  }

  // --- graph body directories ---
  for (size_t i = 1; i < graphs_.size(); ++i) {
    if (!graphs_[i] || graphs_[i]->deleted_) {
      continue;
    }
    if (!graphs_[i]->dirty_) {
      continue;
    }
    const auto dir = fs::path(db_path) / ("graph_" + std::to_string(i));
    graphs_[i]->save_body(dir.string());
  }
}

void GraphLibrary::load(const std::string& db_path) {
  namespace fs = std::filesystem;

  // Clear current state.
  for (auto& g : graphs_) {
    if (g) {
      g->invalidate_from_library();
    }
  }
  graph_ios_.clear();
  graphs_.clear();
  graph_name_to_id_.clear();
  live_count_     = 0;
  mutation_epoch_ = 1;

  // Reserve slot 0.
  graph_ios_.push_back(nullptr);
  graphs_.push_back(nullptr);

  // --- Parse library.txt ---
  {
    std::ifstream ifs(fs::path(db_path) / "library.txt");
    assert(ifs.good() && "GraphLibrary::load: cannot open library.txt");

    std::string line;
    std::getline(ifs, line);  // header: "hhds_graphlib 1"

    std::shared_ptr<GraphIO> current_gio;
    while (std::getline(ifs, line)) {
      if (line.empty()) {
        continue;
      }
      if (line.substr(0, 9) == "graph_io ") {
        // "graph_io <gid> <name>"
        std::istringstream ss(line.substr(9));
        Gid         gid;
        std::string name;
        ss >> gid >> name;
        current_gio = create_io_impl(gid, name);
      } else if (line.size() > 2 && line[0] == ' ' && line[1] == ' ') {
        assert(current_gio && "GraphLibrary::load: pin decl without graph_io");
        std::istringstream ss(line.substr(2));
        std::string        direction;
        Port_id            port_id;
        std::string        name;
        ss >> direction >> port_id >> name;
        std::string rest;
        bool        loop_last = false;
        if (ss >> rest && rest == "loop_last") {
          loop_last = true;
        }
        if (direction == "input") {
          current_gio->add_input(name, port_id, loop_last);
        } else {
          current_gio->add_output(name, port_id, loop_last);
        }
      }
    }
  }

  // --- Load graph bodies ---
  for (size_t i = 1; i < graph_ios_.size(); ++i) {
    const auto& gio = graph_ios_[i];
    if (!gio) {
      continue;
    }
    const auto dir = fs::path(db_path) / ("graph_" + std::to_string(i));
    if (fs::exists(dir / "body.bin")) {
      auto graph = gio->create_graph();
      graph->load_body(dir.string());
    }
  }
}

}  // namespace hhds
