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

auto Node_class::get_root_gid() const noexcept -> Gid {
  if (context_ == Context::Flat || context_ == Context::Hier) {
    return root_gid_;
  }
  return graph_ != nullptr ? graph_->get_gid() : Gid_invalid;
}

auto Node_class::get_current_gid() const noexcept -> Gid {
  return graph_ != nullptr ? graph_->get_gid() : Gid_invalid;
}

auto Pin_class::get_debug_nid() const noexcept -> Nid {
  // Port-0 pins (node-as-pin): pin_pid & 1 == 0, and pin_pid encodes the
  // master nid directly (bit 2 = driver/sink role).
  if ((pin_pid & 1) == 0) {
    return pin_pid & ~static_cast<Nid>(2);
  }
  // Real pin: look up master nid in pin_table.
  if (graph_ == nullptr) {
    return 0;
  }
  return graph_->ref_pin(pin_pid)->get_master_nid();
}

auto Pin_class::get_port_id() const noexcept -> Port_id {
  if ((pin_pid & 1) == 0) {
    return 0;  // port-0 pin
  }
  if (graph_ == nullptr) {
    return 0;
  }
  return graph_->ref_pin(pin_pid)->get_port_id();
}

auto Pin_class::get_root_gid() const noexcept -> Gid {
  if (context_ == Handle_context::Flat || context_ == Handle_context::Hier) {
    return root_gid_;
  }
  return graph_ != nullptr ? graph_->get_gid() : Gid_invalid;
}

auto Pin_class::get_current_gid() const noexcept -> Gid {
  return graph_ != nullptr ? graph_->get_gid() : Gid_invalid;
}

Graph::PinEntry::PinEntry()
    : master_nid(0), port_id(0), next_pin_id(0), ledge0(0), ledge1(0), use_overflow(0), sedges_{.sedges = 0} {}

Graph::PinEntry::PinEntry(Nid mn, Port_id pid)
    : master_nid(mn), port_id(pid), next_pin_id(0), ledge0(0), ledge1(0), use_overflow(0), sedges_{.sedges = 0} {}

Nid     Graph::PinEntry::get_master_nid() const { return master_nid; }
Port_id Graph::PinEntry::get_port_id() const { return port_id; }
Pid     Graph::PinEntry::get_next_pin_id() const { return next_pin_id; }
void    Graph::PinEntry::set_next_pin_id(Pid id) { next_pin_id = id; }

auto Graph::PinEntry::overflow_handling(Pid self_id, Vid other_id, OverflowPool& pool) -> bool {
  if (use_overflow) {
    pool.sets[sedges_.overflow_idx].insert(other_id);
    return true;
  }
  uint32_t           idx       = pool.alloc();
  auto&              hs        = pool.sets[idx];
  constexpr int      SHIFT     = 16;
  constexpr uint64_t SLOT_MASK = (1ULL << SHIFT) - 1;

  for (int i = 0; i < 4; ++i) {
    uint64_t raw = (sedges_.sedges >> (i * SHIFT)) & SLOT_MASK;
    if (!raw) {
      continue;
    }
    bool is_driver = raw & (1ULL << 1);
    bool is_pin    = raw & (1ULL << 0);
    bool neg       = raw & (1ULL << 15);

    uint64_t mag  = (raw >> 2) & ((1ULL << 13) - 1);
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

auto Graph::PinEntry::add_edge(Pid self_id, Vid other_id, OverflowPool& pool) -> bool {
  if (use_overflow) {
    return overflow_handling(self_id, other_id, pool);
  }

  Nid     actual_self  = self_id >> 2;
  Vid     actual_other = other_id >> 2;
  int64_t diff         = static_cast<int64_t>(actual_self) - static_cast<int64_t>(actual_other);

  bool     isNeg = diff < 0;
  uint64_t mag   = static_cast<uint64_t>(isNeg ? -diff : diff);

  constexpr uint64_t MAX_MAG = (1ULL << 13) - 1;
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
    e |= 1ULL << 15;
  }
  e |= (mag << 2);
  if (other_id & 2) {
    e = e | 2;
  }
  if (other_id & 1) {
    e = e | 1;
  }
  constexpr int      SHIFT = 16;
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

auto Graph::PinEntry::delete_edge(Pid self_id, Vid other_id, OverflowPool& pool) -> bool {
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

Graph::PinEntry::EdgeRange::EdgeRange(const Graph::PinEntry* pin, Pid pid, const OverflowVec& overflow) noexcept
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

Graph::PinEntry::EdgeRange::EdgeRange(EdgeRange&& o) noexcept : pin_(o.pin_), set_(o.set_), own_(o.own_) {
  o.pin_ = nullptr;
  o.set_ = nullptr;
}

Graph::PinEntry::EdgeRange::~EdgeRange() noexcept {
  if (own_) {
    release_set(set_);
  }
}

ankerl::unordered_dense::set<Vid>* Graph::PinEntry::EdgeRange::acquire_set() noexcept {
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

void Graph::PinEntry::EdgeRange::release_set(ankerl::unordered_dense::set<Vid>* set) noexcept {
  static thread_local std::vector<ankerl::unordered_dense::set<Vid>*> pool;
  pool.push_back(set);
}

void Graph::PinEntry::EdgeRange::populate_set(const Graph::PinEntry* pin, ankerl::unordered_dense::set<Vid>& set,
                                              Pid pid) noexcept {
  constexpr uint64_t SLOT_MASK  = (1ULL << 16) - 1;  // grab 16 bits
  constexpr uint64_t SIGN_BIT   = 1ULL << 15;
  constexpr uint64_t DRIVER_BIT = 1ULL << 1;
  constexpr uint64_t PIN_BIT    = 1ULL << 0;
  constexpr uint64_t MAG_MASK   = (1ULL << 13) - 1;  // bits 14–2

  // our caller passed us vid_self = (numeric_id << 1) | pin_flag
  // strip off the low two bits to get the pure numeric node index:
  uint64_t self_num = static_cast<uint64_t>(pid) >> 2;

  uint64_t packed = pin->sedges_.sedges;
  for (int slot = 0; slot < 4; ++slot) {
    uint64_t raw = (packed >> (slot * 16)) & SLOT_MASK;
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

auto Graph::PinEntry::get_edges(Pid pid, const OverflowVec& overflow) const noexcept -> EdgeRange {
  return EdgeRange(this, pid, overflow);
}

bool Graph::PinEntry::has_edges() const {
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

Graph::NodeEntry::NodeEntry() { clear_node(); }
Graph::NodeEntry::NodeEntry(bool alive_val) {
  clear_node();
  alive = alive_val ? 1u : 0u;
}

void Graph::NodeEntry::clear_node() { bzero(this, sizeof(NodeEntry)); }

void Graph::NodeEntry::set_type(Type t) { type = t; }
Type Graph::NodeEntry::get_type() const { return type; }
bool Graph::NodeEntry::is_loop_last() const noexcept { return (type & 1u) != 0u; }
Pid  Graph::NodeEntry::get_next_pin_id() const { return next_pin_id; }
void Graph::NodeEntry::set_next_pin_id(Pid id) { next_pin_id = id; }

auto Graph::NodeEntry::overflow_handling(Nid self_id, Vid other_id, OverflowPool& pool) -> bool {
  if (use_overflow) {
    if (other_id) {
      pool.sets[sedges_.overflow_idx].insert(other_id);
    }
    return true;
  }

  uint32_t           idx       = pool.alloc();
  auto&              hs        = pool.sets[idx];
  constexpr int      SHIFT     = 16;
  constexpr uint64_t SLOT_MASK = (1ULL << SHIFT) - 1;

  auto flush_slot = [&](uint64_t raw) {
    if (!raw) {
      return;
    }
    bool     is_driver = raw & 2;
    bool     is_pin    = raw & 1;
    bool     neg       = raw & (1ULL << 15);
    uint64_t mag       = (raw >> 2) & ((1ULL << 13) - 1);
    int64_t  diff      = neg ? -static_cast<int64_t>(mag) : static_cast<int64_t>(mag);

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
  };

  for (int i = 0; i < 4; ++i) {
    flush_slot((sedges_.sedges >> (i * SHIFT)) & SLOT_MASK);
  }
  const uint64_t extra = sedges_extra;
  for (int i = 0; i < 3; ++i) {
    flush_slot((extra >> (i * SHIFT)) & SLOT_MASK);
  }

  if (ledge0) {
    hs.insert(static_cast<Vid>(ledge0));
  }
  if (ledge1) {
    hs.insert(static_cast<Vid>(ledge1));
  }

  use_overflow         = 1;
  sedges_.sedges       = 0;
  sedges_.overflow_idx = idx;
  sedges_extra         = 0;
  ledge0 = ledge1 = 0;

  if (other_id) {
    hs.insert(other_id);
  }
  return true;
}

auto Graph::NodeEntry::add_edge(Nid self_id, Vid other_id, OverflowPool& pool) -> bool {
  if (use_overflow) {
    return overflow_handling(self_id, other_id, pool);
  }

  Nid     actual_self  = self_id >> 2;
  Vid     actual_other = other_id >> 2;
  int64_t diff         = static_cast<int64_t>(actual_self) - static_cast<int64_t>(actual_other);

  bool     isNeg = diff < 0;
  uint64_t mag   = static_cast<uint64_t>(isNeg ? -diff : diff);

  constexpr uint64_t MAX_MAG = (1ULL << 13) - 1;
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
    e |= 1ULL << 15;
  }
  e |= (mag << 2);
  if (other_id & 2) {
    e = e | 2;
  }
  if (other_id & 1) {
    e = e | 1;
  }
  constexpr int      SHIFT = 16;
  constexpr uint64_t SLOT  = (1ULL << SHIFT) - 1;
  for (int i = 0; i < 4; ++i) {
    uint64_t mask = SLOT << (i * SHIFT);
    if ((sedges_.sedges & mask) == 0) {
      sedges_.sedges |= (e & SLOT) << (i * SHIFT);
      return true;
    }
  }
  uint64_t extra = sedges_extra;
  for (int i = 0; i < 3; ++i) {
    uint64_t mask = SLOT << (i * SHIFT);
    if ((extra & mask) == 0) {
      sedges_extra = extra | ((e & SLOT) << (i * SHIFT));
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

auto Graph::NodeEntry::delete_edge(Nid self_id, Vid other_id, OverflowPool& pool) -> bool {
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
  sedges_extra   = 0;
  ledge0         = 0;
  ledge1         = 0;
  for (auto edge : keep) {
    (void)add_edge(self_id, edge, pool);
  }
  return true;
}

bool Graph::NodeEntry::has_edges(const OverflowVec& overflow) const {
  if (use_overflow) {
    return !overflow[sedges_.overflow_idx].empty();
  }
  if (sedges_.sedges != 0) {
    return true;
  }
  if (sedges_extra != 0) {
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

void Graph::NodeEntry::set_subnode(Nid self_nid, Gid gid, OverflowPool& pool) {
  // Existence inside GraphLibrary must be checked by the caller (NodeEntry has no library pointer).
  if (gid == Gid_invalid) {
    return;
  }
  // Ensure overflow mode so ledge0 is no longer used as an edge spill slot.
  if (!use_overflow) {
    const Vid self_vid = static_cast<Vid>(self_nid & ~static_cast<Nid>(3));
    (void)overflow_handling(self_vid, 0, pool);  // 0 => promote only, no edge insert
  }

  // Store gid in ledge0 as (gid + 1) so ledge0==0 means "no subnode".
  const uint64_t g = static_cast<uint64_t>(gid);
  assert(g < (1ULL << Nid_bits));  // since ledge0 is Nid_bits wide
  ledge0 = static_cast<Nid>(g);
}

Gid Graph::NodeEntry::get_subnode() const noexcept {
  if (!use_overflow || ledge0 == 0) {
    return Gid_invalid;
  }
  return static_cast<Gid>(static_cast<uint64_t>(ledge0));
}

bool Graph::NodeEntry::has_subnode() const noexcept { return use_overflow && ledge0 != 0; }

Graph::NodeEntry::EdgeRange::EdgeRange(const Graph::NodeEntry* node, Nid nid, const OverflowVec& overflow) noexcept
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

Graph::NodeEntry::EdgeRange::EdgeRange(EdgeRange&& o) noexcept : node_(o.node_), /* nid_(o.nid_), */ set_(o.set_), own_(o.own_) {
  o.node_ = nullptr;
  o.set_  = nullptr;
}

Graph::NodeEntry::EdgeRange::~EdgeRange() noexcept {
  if (own_) {
    release_set(set_);
  }
}

ankerl::unordered_dense::set<Nid>* Graph::NodeEntry::EdgeRange::acquire_set() noexcept {
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

void Graph::NodeEntry::EdgeRange::release_set(ankerl::unordered_dense::set<Nid>* set) noexcept {
  static thread_local std::vector<ankerl::unordered_dense::set<Nid>*> pool;
  pool.push_back(set);
}

void Graph::NodeEntry::EdgeRange::populate_set(const Graph::NodeEntry* node, ankerl::unordered_dense::set<Nid>& set,
                                               Nid nid) noexcept {
  constexpr uint64_t SLOT_MASK  = (1ULL << 16) - 1;  // grab 16 bits
  constexpr uint64_t SIGN_BIT   = 1ULL << 15;
  constexpr uint64_t DRIVER_BIT = 1ULL << 1;
  constexpr uint64_t PIN_BIT    = 1ULL << 0;
  constexpr uint64_t MAG_MASK   = (1ULL << 13) - 1;  // bits 14–2

  // our caller passed us vid_self = (numeric_id << 1) | pin_flag
  // strip off the low two bits to get the pure numeric node index:
  uint64_t self_num = static_cast<uint64_t>(nid) >> 2;

  auto decode_slot = [&](uint64_t raw) {
    if (raw == 0) {
      return;
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
  };

  uint64_t packed = node->sedges_.sedges;
  for (int slot = 0; slot < 4; ++slot) {
    decode_slot((packed >> (slot * 16)) & SLOT_MASK);
  }
  const uint64_t extra = node->sedges_extra;
  for (int slot = 0; slot < 3; ++slot) {
    decode_slot((extra >> (slot * 16)) & SLOT_MASK);
  }

  // also remember any “overflow” residues
  if (node->ledge0) {
    set.insert(node->ledge0);
  }
  if (node->ledge1) {
    set.insert(node->ledge1);
  }
}

auto Graph::NodeEntry::get_edges(Nid nid, const OverflowVec& overflow) const noexcept -> EdgeRange {
  return EdgeRange(this, nid, overflow);
}

Graph::Graph() {
  register_attr_tag<attrs::name_t>("hhds::attrs::name");
  clear_graph();
}

void Graph::assert_accessible() const noexcept { assert(!deleted_ && "graph is no longer valid"); }

bool Graph::is_node_valid(Nid nid) const noexcept {
  if (deleted_) {
    return false;
  }
  const Nid actual_id = (nid & ~static_cast<Nid>(3)) >> 2;
  return actual_id > 0 && actual_id < node_table.size() && node_table[actual_id].is_alive();
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
  const Nid raw_nid   = node.get_debug_nid();
  const Nid actual_id = raw_nid >> 2;
  assert(node.get_graph() == this && "node handle belongs to a different graph");
  assert((raw_nid & static_cast<Nid>(1)) == 0 && "node handle is not a node");
  assert(actual_id > 0 && actual_id < node_table.size() && "node handle is invalid for this graph");
  assert(node_table[actual_id].is_alive() && "node handle refers to a deleted node");
}

void Graph::assert_pin_exists(const Pin_class& pin) const noexcept {
  assert_accessible();
  assert(pin.get_graph() == this && "pin handle belongs to a different graph");
  const Pid raw_pid = pin.get_debug_pid();
  if (!(raw_pid & static_cast<Pid>(1))) {
    // port_id == 0: node-as-pin — validate as a node
    const Nid actual_id = raw_pid >> 2;
    assert(actual_id > 0 && actual_id < node_table.size() && "pin(0) node handle is invalid for this graph");
    assert(node_table[actual_id].is_alive() && "pin(0) refers to a deleted node");
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
  discard_attr_stores();
  deleted_   = true;
  owner_lib_ = nullptr;
  node_table.clear();
  pin_table.clear();
  forward_pass2_cache_.clear();
  forward_remaining_in_cache_.clear();
  forward_caches_valid_ = false;
  if (tree_) {
    tree_->clear();
  }
  subnode_tree_pos_.clear();
  input_pins_.clear();
  output_pins_.clear();
}

void Graph::clear_graph() {
  assert_accessible();
  release_storage();
  discard_attr_stores();
  node_table.clear();
  pin_table.clear();
  input_pins_.clear();
  output_pins_.clear();
  node_table.emplace_back(false);  // Invalid ID (slot 0 is tombstone)
  node_table.emplace_back(true);   // Input node (can have many pins to node 1)
  node_table.emplace_back(true);   // Output node
  node_table.emplace_back(true);   // Constant (common value/issue to handle for toposort) - Each const value is a pin in node3
  pin_table.emplace_back(0, 0);
  if (!tree_) {
    tree_ = Tree::create();
  } else {
    tree_->clear();
  }
  (void)tree_->add_root();
  subnode_tree_pos_.clear();
  invalidate_traversal_caches();
}

void Graph::clear() {
  assert_accessible();

  overflow_sets_.clear();
  overflow_free_.clear();
  discard_attr_stores();

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
    node_table.emplace_back(false);  // Invalid ID
    node_table.emplace_back(true);   // Input
    node_table.emplace_back(true);   // Output
    node_table.emplace_back(true);   // Const
  } else {
    for (size_t idx = 0; idx < node_table.size(); ++idx) {
      auto& node = node_table[idx];
      if (idx == 0) {
        node = NodeEntry();  // slot 0 stays dead
      } else if (idx < 4) {
        node = NodeEntry(true);  // built-in IO nodes are alive
      } else {
        node = NodeEntry();
      }
    }
  }

  input_pins_.clear();
  output_pins_.clear();
  if (tree_) {
    tree_->clear();
  } else {
    tree_ = Tree::create();
  }
  (void)tree_->add_root();
  subnode_tree_pos_.clear();
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
  dirty_                = true;
  forward_caches_valid_ = false;
  if (owner_lib_ != nullptr) {
    owner_lib_->note_graph_mutation();
  }
}

// Shared forward-traversal constant: where topological iteration begins in
// node_table. 0:invalid, 1:INPUT, 2:OUTPUT, 3:CONST — CONST is a source so we
// start scanning at idx=3 and let is_source() decide.
static constexpr size_t kForwardFirstIdx = 3;

// Source classification used by both the cache builder and the streaming
// iterator. INPUT (idx=1) and CONST (idx=3) are implicit sources; any live
// user node whose Type's bit 0 is set (is_loop_last — flop/clocked pin) is an
// explicit source.
bool Graph::forward_is_source(size_t idx) const noexcept {
  if (idx == 1 || idx == 3) {
    return true;
  }
  if (idx < kForwardFirstIdx) {
    return false;  // OUTPUT (idx=2) is a forward sink.
  }
  if (idx >= node_table.size()) {
    return false;
  }
  return node_table[idx].is_loop_last();
}

void Graph::ensure_forward_caches() const {
  if (forward_caches_valid_) {
    return;
  }
  const size_t node_count = node_table.size();

  forward_pass2_cache_.clear();
  forward_remaining_in_cache_.assign(node_count, 0);

  if (node_count <= kForwardFirstIdx) {
    forward_caches_valid_ = true;
    return;
  }

  // Resolve a sink Vid back to its owning node index.
  auto sink_idx_of = [&](Vid vid) -> size_t {
    Nid sink_nid;
    if (vid & static_cast<Vid>(1)) {
      const Pid sink_pid = (static_cast<Pid>(vid) & ~static_cast<Pid>(2)) | static_cast<Pid>(1);
      sink_nid           = ref_pin(sink_pid)->get_master_nid();
    } else {
      sink_nid = static_cast<Nid>(vid);
    }
    sink_nid = sink_nid & ~static_cast<Nid>(3);
    return static_cast<size_t>(sink_nid >> 2);
  };

  // Enumerate every downstream sink idx reachable from driver_idx (via node
  // edges and its pin edges); drop in-edges and out-of-range sinks.
  auto for_each_out_sink = [&](size_t driver_idx, auto&& f) {
    const Nid driver_nid = static_cast<Nid>(driver_idx) << 2;
    auto      node_edges = node_table[driver_idx].get_edges(driver_nid, overflow_sets_);
    for (auto vid : node_edges) {
      if (vid & static_cast<Vid>(2)) {
        continue;
      }
      const size_t sink_idx = sink_idx_of(vid);
      if (sink_idx >= kForwardFirstIdx && sink_idx < node_count) {
        f(sink_idx);
      }
    }
    for (Pid pin_vid = node_table[driver_idx].get_next_pin_id(); pin_vid != 0;) {
      const Pid canonical_pin = (pin_vid & ~static_cast<Pid>(2)) | static_cast<Pid>(1);
      auto      pin_edges     = ref_pin(canonical_pin)->get_edges(canonical_pin, overflow_sets_);
      for (auto edge_vid : pin_edges) {
        if (edge_vid & static_cast<Vid>(2)) {
          continue;
        }
        const size_t sink_idx = sink_idx_of(edge_vid);
        if (sink_idx >= kForwardFirstIdx && sink_idx < node_count) {
          f(sink_idx);
        }
      }
      pin_vid = ref_pin(canonical_pin)->get_next_pin_id();
    }
  };

  // Pre-pass: initial in-edge counts (ignoring source-origin edges).
  auto& remaining_in = forward_remaining_in_cache_;
  for (size_t driver_idx = kForwardFirstIdx; driver_idx < node_count; ++driver_idx) {
    if (!node_table[driver_idx].is_alive() || forward_is_source(driver_idx)) {
      continue;
    }
    for_each_out_sink(driver_idx, [&](size_t sink_idx) {
      if (!forward_is_source(sink_idx)) {
        ++remaining_in[sink_idx];
      }
    });
  }

  // Full Pass 1 + Pass 2 dry run to populate forward_pass2_cache_. Uses a
  // working copy so `remaining_in` (the cache) keeps its initial values.
  std::vector<uint32_t> working = remaining_in;
  std::vector<uint64_t> emitted_bits((node_count + 63) / 64, 0);
  auto                  mark_emit = [&](size_t idx) { emitted_bits[idx >> 6] |= (1ULL << (idx & 63)); };
  auto                  is_emit   = [&](size_t idx) { return (emitted_bits[idx >> 6] >> (idx & 63)) & 1ULL; };

  auto propagate = [&](size_t driver_idx, size_t cursor) {
    if (forward_is_source(driver_idx)) {
      return;
    }
    for_each_out_sink(driver_idx, [&](size_t sink_idx) {
      if (is_emit(sink_idx) || forward_is_source(sink_idx)) {
        return;
      }
      if (working[sink_idx] == 0) {
        return;
      }
      --working[sink_idx];
      if (working[sink_idx] == 0 && sink_idx <= cursor) {
        forward_pass2_cache_.push_back(static_cast<Nid>(sink_idx) << 2);
      }
    });
  };

  for (size_t idx = kForwardFirstIdx; idx < node_count; ++idx) {
    if (!node_table[idx].is_alive() || is_emit(idx)) {
      continue;
    }
    if (forward_is_source(idx) || working[idx] == 0) {
      mark_emit(idx);
      propagate(idx, idx);
    }
  }

  for (size_t head = 0; head < forward_pass2_cache_.size(); ++head) {
    const size_t idx = static_cast<size_t>(forward_pass2_cache_[head] >> 2);
    if (is_emit(idx)) {
      continue;
    }
    mark_emit(idx);
    propagate(idx, node_count);
  }

  // Tail (cycle survivors) is not cached — the streaming iterator re-derives
  // it by scanning for alive-but-unemitted entries after Pass 2 completes.
  forward_caches_valid_ = true;
}

auto Graph::create_node() -> Node {
  assert_accessible();
  Nid id = node_table.size();
  assert(id);
  node_table.emplace_back(true);
  invalidate_traversal_caches();
  Nid raw_nid = id << 2 | 0;
  return Node_class(this, raw_nid);
}

auto Graph::create_pin(Node_class node, Port_id port_id) -> Pin_class {
  assert_node_exists(node);
  const Pid pin_pid = create_pin(node.get_debug_nid(), port_id);
  return Pin_class(this, pin_pid);
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

auto Graph::make_pin_class(Pid pin_pid) const -> Pin_class { return Pin_class(const_cast<Graph*>(this), pin_pid); }

void inherit_pin_context(Pin_class& pin, const Node_class& node) {
  pin.context_  = node.context_;
  pin.root_gid_ = node.root_gid_;
  pin.hier_pos_ = node.hier_pos_;
}

auto Graph::find_pin(Node_class node, Port_id port_id, bool driver) const -> Pin_class {
  assert_node_exists(node);
  if (port_id == 0) {
    // port_id == 0 is the node itself acting as a pin
    const Nid nid = node.get_debug_nid() & ~static_cast<Nid>(2);
    Pid       pid = nid;
    if (driver) {
      pid |= static_cast<Pid>(2);
    }
    return Pin_class(const_cast<Graph*>(this), pid);
  }
  const Nid self_nid = node.get_debug_nid() & ~static_cast<Nid>(2);
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
  const Nid self_nid = node.get_debug_nid() & ~static_cast<Nid>(2);
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
  const auto* entry = ref_node(node.get_debug_nid());
  assert(entry->has_subnode() && "create_driver_pin: string form requires a subnode GraphIO");
  assert(owner_lib_ != nullptr && "create_driver_pin: graph has no GraphLibrary");
  auto gio = owner_lib_->graph_ios_[static_cast<size_t>(entry->get_subnode())];
  assert(gio != nullptr && gio->has_output(name) && "create_driver_pin: output name not found in subnode GraphIO");
  return gio->get_output_port_id(name);
}

auto Graph::resolve_sink_port(Node_class node, std::string_view name) const -> Port_id {
  assert_node_exists(node);
  const auto* entry = ref_node(node.get_debug_nid());
  assert(entry->has_subnode() && "create_sink_pin: string form requires a subnode GraphIO");
  assert(owner_lib_ != nullptr && "create_sink_pin: graph has no GraphLibrary");
  auto gio = owner_lib_->graph_ios_[static_cast<size_t>(entry->get_subnode())];
  assert(gio != nullptr && gio->has_input(name) && "create_sink_pin: input name not found in subnode GraphIO");
  return gio->get_input_port_id(name);
}

auto Graph::pin_name(Pin_class pin) const -> std::string_view {
  assert_pin_exists(pin);

  const Pid  raw_pid        = pin.get_debug_pid();
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

  const Pid pin_lookup = (it->second & ~static_cast<Pid>(2)) | static_cast<Pid>(1);
  assert(!ref_pin(pin_lookup)->has_edges() && "erase_declared_io_pin: declared pin still has connected edges");

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
          constexpr int      SHIFT      = 16;
          constexpr uint64_t SLOT       = (1ULL << SHIFT) - 1;
          constexpr uint64_t SIGN_BIT   = 1ULL << 15;
          constexpr uint64_t DRIVER_BIT = 1ULL << 1;
          constexpr uint64_t PIN_BIT    = 1ULL << 0;
          constexpr uint64_t MAG_MASK   = (1ULL << 13) - 1;
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
          constexpr int      SHIFT      = 16;
          constexpr uint64_t SLOT       = (1ULL << SHIFT) - 1;
          constexpr uint64_t SIGN_BIT   = 1ULL << 15;
          constexpr uint64_t DRIVER_BIT = 1ULL << 1;
          constexpr uint64_t PIN_BIT    = 1ULL << 0;
          constexpr uint64_t MAG_MASK   = (1ULL << 13) - 1;
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
          constexpr int      SHIFT      = 16;
          constexpr uint64_t SLOT       = (1ULL << SHIFT) - 1;
          constexpr uint64_t SIGN_BIT   = 1ULL << 15;
          constexpr uint64_t DRIVER_BIT = 1ULL << 1;
          constexpr uint64_t PIN_BIT    = 1ULL << 0;
          constexpr uint64_t MAG_MASK   = (1ULL << 13) - 1;
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
          constexpr int      SHIFT      = 16;
          constexpr uint64_t SLOT       = (1ULL << SHIFT) - 1;
          constexpr uint64_t SIGN_BIT   = 1ULL << 15;
          constexpr uint64_t DRIVER_BIT = 1ULL << 1;
          constexpr uint64_t PIN_BIT    = 1ULL << 0;
          constexpr uint64_t MAG_MASK   = (1ULL << 13) - 1;
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
  erase_attr_object(make_pin_attr_key(static_cast<uint64_t>(pin_lookup)));
  pin_table[actual_id] = PinEntry();
  invalidate_traversal_caches();
}

auto Pin_class::get_master_node() const -> Node_class {
  const Nid nid = get_debug_nid();
  if (context_ == Handle_context::Flat) {
    return Node_class(graph_, root_gid_, nid);
  }
  if (context_ == Handle_context::Hier) {
    return Node_class(graph_, root_gid_, hier_pos_, nid);
  }
  return Node_class(graph_, nid);
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

void Pin_class::connect_sink(Pin_class sink_pin) const {
  assert(graph_ != nullptr && "connect_sink: pin is not attached to a graph");
  graph_->add_edge(*this, sink_pin);
}

void Pin_class::del_sink(Pin_class driver_pin) const {
  assert(graph_ != nullptr && "del_sink: pin is not attached to a graph");
  graph_->del_edge(driver_pin, *this);
}

void Pin_class::del_sink() const {
  assert(graph_ != nullptr && "del_sink: pin is not attached to a graph");
  auto edges = graph_->inp_edges(*this);
  for (const auto& edge : edges) {
    edge.del_edge();
  }
}

void Pin_class::del_driver() const {
  assert(graph_ != nullptr && "del_driver: pin is not attached to a graph");
  auto edges = graph_->out_edges(*this);
  for (const auto& edge : edges) {
    edge.del_edge();
  }
}

void Pin_class::del_pin() const {
  assert(graph_ != nullptr && "del_pin: pin is not attached to a graph");
  del_sink();
  del_driver();
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

Type Node_class::get_type() const {
  assert(graph_ != nullptr && "get_type: node is not attached to a graph");
  return graph_->ref_node(raw_nid)->get_type();
}

bool Node_class::is_loop_last() const {
  assert(graph_ != nullptr && "is_loop_last: node is not attached to a graph");
  return graph_->ref_node(raw_nid)->is_loop_last();
}

auto Node_class::create_driver_pin() const -> Pin_class { return create_driver_pin(0); }
auto Node_class::create_driver_pin(Port_id port_id) const -> Pin_class {
  assert(graph_ != nullptr && "create_driver_pin: node is not attached to a graph");
  if (port_id == 0) {
    // Node itself acts as driver pin(0)
    const Nid nid = raw_nid & ~static_cast<Nid>(2);
    auto      pin = Pin_class(graph_, nid | static_cast<Pid>(2));
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
    auto      pin = Pin_class(graph_, nid);
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
  set_subnode(node.get_debug_nid(), gid);
}

void Graph::set_subnode(Nid nid, Gid gid) {
  assert_accessible();
  if (gid == Gid_invalid) {
    return;
  }

  const GraphIO* subnode_gio = nullptr;
  if (owner_lib_ != nullptr) {
    const size_t idx = static_cast<size_t>(gid);
    assert(idx < owner_lib_->graph_ios_.size() && owner_lib_->graph_ios_[idx] != nullptr);
    if (idx >= owner_lib_->graph_ios_.size() || owner_lib_->graph_ios_[idx] == nullptr) {
      return;
    }
    subnode_gio = owner_lib_->graph_ios_[idx].get();
  }

  nid &= ~static_cast<Nid>(3);
  auto pool = get_overflow_pool();
  ref_node(nid)->set_subnode(nid, gid, pool);

  // Persistent hierarchy: add a child to this graph's tree representing
  // this subnode instance. Only add if not already tracked (re-calling
  // set_subnode on the same node just updates the target Gid).
  if (tree_ && subnode_tree_pos_.find(nid) == subnode_tree_pos_.end()) {
    const Tree_pos child_pos = tree_->add_child(static_cast<Tree_pos>(ROOT));
    subnode_tree_pos_.emplace(nid, child_pos);
  }

  // Stamp node type so the forward iterator can O(1) tell whether this
  // subnode contains any loop_last boundary (flop/register/clocked pin).
  // Bit 0 of Type encodes is_loop_last (odd == loop_last, even == not).
  if (subnode_gio != nullptr) {
    bool has_loop_last = false;
    for (const auto& decl : subnode_gio->input_pin_decls_) {
      if (decl.loop_last) {
        has_loop_last = true;
        break;
      }
    }
    if (!has_loop_last) {
      for (const auto& decl : subnode_gio->output_pin_decls_) {
        if (decl.loop_last) {
          has_loop_last = true;
          break;
        }
      }
    }
    ref_node(nid)->set_type(has_loop_last ? static_cast<Type>(3) : static_cast<Type>(2));
  }

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

void Edge_class::del_edge() const {
  auto* graph = driver.get_graph();
  assert(graph != nullptr && "del_edge: edge driver is not attached to a graph");
  assert(graph == sink.get_graph() && "del_edge: edge endpoints belong to different graphs");
  graph->del_edge(driver, sink);
}

void Graph::del_edge(Pin_class driver_pin, Pin_class sink_pin) {
  assert_accessible();
  assert_pin_exists(driver_pin);
  assert_pin_exists(sink_pin);
  del_edge_int(driver_pin.get_debug_pid(), sink_pin.get_debug_pid());
  invalidate_traversal_caches();
}

FastClassRange Graph::fast_class() const noexcept { return FastClassRange(const_cast<Graph*>(this)); }

ForwardClassRange Graph::forward_class() const noexcept {
  assert_accessible();
  return ForwardClassRange(const_cast<Graph*>(this));
}

FastFlatRange Graph::fast_flat() const noexcept { return FastFlatRange(const_cast<Graph*>(this)); }

FastHierRange Graph::fast_hier() const noexcept { return FastHierRange(const_cast<Graph*>(this)); }

// --- FastClassIterator ---

FastClassIterator::FastClassIterator(Graph* graph, size_t idx, size_t end) noexcept : graph_(graph), idx_(idx), end_(end) {
  skip_tombstones();
}

void FastClassIterator::skip_tombstones() noexcept {
  while (idx_ < end_ && !graph_->node_table[idx_].is_alive()) {
    ++idx_;
  }
}

auto FastClassIterator::operator*() const noexcept -> Node_class { return Node_class(graph_, static_cast<Nid>(idx_) << 2); }

auto FastClassIterator::operator++() noexcept -> FastClassIterator& {
  ++idx_;
  skip_tombstones();
  return *this;
}

FastClassIterator FastClassRange::begin() const noexcept {
  if (graph_ == nullptr) {
    return FastClassIterator{};
  }
  return FastClassIterator(graph_, 3, graph_->node_table.size());
}

FastClassIterator FastClassRange::end() const noexcept {
  if (graph_ == nullptr) {
    return FastClassIterator{};
  }
  const size_t n = graph_->node_table.size();
  return FastClassIterator(graph_, n, n);
}

// --- FastFlatIterator ---

FastFlatIterator::FastFlatIterator(Graph* root_graph) {
  if (root_graph == nullptr) {
    return;
  }
  root_graph->assert_accessible();
  top_graph_ = root_graph->self_gid_;
  if (top_graph_ != Gid_invalid) {
    active_graphs_.insert(top_graph_);
  }
  stack_.push_back(Frame{root_graph, 3, root_graph->node_table.size()});
  advance();
}

void FastFlatIterator::advance() {
  while (!stack_.empty()) {
    Frame& frame = stack_.back();
    if (frame.node_idx >= frame.end) {
      stack_.pop_back();
      continue;
    }
    const auto& entry = frame.graph->node_table[frame.node_idx];
    if (!entry.is_alive()) {
      ++frame.node_idx;
      continue;
    }
    return;  // positioned at an emittable node
  }
}

auto FastFlatIterator::operator*() const -> Node_class {
  const Frame& frame   = stack_.back();
  const Nid    raw_nid = static_cast<Nid>(frame.node_idx) << 2;
  return Node_class(frame.graph, top_graph_, raw_nid);
}

auto FastFlatIterator::operator++() -> FastFlatIterator& {
  Frame&      frame = stack_.back();
  const auto& entry = frame.graph->node_table[frame.node_idx];
  if (entry.has_subnode() && frame.graph->owner_lib_ != nullptr) {
    const Gid   sub = entry.get_subnode();
    const auto* lib = frame.graph->owner_lib_;
    if (lib->has_graph(sub) && active_graphs_.find(sub) == active_graphs_.end()) {
      Graph* child_graph = const_cast<Graph*>(lib->get_graph(sub).get());
      ++frame.node_idx;  // parent resumes past this subnode on pop
      active_graphs_.insert(sub);
      stack_.push_back(Frame{child_graph, 3, child_graph->node_table.size()});
      advance();
      return *this;
    }
  }
  ++frame.node_idx;
  advance();
  return *this;
}

FastFlatIterator FastFlatRange::begin() const { return FastFlatIterator(graph_); }

// --- FastHierIterator ---

FastHierIterator::FastHierIterator(Graph* root_graph) {
  if (root_graph == nullptr) {
    return;
  }
  root_graph->assert_accessible();
  root_gid_ = root_graph->self_gid_;

  if (root_gid_ != Gid_invalid) {
    active_graphs_.insert(root_gid_);
  }
  // Root frame: top-level nodes share hier_pos = ROOT (the root graph's own
  // structure-tree root).
  stack_.push_back(Frame{root_graph, 3, root_graph->node_table.size(), static_cast<Tree_pos>(ROOT)});
  advance();
}

void FastHierIterator::advance() {
  while (!stack_.empty()) {
    Frame& frame = stack_.back();
    if (frame.node_idx >= frame.end) {
      const Gid popped = frame.graph->self_gid_;
      stack_.pop_back();
      if (popped != Gid_invalid) {
        active_graphs_.erase(popped);
      }
      continue;
    }
    const auto& entry = frame.graph->node_table[frame.node_idx];
    if (!entry.is_alive()) {
      ++frame.node_idx;
      continue;
    }
    return;
  }
}

auto FastHierIterator::operator*() const -> Node_class {
  const Frame& frame   = stack_.back();
  const Nid    raw_nid = static_cast<Nid>(frame.node_idx) << 2;
  return Node_class(frame.graph, root_gid_, frame.hier_pos, raw_nid);
}

auto FastHierIterator::operator++() -> FastHierIterator& {
  Frame&      frame = stack_.back();
  const auto& entry = frame.graph->node_table[frame.node_idx];
  if (entry.has_subnode() && frame.graph->owner_lib_ != nullptr) {
    const Gid   sub = entry.get_subnode();
    const auto* lib = frame.graph->owner_lib_;
    if (lib->has_graph(sub) && active_graphs_.find(sub) == active_graphs_.end()) {
      Graph*    child_graph = const_cast<Graph*>(lib->get_graph(sub).get());
      const Nid subnode_nid = static_cast<Nid>(frame.node_idx) << 2;
      // Stable Tree_pos from the structure tree that set_subnode built.
      auto           it        = frame.graph->subnode_tree_pos_.find(subnode_nid);
      const Tree_pos child_pos = (it != frame.graph->subnode_tree_pos_.end()) ? it->second : static_cast<Tree_pos>(ROOT);
      ++frame.node_idx;
      active_graphs_.insert(sub);
      stack_.push_back(Frame{child_graph, 3, child_graph->node_table.size(), child_pos});
      advance();
      return *this;
    }
  }
  ++frame.node_idx;
  advance();
  return *this;
}

FastHierIterator FastHierRange::begin() const { return FastHierIterator(graph_); }

ForwardFlatRange Graph::forward_flat() const noexcept {
  assert_accessible();
  return ForwardFlatRange(const_cast<Graph*>(this));
}

ForwardHierRange Graph::forward_hier() const noexcept {
  assert_accessible();
  return ForwardHierRange(const_cast<Graph*>(this));
}

// --- ForwardClassIterator ---
//
// The iterator replays the topological emission order using the cached Pass-2
// Nid list and initial in-edge counts. Pass 1 scans storage order with a
// working copy of in-edge counts (so multiple iterators can coexist without
// clobbering the cache). Pass 2 reads the cache directly. Tail re-scans
// storage order for cycle survivors.

ForwardClassIterator::ForwardClassIterator(Graph* graph) : graph_(graph) {
  if (graph_ == nullptr) {
    phase_ = Phase::End;
    return;
  }
  graph_->ensure_forward_caches();
  node_count_ = graph_->node_table.size();
  if (node_count_ <= kForwardFirstIdx) {
    phase_ = Phase::End;
    return;
  }
  working_remaining_in_ = graph_->forward_remaining_in_cache_;
  emitted_bits_.assign((node_count_ + 63) / 64, 0);
  phase_ = Phase::Pass1;
  idx_   = kForwardFirstIdx;
  advance();
}

ForwardClassIterator::ForwardClassIterator(ForwardClassIterator&& o) noexcept = default;
ForwardClassIterator& ForwardClassIterator::operator=(ForwardClassIterator&& o) noexcept = default;

bool ForwardClassIterator::is_source(size_t idx) const noexcept { return graph_->forward_is_source(idx); }

bool ForwardClassIterator::is_emitted(size_t idx) const noexcept {
  return (emitted_bits_[idx >> 6] >> (idx & 63)) & 1ULL;
}

void ForwardClassIterator::mark_emitted(size_t idx) noexcept { emitted_bits_[idx >> 6] |= (1ULL << (idx & 63)); }

// Decrement downstream sinks for a Pass-1 emission (cached Pass-2 replay does
// not decrement — the cache already captures the full pending sequence).
void ForwardClassIterator::propagate(size_t driver_idx, size_t /*cursor*/) {
  if (is_source(driver_idx)) {
    return;
  }
  const Nid driver_nid = static_cast<Nid>(driver_idx) << 2;
  auto&     node_table = graph_->node_table;
  auto&     overflow   = graph_->overflow_sets_;

  auto sink_idx_of = [&](Vid vid) -> size_t {
    Nid sink_nid;
    if (vid & static_cast<Vid>(1)) {
      const Pid sink_pid = (static_cast<Pid>(vid) & ~static_cast<Pid>(2)) | static_cast<Pid>(1);
      sink_nid           = graph_->ref_pin(sink_pid)->get_master_nid();
    } else {
      sink_nid = static_cast<Nid>(vid);
    }
    sink_nid = sink_nid & ~static_cast<Nid>(3);
    return static_cast<size_t>(sink_nid >> 2);
  };

  auto try_dec = [&](size_t sink_idx) {
    if (sink_idx < kForwardFirstIdx || sink_idx >= node_count_) {
      return;
    }
    if (is_emitted(sink_idx) || is_source(sink_idx)) {
      return;
    }
    if (working_remaining_in_[sink_idx] == 0) {
      return;
    }
    --working_remaining_in_[sink_idx];
  };

  auto node_edges = node_table[driver_idx].get_edges(driver_nid, overflow);
  for (auto vid : node_edges) {
    if (vid & static_cast<Vid>(2)) {
      continue;
    }
    try_dec(sink_idx_of(vid));
  }
  for (Pid pin_vid = node_table[driver_idx].get_next_pin_id(); pin_vid != 0;) {
    const Pid canonical_pin = (pin_vid & ~static_cast<Pid>(2)) | static_cast<Pid>(1);
    auto      pin_edges     = graph_->ref_pin(canonical_pin)->get_edges(canonical_pin, overflow);
    for (auto edge_vid : pin_edges) {
      if (edge_vid & static_cast<Vid>(2)) {
        continue;
      }
      try_dec(sink_idx_of(edge_vid));
    }
    pin_vid = graph_->ref_pin(canonical_pin)->get_next_pin_id();
  }
}

void ForwardClassIterator::advance() {
  // Position at the next emittable node; emit it (mark + propagate if Pass1);
  // leaves current_idx_ set and phase_ == End when exhausted.
  while (true) {
    if (phase_ == Phase::Pass1) {
      while (idx_ < node_count_) {
        const size_t i = idx_++;
        if (!graph_->node_table[i].is_alive() || is_emitted(i)) {
          continue;
        }
        if (is_source(i) || working_remaining_in_[i] == 0) {
          mark_emitted(i);
          propagate(i, i);
          current_idx_ = i;
          return;
        }
      }
      phase_      = Phase::Pass2;
      pass2_head_ = 0;
      continue;
    }
    if (phase_ == Phase::Pass2) {
      const auto& cache = graph_->forward_pass2_cache_;
      while (pass2_head_ < cache.size()) {
        const size_t i = static_cast<size_t>(cache[pass2_head_++] >> 2);
        if (i >= node_count_ || is_emitted(i) || !graph_->node_table[i].is_alive()) {
          continue;
        }
        mark_emitted(i);
        current_idx_ = i;
        return;
      }
      phase_ = Phase::Tail;
      idx_   = kForwardFirstIdx;
      continue;
    }
    if (phase_ == Phase::Tail) {
      while (idx_ < node_count_) {
        const size_t i = idx_++;
        if (!graph_->node_table[i].is_alive() || is_emitted(i)) {
          continue;
        }
        mark_emitted(i);
        current_idx_ = i;
        return;
      }
      phase_       = Phase::End;
      current_idx_ = 0;
      return;
    }
    return;  // End
  }
}

Node_class ForwardClassIterator::operator*() const {
  return Node_class(graph_, static_cast<Nid>(current_idx_) << 2);
}

ForwardClassIterator& ForwardClassIterator::operator++() {
  advance();
  return *this;
}

ForwardClassIterator ForwardClassRange::begin() const { return ForwardClassIterator(graph_); }

size_t ForwardClassRange::size() const {
  size_t n = 0;
  for (auto it = begin(); it != end(); ++it) {
    ++n;
  }
  return n;
}

Node_class ForwardClassRange::front() const {
  auto it = begin();
  assert(it != end() && "ForwardClassRange::front() on empty range");
  return *it;
}

bool ForwardClassRange::empty() const { return begin() == end(); }

// --- ForwardFlatIterator ---

ForwardFlatIterator::ForwardFlatIterator(Graph* root_graph) {
  if (root_graph == nullptr) {
    return;
  }
  root_graph->assert_accessible();
  top_graph_ = root_graph->self_gid_;
  if (top_graph_ != Gid_invalid) {
    active_graphs_.insert(top_graph_);
  }
  stack_.push_back(Frame{root_graph, ForwardClassIterator(root_graph)});
  advance();
}

void ForwardFlatIterator::advance() {
  while (!stack_.empty()) {
    auto& frame = stack_.back();
    if (frame.it == ForwardClassIterator{}) {
      stack_.pop_back();
      continue;
    }
    return;  // positioned
  }
}

Node_class ForwardFlatIterator::operator*() const {
  const auto& frame   = stack_.back();
  const Nid   raw_nid = (*frame.it).get_debug_nid();
  return Node_class(frame.graph, top_graph_, raw_nid);
}

ForwardFlatIterator& ForwardFlatIterator::operator++() {
  auto&       frame      = stack_.back();
  const Nid   cur_nid    = (*frame.it).get_debug_nid();
  const auto& entry      = frame.graph->node_table[static_cast<size_t>(cur_nid >> 2)];
  const auto* lib        = frame.graph->owner_lib_;
  ++frame.it;
  if (entry.has_subnode() && lib != nullptr) {
    const Gid sub = entry.get_subnode();
    if (lib->has_graph(sub) && active_graphs_.find(sub) == active_graphs_.end()) {
      Graph* child = const_cast<Graph*>(lib->get_graph(sub).get());
      active_graphs_.insert(sub);
      stack_.push_back(Frame{child, ForwardClassIterator(child)});
    }
  }
  advance();
  return *this;
}

ForwardFlatIterator ForwardFlatRange::begin() const { return ForwardFlatIterator(graph_); }

// --- ForwardHierIterator ---

ForwardHierIterator::ForwardHierIterator(Graph* root_graph) {
  if (root_graph == nullptr) {
    return;
  }
  root_graph->assert_accessible();
  root_gid_ = root_graph->self_gid_;
  if (root_gid_ != Gid_invalid) {
    active_graphs_.insert(root_gid_);
  }
  stack_.push_back(Frame{root_graph, ForwardClassIterator(root_graph), static_cast<Tree_pos>(ROOT)});
  advance();
}

void ForwardHierIterator::advance() {
  while (!stack_.empty()) {
    auto& frame = stack_.back();
    if (frame.it == ForwardClassIterator{}) {
      const Gid popped = frame.graph->self_gid_;
      stack_.pop_back();
      if (popped != Gid_invalid) {
        active_graphs_.erase(popped);
      }
      continue;
    }
    return;
  }
}

Node_class ForwardHierIterator::operator*() const {
  const auto& frame   = stack_.back();
  const Nid   raw_nid = (*frame.it).get_debug_nid();
  return Node_class(frame.graph, root_gid_, frame.hier_pos, raw_nid);
}

ForwardHierIterator& ForwardHierIterator::operator++() {
  auto&     frame     = stack_.back();
  const Nid cur_nid   = (*frame.it).get_debug_nid();
  const auto& entry   = frame.graph->node_table[static_cast<size_t>(cur_nid >> 2)];
  const auto* lib     = frame.graph->owner_lib_;
  ++frame.it;
  if (entry.has_subnode() && lib != nullptr) {
    const Gid sub = entry.get_subnode();
    if (lib->has_graph(sub) && active_graphs_.find(sub) == active_graphs_.end()) {
      Graph*         child     = const_cast<Graph*>(lib->get_graph(sub).get());
      auto           it        = frame.graph->subnode_tree_pos_.find(cur_nid);
      const Tree_pos child_pos = (it != frame.graph->subnode_tree_pos_.end()) ? it->second : static_cast<Tree_pos>(ROOT);
      active_graphs_.insert(sub);
      stack_.push_back(Frame{child, ForwardClassIterator(child), child_pos});
    }
  }
  advance();
  return *this;
}

ForwardHierIterator ForwardHierRange::begin() const { return ForwardHierIterator(graph_); }

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
  const Nid               self_nid = node.get_debug_nid() & ~static_cast<Nid>(2);
  auto*                   self     = ref_node(self_nid);
  auto                    edges    = self->get_edges(self_nid, overflow_sets_);
  Pin_class               self_driver(this, self_nid | static_cast<Pid>(2));
  inherit_pin_context(self_driver, node);

  for (auto vid : edges) {
    if (vid & 2) {
      continue;
    }
    if (vid & 1) {
      const Pid sink_pid = static_cast<Pid>(vid);

      Edge_class e{};
      e.driver = self_driver;
      e.sink   = make_pin_class(sink_pid);
      inherit_pin_context(e.sink, node);
      out.push_back(e);
      continue;
    } else {
      const Nid sink_nid = static_cast<Nid>(vid);

      Edge_class e{};
      e.driver = self_driver;
      e.sink   = Pin_class(this, sink_nid & ~static_cast<Nid>(2));
      out.push_back(e);
    }
  }

  for (auto pin : get_pins(node)) {
    inherit_pin_context(pin, node);
    auto pin_edges = out_edges(pin);
    out.insert(out.end(), pin_edges.begin(), pin_edges.end());
  }
  return out;
}

auto Graph::inp_edges(Node_class node) -> std::vector<Edge_class> {
  assert_accessible();
  assert_node_exists(node);
  std::vector<Edge_class> out;
  const Nid               self_nid = node.get_debug_nid() & ~static_cast<Nid>(2);
  auto*                   self     = ref_node(self_nid);
  auto                    edges    = self->get_edges(self_nid, overflow_sets_);
  Pin_class               self_sink(this, self_nid & ~static_cast<Pid>(2));
  inherit_pin_context(self_sink, node);

  for (auto vid : edges) {
    if (!(vid & 2)) {
      continue;
    }
    if (vid & 1) {
      const Pid driver_pid = static_cast<Pid>(vid);

      Edge_class e{};
      e.driver = make_pin_class(driver_pid);
      inherit_pin_context(e.driver, node);
      e.sink = self_sink;
      out.push_back(e);
      continue;
    } else {
      const Nid driver_nid = static_cast<Nid>(vid);

      Edge_class e{};
      e.driver = Pin_class(this, driver_nid | static_cast<Nid>(2));
      e.sink   = self_sink;
      out.push_back(e);
    }
  }

  for (auto pin : get_pins(node)) {
    inherit_pin_context(pin, node);
    auto pin_edges = inp_edges(pin);
    out.insert(out.end(), pin_edges.begin(), pin_edges.end());
  }
  return out;
}

auto Graph::out_edges(Pin_class pin) -> std::vector<Edge_class> {
  assert_accessible();
  assert_pin_exists(pin);

  // port_id == 0: read edges from NodeEntry, build pin-aware results
  if (!(pin.get_debug_pid() & static_cast<Pid>(1))) {
    const Nid self_nid = pin.get_debug_pid() & ~static_cast<Nid>(2);
    auto*     self     = ref_node(self_nid);
    auto      edges    = self->get_edges(self_nid, overflow_sets_);
    Pin_class self_driver_pin(this, self_nid | static_cast<Pid>(2));
    self_driver_pin.context_     = pin.context_;
    self_driver_pin.root_gid_    = pin.root_gid_;
    self_driver_pin.hier_pos_    = pin.hier_pos_;

    std::vector<Edge_class> out;
    for (auto vid : edges) {
      if (vid & 2) {
        continue;  // skip back edges
      }
      if (vid & 1) {
        Edge_class e{};
        e.driver            = self_driver_pin;
        e.sink              = make_pin_class(static_cast<Pid>(vid));
        e.sink.context_     = pin.context_;
        e.sink.root_gid_    = pin.root_gid_;
        e.sink.hier_pos_    = pin.hier_pos_;
        out.push_back(e);
      } else {
        const Nid  sink_nid = static_cast<Nid>(vid);
        Edge_class e{};
        e.driver            = self_driver_pin;
        e.sink              = Pin_class(this, sink_nid & ~static_cast<Nid>(2));
        e.sink.context_     = pin.context_;
        e.sink.root_gid_    = pin.root_gid_;
        e.sink.hier_pos_    = pin.hier_pos_;
        out.push_back(e);
      }
    }
    return out;
  }

  std::vector<Edge_class> out;
  const Pid               self_pid           = pin.get_debug_pid();
  const Pid               self_pid_lookup    = (self_pid & ~static_cast<Pid>(2)) | static_cast<Pid>(1);
  auto*                   self               = ref_pin(self_pid_lookup);
  auto                    edges              = self->get_edges(self_pid_lookup, overflow_sets_);
  const Pin_class         self_driver_pin    = make_pin_class(self_pid_lookup | static_cast<Pid>(2));
  Pin_class               context_driver_pin = self_driver_pin;
  context_driver_pin.context_                = pin.context_;
  context_driver_pin.root_gid_               = pin.root_gid_;
  context_driver_pin.hier_pos_               = pin.hier_pos_;

  for (auto vid : edges) {
    if (vid & 2) {
      continue;
    }
    if (vid & 1) {
      const Pid sink_pid = static_cast<Pid>(vid);

      Edge_class e{};
      e.driver            = context_driver_pin;
      e.sink              = make_pin_class(sink_pid);
      e.sink.context_     = pin.context_;
      e.sink.root_gid_    = pin.root_gid_;
      e.sink.hier_pos_    = pin.hier_pos_;
      out.push_back(e);
      continue;
    }

    const Nid sink_nid = static_cast<Nid>(vid);

    Edge_class e{};
    e.driver = context_driver_pin;
    e.sink   = Pin_class(this, sink_nid & ~static_cast<Nid>(2));
    out.push_back(e);
  }

  return out;
}

auto Graph::inp_edges(Pin_class pin) -> std::vector<Edge_class> {
  assert_accessible();
  assert_pin_exists(pin);

  // port_id == 0: read edges from NodeEntry, build pin-aware results
  if (!(pin.get_debug_pid() & static_cast<Pid>(1))) {
    const Nid self_nid = pin.get_debug_pid() & ~static_cast<Nid>(2);
    auto*     self     = ref_node(self_nid);
    auto      edges    = self->get_edges(self_nid, overflow_sets_);
    Pin_class self_sink_pin(this, self_nid);
    self_sink_pin.context_     = pin.context_;
    self_sink_pin.root_gid_    = pin.root_gid_;
    self_sink_pin.hier_pos_    = pin.hier_pos_;

    std::vector<Edge_class> out;
    for (auto vid : edges) {
      if (!(vid & 2)) {
        continue;  // skip local/forward edges
      }
      if (vid & 1) {
        Edge_class e{};
        e.driver              = make_pin_class(static_cast<Pid>(vid));
        e.driver.context_     = pin.context_;
        e.driver.root_gid_    = pin.root_gid_;
        e.driver.hier_pos_    = pin.hier_pos_;
        e.sink                = self_sink_pin;
        out.push_back(e);
      } else {
        const Nid  driver_nid = static_cast<Nid>(vid);
        Edge_class e{};
        e.driver              = Pin_class(this, driver_nid | static_cast<Nid>(2));
        e.driver.context_     = pin.context_;
        e.driver.root_gid_    = pin.root_gid_;
        e.driver.hier_pos_    = pin.hier_pos_;
        e.sink                = self_sink_pin;
        out.push_back(e);
      }
    }
    return out;
  }

  std::vector<Edge_class> out;
  const Pid               self_pid         = pin.get_debug_pid();
  const Pid               self_pid_sink    = (self_pid & ~static_cast<Pid>(2)) | static_cast<Pid>(1);
  auto*                   self             = ref_pin(self_pid_sink);
  auto                    edges            = self->get_edges(self_pid_sink, overflow_sets_);
  const Pin_class         self_sink_pin    = make_pin_class(self_pid_sink);
  Pin_class               context_sink_pin = self_sink_pin;
  context_sink_pin.context_                = pin.context_;
  context_sink_pin.root_gid_               = pin.root_gid_;
  context_sink_pin.hier_pos_               = pin.hier_pos_;

  for (auto vid : edges) {
    if (!(vid & 2)) {
      continue;
    }
    if (vid & 1) {
      const Pid driver_pid = static_cast<Pid>(vid);

      Edge_class e{};
      e.driver              = make_pin_class(driver_pid);
      e.driver.context_     = pin.context_;
      e.driver.root_gid_    = pin.root_gid_;
      e.driver.hier_pos_    = pin.hier_pos_;
      e.sink                = context_sink_pin;
      out.push_back(e);
      continue;
    }

    const Nid driver_nid = static_cast<Nid>(vid);

    Edge_class e{};
    e.driver = Pin_class(this, driver_nid | static_cast<Nid>(2));
    e.sink   = context_sink_pin;
    out.push_back(e);
  }

  return out;
}

auto Graph::get_pins(Node_class node) -> std::vector<Pin_class> {
  assert_accessible();
  assert_node_exists(node);
  std::vector<Pin_class> out;
  const Nid              self_nid = node.get_debug_nid() & ~static_cast<Nid>(2);
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
    const Pid pid_lookup = (pin.get_debug_pid() & ~static_cast<Pid>(2)) | static_cast<Pid>(1);
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
    const Pid pid_lookup = (pin.get_debug_pid() & ~static_cast<Pid>(2)) | static_cast<Pid>(1);
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
  assert(actual_id < node_table.size() && node_table[actual_id].is_alive() && "delete_node: node handle is invalid");

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

  erase_attr_object(make_node_attr_key(static_cast<uint64_t>(nid)));
  erase_attr_object(make_pin_attr_key(static_cast<uint64_t>(nid)));
  for (auto pin_pid : pins_to_delete) {
    const Pid actual_pin_id = pin_pid >> 2;
    auto*     pin           = &pin_table[actual_pin_id];
    if (pin->use_overflow) {
      overflow_free_.push_back(pin->get_overflow_idx());
      overflow_sets_[pin->get_overflow_idx()].clear();
    }
    erase_attr_object(make_pin_attr_key(static_cast<uint64_t>(pin_pid)));
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
      std::cerr << "Error: NodeEntry " << (self_id >> 2) << " overflowed edges while adding edge from " << self_id << " to "
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

void Graph::print(std::ostream& os) const {
  assert_accessible();
  os << name_ << " {\n";
  for (auto node : forward_class()) {
    const Nid raw_nid = node.get_debug_nid() & ~static_cast<Nid>(3);
    const Nid actual  = raw_nid >> 2;
    if (actual < 4) {
      continue;
    }

    os << "  %" << raw_nid << " = ";
    if (node.attr(attrs::name).has()) {
      os << node.attr(attrs::name).get();
    } else {
      os << "node_" << actual;
    }

    const auto* entry = ref_node(raw_nid);
    if (entry->has_subnode() && owner_lib_ != nullptr) {
      const auto gio = owner_lib_->graph_ios_[static_cast<size_t>(entry->get_subnode())];
      if (gio != nullptr) {
        os << " : " << gio->get_name();
      }
    }
    os << '\n';

    for (Pid cur_pin = entry->get_next_pin_id(); cur_pin != 0;) {
      const Pid  canonical_pin = (cur_pin & ~static_cast<Pid>(2)) | static_cast<Pid>(1);
      const auto pin           = make_pin_class(canonical_pin);
      os << "    ." << pin.get_pin_name();
      if (pin.attr(attrs::name).has()) {
        os << " @" << pin.attr(attrs::name).get();
      }
      os << '\n';
      cur_pin = ref_pin(canonical_pin)->get_next_pin_id();
    }
  }
  os << "}\n";
}

std::string Graph::print() const {
  std::ostringstream oss;
  print(oss);
  return oss.str();
}

// --------------------------------------------------------------------------
// Binary persistence
// --------------------------------------------------------------------------

static constexpr uint32_t GRAPH_BODY_MAGIC   = 0x48484742;  // "HHGB"
static constexpr uint32_t GRAPH_BODY_VERSION = 3;
static constexpr uint32_t ENDIAN_CHECK       = 0x01020304;

void Graph::save_body(const std::string& dir_path) const {
  namespace fs = std::filesystem;
  fs::create_directories(dir_path);

  // --- body.bin ---
  {
    const auto    path = fs::path(dir_path) / "body.bin";
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
    save_attr_stores(ofs);
  }

  // --- overflow_<idx>.bin (one per overflow set) ---
  for (uint32_t i = 0; i < overflow_sets_.size(); ++i) {
    const auto&   oset = overflow_sets_[i];
    const auto    path = fs::path(dir_path) / ("overflow_" + std::to_string(i) + ".bin");
    std::ofstream ofs(path, std::ios::binary);
    assert(ofs.good() && "save_body: cannot open overflow file for writing");

    // Use the values() API — contiguous Vid vector, no bucket data needed.
    const auto&    vals  = oset.values();
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
    const auto    path = fs::path(dir_path) / "body.bin";
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
    overflow_free_.clear();

    load_attr_stores(ifs);
  }

  // --- overflow_<idx>.bin ---
  for (uint32_t i = 0; i < overflow_sets_.size(); ++i) {
    const auto    path = std::filesystem::path(dir_path) / ("overflow_" + std::to_string(i) + ".bin");
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

  // Rebuild structure tree: save/load only persists node_table (which holds
  // each subnode's target Gid in ledge0). Walk the live entries and
  // reconstruct tree_ + subnode_tree_pos_ so hier traversal works.
  if (!tree_) {
    tree_ = Tree::create();
  } else {
    tree_->clear();
  }
  (void)tree_->add_root();
  subnode_tree_pos_.clear();
  for (size_t i = 1; i < node_table.size(); ++i) {
    if (!node_table[i].is_alive() || !node_table[i].has_subnode()) {
      continue;
    }
    const Nid      subnode_nid = static_cast<Nid>(i) << 2;
    const Tree_pos child_pos   = tree_->add_child(static_cast<Tree_pos>(ROOT));
    subnode_tree_pos_.emplace(subnode_nid, child_pos);
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
    // Preserve (name, gid) pairs for deleted graphs so that recreating by name
    // reuses the original gid. Parent graphs store subnode gids in their
    // binary bodies; this lets those references survive delete + recreate
    // across save/load.
    for (const auto& [name, gid] : deleted_name_to_id_) {
      ofs << "graph_io_deleted " << gid << " " << name << "\n";
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
  deleted_name_to_id_.clear();
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
      if (line.substr(0, 17) == "graph_io_deleted ") {
        std::istringstream ss(line.substr(17));
        Gid                gid;
        std::string        name;
        ss >> gid >> name;
        // Reserve the slot so fresh allocations don't reuse this gid.
        const size_t idx = static_cast<size_t>(gid);
        if (idx >= graph_ios_.size()) {
          graph_ios_.resize(idx + 1);
          graphs_.resize(idx + 1);
        }
        deleted_name_to_id_[name] = gid;
        current_gio.reset();
      } else if (line.substr(0, 9) == "graph_io ") {
        // "graph_io <gid> <name>"
        std::istringstream ss(line.substr(9));
        Gid                gid;
        std::string        name;
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
