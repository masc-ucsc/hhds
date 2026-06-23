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

auto Node_class::get_current_gid() const noexcept -> Gid { return graph_ != nullptr ? graph_->get_gid() : Gid_invalid; }

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

auto Pin_class::get_current_gid() const noexcept -> Gid { return graph_ != nullptr ? graph_->get_gid() : Gid_invalid; }

Graph::PinEntry::PinEntry()
    : master_nid(0), port_id(0), next_pin_id(0), ledge0(0), ledge1(0), use_overflow(0), sedges_{.sedges = 0} {}

Graph::PinEntry::PinEntry(Nid mn, Port_id pid)
    : master_nid(mn), port_id(pid), next_pin_id(0), ledge0(0), ledge1(0), use_overflow(0), sedges_{.sedges = 0} {}

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
  // Zero the full 64-bit union word before writing the 32-bit overflow_idx so
  // the stale upper 32 bits of the old packed sedges don't linger (they get
  // serialized verbatim by save_body, which would make identical graphs
  // produce byte-different bodies). Mirrors the NodeEntry promotion path.
  sedges_.sedges       = 0;
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
  // e == 0 (diff == 0 with a node-as-sink target, flag bits 00) is
  // indistinguishable from the empty-slot sentinel: storing it in a sedge
  // slot would silently drop the edge. This aliasing is legal — pin_table
  // and node_table indices are independent, so a pin at index N can drive
  // port 0 of the node at index N. Route it through the long-edge slots /
  // overflow set, which keep the full Vid.
  constexpr int      SHIFT = 16;
  constexpr uint64_t SLOT  = (1ULL << SHIFT) - 1;
  if (e != 0) {
    for (int i = 0; i < 4; ++i) {
      uint64_t mask = SLOT << (i * SHIFT);
      if ((sedges_.sedges & mask) == 0) {
        sedges_.sedges |= (e & SLOT) << (i * SHIFT);
        return true;
      }
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

  // Fast in-place delete for inline edges. We iterate slots, decode each, and
  // zero out the first matching one. Slots can be left with gaps (zeros);
  // populate_vec and add_edge both already handle holes.
  if (ledge0 == other_id) {
    ledge0 = 0;
    return true;
  }
  if (ledge1 == other_id) {
    ledge1 = 0;
    return true;
  }
  constexpr uint64_t SLOT_MASK  = (1ULL << 16) - 1;
  constexpr uint64_t SIGN_BIT   = 1ULL << 15;
  constexpr uint64_t DRIVER_BIT = 1ULL << 1;
  constexpr uint64_t PIN_BIT    = 1ULL << 0;
  constexpr uint64_t MAG_MASK   = (1ULL << 13) - 1;
  const uint64_t     self_num   = static_cast<uint64_t>(self_id) >> 2;
  for (int i = 0; i < 4; ++i) {
    const uint64_t raw = (sedges_.sedges >> (i * 16)) & SLOT_MASK;
    if (raw == 0) {
      continue;
    }
    const bool     neg        = (raw & SIGN_BIT) != 0;
    const bool     driver     = (raw & DRIVER_BIT) != 0;
    const bool     pin        = (raw & PIN_BIT) != 0;
    const uint64_t mag        = (raw >> 2) & MAG_MASK;
    const int64_t  delta      = neg ? -static_cast<int64_t>(mag) : static_cast<int64_t>(mag);
    const uint64_t target_num = self_num - delta;
    const Vid      v          = static_cast<Vid>((target_num << 2) | (driver ? DRIVER_BIT : 0) | (pin ? PIN_BIT : 0));
    if (v == other_id) {
      sedges_.sedges &= ~(SLOT_MASK << (i * 16));
      return true;
    }
  }
  return false;
}

Graph::PinEntry::EdgeRange::EdgeRange(const Graph::PinEntry* pin, Pid pid, const OverflowVec& overflow) noexcept {
  if (pin->use_overflow) {
    overflow_set_ = &overflow[pin->sedges_.overflow_idx];
    return;
  }
  // Inline: decode the 4 packed sedge slots + ledge0/ledge1 directly into inline_buf_.
  constexpr uint64_t SLOT_MASK  = (1ULL << 16) - 1;  // grab 16 bits
  constexpr uint64_t SIGN_BIT   = 1ULL << 15;
  constexpr uint64_t DRIVER_BIT = 1ULL << 1;
  constexpr uint64_t PIN_BIT    = 1ULL << 0;
  constexpr uint64_t MAG_MASK   = (1ULL << 13) - 1;  // bits 14–2

  const uint64_t self_num = static_cast<uint64_t>(pid) >> 2;
  const uint64_t packed   = pin->sedges_.sedges;
  for (int slot = 0; slot < 4; ++slot) {
    const uint64_t raw = (packed >> (slot * 16)) & SLOT_MASK;
    if (raw == 0) {
      continue;
    }
    const bool     neg           = (raw & SIGN_BIT) != 0;
    const bool     driver        = (raw & DRIVER_BIT) != 0;
    const bool     pin_bit       = (raw & PIN_BIT) != 0;
    const uint64_t mag           = (raw >> 2) & MAG_MASK;
    const int64_t  delta         = neg ? -static_cast<int64_t>(mag) : static_cast<int64_t>(mag);
    const uint64_t target_num    = self_num - delta;
    inline_buf_[inline_count_++] = static_cast<Vid>((target_num << 2) | (driver ? DRIVER_BIT : 0) | (pin_bit ? PIN_BIT : 0));
  }
  if (pin->ledge0) {
    inline_buf_[inline_count_++] = pin->ledge0;
  }
  if (pin->ledge1) {
    inline_buf_[inline_count_++] = pin->ledge1;
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
  // e == 0 (diff == 0 with a node-as-sink target, flag bits 00) aliases the
  // empty-slot sentinel — see the matching comment in PinEntry::add_edge.
  // For a NodeEntry this is the port0->port0 self-loop. Route it through the
  // long-edge slots / overflow set, which keep the full Vid.
  constexpr int      SHIFT = 16;
  constexpr uint64_t SLOT  = (1ULL << SHIFT) - 1;
  if (e != 0) {
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

  // Fast in-place delete for inline edges (4 sedges + 3 sedges_extra + 2 ledges).
  // Zero out the matching slot; gaps are fine (add_edge and populate_vec both
  // handle them).
  if (ledge0 == other_id) {
    ledge0 = 0;
    return true;
  }
  if (ledge1 == other_id) {
    ledge1 = 0;
    return true;
  }
  constexpr uint64_t SLOT_MASK  = (1ULL << 16) - 1;
  constexpr uint64_t SIGN_BIT   = 1ULL << 15;
  constexpr uint64_t DRIVER_BIT = 1ULL << 1;
  constexpr uint64_t PIN_BIT    = 1ULL << 0;
  constexpr uint64_t MAG_MASK   = (1ULL << 13) - 1;
  const uint64_t     self_num   = static_cast<uint64_t>(self_id) >> 2;

  auto try_zero_slot = [&](uint64_t packed, int slot) -> bool {
    const uint64_t raw = (packed >> (slot * 16)) & SLOT_MASK;
    if (raw == 0) {
      return false;
    }
    const bool     neg        = (raw & SIGN_BIT) != 0;
    const bool     driver     = (raw & DRIVER_BIT) != 0;
    const bool     pin        = (raw & PIN_BIT) != 0;
    const uint64_t mag        = (raw >> 2) & MAG_MASK;
    const int64_t  delta      = neg ? -static_cast<int64_t>(mag) : static_cast<int64_t>(mag);
    const uint64_t target_num = self_num - delta;
    const Vid      v          = static_cast<Vid>((target_num << 2) | (driver ? DRIVER_BIT : 0) | (pin ? PIN_BIT : 0));
    return v == other_id;
  };

  for (int i = 0; i < 4; ++i) {
    if (try_zero_slot(sedges_.sedges, i)) {
      sedges_.sedges &= ~(SLOT_MASK << (i * 16));
      return true;
    }
  }
  for (int i = 0; i < 3; ++i) {
    if (try_zero_slot(sedges_extra, i)) {
      const uint64_t mask = SLOT_MASK << (i * 16);
      sedges_extra        = sedges_extra & ~mask;
      return true;
    }
  }
  return false;
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

Graph::NodeEntry::EdgeRange::EdgeRange(const Graph::NodeEntry* node, Nid nid, const OverflowVec& overflow) noexcept {
  if (node->use_overflow) {
    overflow_set_ = &overflow[node->sedges_.overflow_idx];
    return;
  }
  // Inline: decode 4 packed sedge slots + 3 extra slots + ledge0/ledge1 into inline_buf_.
  constexpr uint64_t SLOT_MASK  = (1ULL << 16) - 1;  // grab 16 bits
  constexpr uint64_t SIGN_BIT   = 1ULL << 15;
  constexpr uint64_t DRIVER_BIT = 1ULL << 1;
  constexpr uint64_t PIN_BIT    = 1ULL << 0;
  constexpr uint64_t MAG_MASK   = (1ULL << 13) - 1;  // bits 14–2

  const uint64_t self_num = static_cast<uint64_t>(nid) >> 2;

  auto decode_slot = [&](uint64_t raw) {
    if (raw == 0) {
      return;
    }
    const bool     neg           = (raw & SIGN_BIT) != 0;
    const bool     driver        = (raw & DRIVER_BIT) != 0;
    const bool     pin_bit       = (raw & PIN_BIT) != 0;
    const uint64_t mag           = (raw >> 2) & MAG_MASK;
    const int64_t  delta         = neg ? -static_cast<int64_t>(mag) : static_cast<int64_t>(mag);
    const uint64_t target_num    = self_num - delta;
    inline_buf_[inline_count_++] = static_cast<Vid>((target_num << 2) | (driver ? DRIVER_BIT : 0) | (pin_bit ? PIN_BIT : 0));
  };

  const uint64_t packed = node->sedges_.sedges;
  for (int slot = 0; slot < 4; ++slot) {
    decode_slot((packed >> (slot * 16)) & SLOT_MASK);
  }
  const uint64_t extra = node->sedges_extra;
  for (int slot = 0; slot < 3; ++slot) {
    decode_slot((extra >> (slot * 16)) & SLOT_MASK);
  }
  if (node->ledge0) {
    inline_buf_[inline_count_++] = node->ledge0;
  }
  if (node->ledge1) {
    inline_buf_[inline_count_++] = node->ledge1;
  }
}

auto Graph::NodeEntry::get_edges(Nid nid, const OverflowVec& overflow) const noexcept -> EdgeRange {
  return EdgeRange(this, nid, overflow);
}

Graph::Graph() {
  register_attr_tag<attrs::name_t>("hhds::attrs::name");
  clear_graph();
}

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
  srcloc_.clear();
  srcloc_.set_base(nullptr);
  node_table.clear();
  pin_table.clear();
  forward_pass2_cache_.clear();
  forward_remaining_in_cache_.clear();
  forward_caches_valid_ = false;
  backward_pass2_cache_.clear();
  backward_remaining_out_cache_.clear();
  backward_caches_valid_ = false;
  if (tree_) {
    tree_->clear();
  }
  subnode_tree_pos_.clear();
  tree_pos_to_nid_.clear();
  input_pins_.clear();
  output_pins_.clear();
}

void Graph::clear_graph() {
  assert_accessible();
  release_storage();
  discard_attr_stores();
  srcloc_.clear();  // provenance is body content: dropped with the attrs (base kept)
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
  tree_pos_to_nid_.clear();
  invalidate_traversal_caches();
}

void Graph::clear() {
  assert_accessible();

  overflow_sets_.clear();
  overflow_free_.clear();
  discard_attr_stores();
  srcloc_.clear();  // provenance is body content: dropped with the attrs (base kept)

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
  tree_pos_to_nid_.clear();
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
  // Resolution chains per-graph source-provenance mints to the library base
  // (mutable member: stable address for the library's lifetime, which outlives
  // every attached graph).
  srcloc_.set_base(owner != nullptr ? owner->srcmap_sp_.get() : nullptr);
}

// Shared traversal constant: where iteration over user nodes begins in
// node_table. 0:invalid, 1:INPUT, 2:OUTPUT, 3:CONST are built-in singletons
// reached via Graph::get_input_node/get_output_node/get_constant_node — never
// emitted by class/flat/hier traversals. User nodes start at idx 4.
static constexpr size_t kFirstUserNodeIdx = 4;

// Source classification used by both the cache builder and the streaming
// iterator. INPUT (idx=1) and CONST (idx=3) are implicit sources; any live
// user node whose Type's bit 0 is set (is_loop_break — flop/clocked pin) is an
// explicit source.
bool Graph::forward_is_source(size_t idx) const noexcept {
  if (idx == 1 || idx == 3) {
    return true;
  }
  if (idx < kFirstUserNodeIdx) {
    return false;  // OUTPUT (idx=2) is a forward sink.
  }
  if (idx >= node_table.size()) {
    return false;
  }
  return node_table[idx].is_loop_break();
}

void Graph::ensure_forward_caches() const {
  if (forward_caches_valid_) {
    return;
  }
  const size_t node_count = node_table.size();

  forward_pass2_cache_.clear();
  forward_remaining_in_cache_.assign(node_count, 0);

  if (node_count <= kFirstUserNodeIdx) {
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
      if (sink_idx >= kFirstUserNodeIdx && sink_idx < node_count) {
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
        if (sink_idx >= kFirstUserNodeIdx && sink_idx < node_count) {
          f(sink_idx);
        }
      }
      pin_vid = ref_pin(canonical_pin)->get_next_pin_id();
    }
  };

  // Pre-pass: initial in-edge counts (ignoring source-origin edges).
  auto& remaining_in = forward_remaining_in_cache_;
  for (size_t driver_idx = kFirstUserNodeIdx; driver_idx < node_count; ++driver_idx) {
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

  for (size_t idx = kFirstUserNodeIdx; idx < node_count; ++idx) {
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

bool Graph::backward_is_sink(size_t idx) const noexcept {
  if (idx == 2) {
    return true;
  }
  if (idx < kFirstUserNodeIdx) {
    return false;
  }
  if (idx >= node_table.size()) {
    return false;
  }
  return node_table[idx].is_loop_break();
}

void Graph::ensure_backward_caches() const {
  if (backward_caches_valid_) {
    return;
  }
  const size_t node_count = node_table.size();

  backward_pass2_cache_.clear();
  backward_remaining_out_cache_.assign(node_count, 0);

  if (node_count <= kFirstUserNodeIdx) {
    backward_caches_valid_ = true;
    return;
  }

  // Resolve a driver Vid back to its owning node index.
  auto driver_idx_of = [&](Vid vid) -> size_t {
    Nid driver_nid;
    if (vid & static_cast<Vid>(1)) {
      const Pid driver_pid = (static_cast<Pid>(vid) & ~static_cast<Pid>(2)) | static_cast<Pid>(1);
      driver_nid           = ref_pin(driver_pid)->get_master_nid();
    } else {
      driver_nid = static_cast<Nid>(vid);
    }
    driver_nid = driver_nid & ~static_cast<Nid>(3);
    return static_cast<size_t>(driver_nid >> 2);
  };

  // Enumerate every upstream driver idx reachable from sink_idx
  auto for_each_in_driver = [&](size_t sink_idx, auto&& f) {
    const Nid sink_nid   = static_cast<Nid>(sink_idx) << 2;
    auto      node_edges = node_table[sink_idx].get_edges(sink_nid, overflow_sets_);
    for (auto vid : node_edges) {
      if (!(vid & static_cast<Vid>(2))) {
        continue;
      }
      const size_t driver_idx = driver_idx_of(vid);
      if (driver_idx >= kFirstUserNodeIdx && driver_idx < node_count) {
        f(driver_idx);
      }
    }
    for (Pid pin_vid = node_table[sink_idx].get_next_pin_id(); pin_vid != 0;) {
      const Pid canonical_pin = (pin_vid & ~static_cast<Pid>(2)) | static_cast<Pid>(1);
      auto      pin_edges     = ref_pin(canonical_pin)->get_edges(canonical_pin, overflow_sets_);
      for (auto edge_vid : pin_edges) {
        if (!(edge_vid & static_cast<Vid>(2))) {
          continue;
        }
        const size_t driver_idx = driver_idx_of(edge_vid);
        if (driver_idx >= kFirstUserNodeIdx && driver_idx < node_count) {
          f(driver_idx);
        }
      }
      pin_vid = ref_pin(canonical_pin)->get_next_pin_id();
    }
  };

  // Pre-pass: initial out-edge counts.
  auto& remaining_out = backward_remaining_out_cache_;
  for (size_t sink_idx = kFirstUserNodeIdx; sink_idx < node_count; ++sink_idx) {
    if (!node_table[sink_idx].is_alive() || backward_is_sink(sink_idx)) {
      continue;
    }
    for_each_in_driver(sink_idx, [&](size_t driver_idx) {
      if (!backward_is_sink(driver_idx)) {
        ++remaining_out[driver_idx];
      }
    });
  }

  // Full Pass 1 + Pass 2 dry run to populate backward_pass2_cache_. Uses a
  // working copy so `remaining_out` (the cache) keeps its initial values.
  std::vector<uint32_t> working = remaining_out;
  std::vector<uint64_t> emitted_bits((node_count + 63) / 64, 0);
  auto                  mark_emit = [&](size_t idx) { emitted_bits[idx >> 6] |= (1ULL << (idx & 63)); };
  auto                  is_emit   = [&](size_t idx) { return (emitted_bits[idx >> 6] >> (idx & 63)) & 1ULL; };

  auto propagate = [&](size_t sink_idx, size_t cursor) {
    if (backward_is_sink(sink_idx)) {
      return;
    }
    for_each_in_driver(sink_idx, [&](size_t driver_idx) {
      if (is_emit(driver_idx) || backward_is_sink(driver_idx)) {
        return;
      }
      if (working[driver_idx] == 0) {
        return;
      }
      --working[driver_idx];
      if (working[driver_idx] == 0 && driver_idx >= cursor) {
        backward_pass2_cache_.push_back(static_cast<Nid>(driver_idx) << 2);
      }
    });
  };

  for (size_t idx = node_count; idx > kFirstUserNodeIdx;) {
    --idx;
    if (!node_table[idx].is_alive() || is_emit(idx)) {
      continue;
    }
    if (backward_is_sink(idx) || working[idx] == 0) {
      mark_emit(idx);
      propagate(idx, idx);
    }
  }

  for (size_t head = 0; head < backward_pass2_cache_.size(); ++head) {
    const size_t idx = static_cast<size_t>(backward_pass2_cache_[head] >> 2);
    if (is_emit(idx)) {
      continue;
    }
    mark_emit(idx);
    propagate(idx, 0);  // Propagate backwards with cursor=0 so all deferrals are added
  }

  backward_caches_valid_ = true;
}

void Graph::patch_traversal_caches_for_edge(Vid driver_id, Vid sink_id, int32_t delta) noexcept {
  if (!forward_caches_valid_ && !backward_caches_valid_) {
    return;
  }

  auto master_idx_of = [&](Vid vid) -> size_t {
    Nid nid;
    if (vid & static_cast<Vid>(1)) {
      const Pid pid = (static_cast<Pid>(vid) & ~static_cast<Pid>(2)) | static_cast<Pid>(1);
      nid           = ref_pin(pid)->get_master_nid();
    } else {
      nid = static_cast<Nid>(vid);
    }
    nid = nid & ~static_cast<Nid>(3);
    return static_cast<size_t>(nid >> 2);
  };

  const size_t driver_idx = master_idx_of(driver_id);
  const size_t sink_idx   = master_idx_of(sink_id);

  if (forward_caches_valid_) {
    const size_t n = forward_remaining_in_cache_.size();
    if (driver_idx >= kFirstUserNodeIdx && driver_idx < n && sink_idx >= kFirstUserNodeIdx && sink_idx < n
        && !forward_is_source(driver_idx) && !forward_is_source(sink_idx)) {
      auto& slot = forward_remaining_in_cache_[sink_idx];
      if (delta > 0) {
        slot += static_cast<uint32_t>(delta);
      } else {
        const auto dec = 0u - static_cast<uint32_t>(delta);  // magnitude in unsigned space: avoids UB at INT32_MIN
        if (slot >= dec) {
          slot -= dec;
        } else {
          forward_caches_valid_ = false;
        }
      }
    }
  }

  if (backward_caches_valid_) {
    const size_t n = backward_remaining_out_cache_.size();
    if (driver_idx >= kFirstUserNodeIdx && driver_idx < n && sink_idx >= kFirstUserNodeIdx && sink_idx < n
        && !backward_is_sink(driver_idx) && !backward_is_sink(sink_idx)) {
      auto& slot = backward_remaining_out_cache_[driver_idx];
      if (delta > 0) {
        slot += static_cast<uint32_t>(delta);
      } else {
        const auto dec = 0u - static_cast<uint32_t>(delta);  // magnitude in unsigned space: avoids UB at INT32_MIN
        if (slot >= dec) {
          slot -= dec;
        } else {
          backward_caches_valid_ = false;
        }
      }
    }
  }

  dirty_ = true;
  if (owner_lib_ != nullptr) {
    owner_lib_->note_graph_mutation();
  }
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
  nid                  &= ~static_cast<Nid>(2);  // PinEntry ownership is by node identity, independent of edge role bit.
  const Nid actual_nid  = nid >> 2;
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
  pin.context_   = node.context_;
  pin.root_gid_  = node.root_gid_;
  pin.hier_pos_  = node.hier_pos_;
  pin.hier_path_ = node.hier_path_;
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
    const Pid  canonical_pin = (cur_pin & ~static_cast<Pid>(2)) | static_cast<Pid>(1);
    auto*      pin           = ref_pin(canonical_pin);
    const auto cur_port      = pin->get_port_id();
    if (cur_port == port_id) {
      return make_pin_class(canonical_pin);
    }
    if (cur_port > port_id) {
      break;  // sorted list: target port_id cannot appear later
    }
    cur_pin = pin->get_next_pin_id();
  }
  assert(false && "get_pin: requested pin was not created");
  return {};
}

auto Graph::find_or_create_pin(Node_class node, Port_id port_id) -> Pin_class {
  assert_node_exists(node);
  assert(port_id != 0 && "find_or_create_pin: port_id 0 is the node itself");
  const Nid self_nid    = node.get_debug_nid() & ~static_cast<Nid>(2);
  auto*     self        = ref_node(self_nid);
  // The pin linked list is kept sorted by ascending port_id. Find the predecessor whose
  // port_id is just below `port_id`, and stop early if a greater-or-equal port_id is found.
  Pid       prev_pin_id = 0;  // canonical Pid of predecessor (0 = insert at head)
  Pid       cur_pin     = self->get_next_pin_id();
  while (cur_pin != 0) {
    const Pid  canonical_pin = (cur_pin & ~static_cast<Pid>(2)) | static_cast<Pid>(1);
    auto*      pin           = ref_pin(canonical_pin);
    const auto cur_port      = pin->get_port_id();
    if (cur_port == port_id) {
      return make_pin_class(canonical_pin);
    }
    if (cur_port > port_id) {
      break;  // insertion point: new pin goes before cur_pin
    }
    prev_pin_id = canonical_pin;
    cur_pin     = pin->get_next_pin_id();
  }
  // Pin not found: insert before cur_pin (which is 0 when appending at the tail).
  assert_accessible();
  const Pid new_pid_raw = static_cast<Pid>(pin_table.size());
  assert(new_pid_raw);
  pin_table.emplace_back(self_nid, port_id);
  // After emplace_back, pin pointers (e.g. via ref_pin) may have been invalidated,
  // so we look up by index.
  const Pid new_pid_canonical = (new_pid_raw << 2) | static_cast<Pid>(1);
  pin_table[new_pid_raw].set_next_pin_id(cur_pin);
  if (prev_pin_id == 0) {
    node_table[self_nid >> 2].set_next_pin_id(new_pid_canonical);
  } else {
    pin_table[prev_pin_id >> 2].set_next_pin_id(new_pid_canonical);
  }
  invalidate_traversal_caches();
  return Pin_class(this, new_pid_canonical);
}

auto Graph::resolve_driver_port(Node_class node, std::string_view name) const -> Port_id {
  assert_node_exists(node);
  const auto* entry = ref_node(node.get_debug_nid());
  assert(entry->has_subnode() && "create_driver_pin: string form requires a subnode GraphIO");
  assert(owner_lib_ != nullptr && "create_driver_pin: graph has no GraphLibrary");
  auto gio = owner_lib_->io_at_unlocked(entry->get_subnode());
  assert(gio != nullptr && gio->has_output(name) && "create_driver_pin: output name not found in subnode GraphIO");
  return gio->get_output_port_id(name);
}

auto Graph::resolve_sink_port(Node_class node, std::string_view name) const -> Port_id {
  assert_node_exists(node);
  const auto* entry = ref_node(node.get_debug_nid());
  assert(entry->has_subnode() && "create_sink_pin: string form requires a subnode GraphIO");
  assert(owner_lib_ != nullptr && "create_sink_pin: graph has no GraphLibrary");
  auto gio = owner_lib_->io_at_unlocked(entry->get_subnode());
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
    auto gio = owner_lib_->io_at_unlocked(owner->get_subnode());
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
  const auto it = input_pins_.find(name);  // transparent Ci_hash/Ci_eq: no std::string alloc
  assert(it != input_pins_.end() && "get_input_pin: declared input name not found");
  if (it == input_pins_.end()) {
    return {};
  }
  return make_pin_class(it->second | static_cast<Pid>(2));
}

auto Graph::get_output_pin(std::string_view name) const -> Pin_class {
  assert_accessible();
  const auto it = output_pins_.find(name);  // transparent Ci_hash/Ci_eq: no std::string alloc
  assert(it != output_pins_.end() && "get_output_pin: declared output name not found");
  if (it == output_pins_.end()) {
    return {};
  }
  return make_pin_class(it->second);
}

auto Graph::materialize_declared_io_pin(std::string_view name, Port_id port_id, Nid owner_nid,
                                        ankerl::unordered_dense::map<std::string, Pid, Ci_hash, Ci_eq>& pins_by_name) -> Pid {
  assert_accessible();
  assert(!name.empty() && "materialize_declared_io_pin: name is required");

  const auto it = pins_by_name.find(name);  // transparent Ci_hash/Ci_eq: no std::string alloc
  if (it != pins_by_name.end()) {
    return it->second;
  }

  const Pid pin_pid = create_pin(owner_nid, port_id);
  pins_by_name.emplace(std::string(name), pin_pid);
  return pin_pid;
}

void Graph::erase_declared_io_pin(std::string_view                                                name,
                                  ankerl::unordered_dense::map<std::string, Pid, Ci_hash, Ci_eq>& pins_by_name) {
  assert_accessible();
  const auto it = pins_by_name.find(name);  // transparent Ci_hash/Ci_eq: no std::string alloc
  assert(it != pins_by_name.end() && "erase_declared_io_pin: declared pin name not found");
  if (it == pins_by_name.end()) {
    return;
  }

  // delete_pin below handles edge teardown — declared IO pins are wiped
  // wholesale by GraphIO::reset_declarations, including any edges they still
  // carry from the prior build (e.g., when a LiveHD test reuses an Lgraph
  // across cases and clear_int reruns reset_declarations).
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

  // Remove the reverse (back) edge each neighbor stores pointing at this pin.
  // delete_edge is the canonical primitive: it covers every storage regime
  // (inline sedges + the 3 NodeEntry sedges_extra slots + ledge0/ledge1 +
  // overflow set) for both PinEntry and NodeEntry. The previous hand-rolled
  // inline scan only inspected the 4 sedges_ slots, so a node back-edge that
  // spilled into sedges_extra was left dangling after the pin was zeroed.
  auto pool = get_overflow_pool();
  for (auto other_vid : edges_to_remove) {
    // other_vid drives us (bit 2 set) -> its back edge is this pin as a sink
    // (pin_lookup); otherwise we drive it and the back edge is this pin as a
    // driver (pin_lookup | 2).
    const Vid reverse_edge = (other_vid & static_cast<Vid>(2)) ? pin_lookup : (pin_lookup | static_cast<Vid>(2));
    if (other_vid & static_cast<Vid>(1)) {
      (void)ref_pin(other_vid)->delete_edge(other_vid, reverse_edge, pool);
    } else {
      (void)ref_node(other_vid)->delete_edge(other_vid, reverse_edge, pool);
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

  // Incremental traversal-cache patches per removed edge. Done before the pin
  // entry is zeroed so master_nid lookup inside the patch helper still works.
  for (auto other_vid : edges_to_remove) {
    Vid driver_vid;
    Vid sink_vid;
    if (other_vid & static_cast<Vid>(2)) {
      driver_vid = other_vid;
      sink_vid   = pin_lookup;
    } else {
      driver_vid = pin_lookup | static_cast<Vid>(2);
      sink_vid   = other_vid;
    }
    patch_traversal_caches_for_edge(driver_vid, sink_vid, -1);
  }

  pin_table[actual_id] = PinEntry();
}

auto Pin_class::get_master_node() const -> Node_class {
  const Nid nid = get_debug_nid();
  if (context_ == Handle_context::Flat) {
    return Node_class(graph_, root_gid_, nid);
  }
  if (context_ == Handle_context::Hier) {
    return Node_class(graph_, root_gid_, hier_pos_, nid, hier_path_);  // keep the full instance chain
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

Gid Node_class::get_subnode_gid() const {
  assert(graph_ != nullptr && "get_subnode_gid: node is not attached to a graph");
  const auto* entry = graph_->ref_node(raw_nid);
  if (entry == nullptr || !entry->has_subnode()) {
    return Gid_invalid;
  }
  return entry->get_subnode();
}

std::shared_ptr<GraphIO> Node_class::get_subnode_io() const {
  assert(graph_ != nullptr && "get_subnode_io: node is not attached to a graph");
  if (graph_->owner_lib_ == nullptr) {
    return {};
  }
  const Gid gid = get_subnode_gid();
  if (gid == Gid_invalid) {
    return {};
  }
  auto* lib = const_cast<GraphLibrary*>(graph_->owner_lib_);
  return lib->find_io(gid);
}

std::shared_ptr<Graph> Node_class::get_subnode_graph() const {
  auto gio = get_subnode_io();
  if (!gio) {
    return {};
  }
  return gio->get_graph();
}

void Node_class::set_type(Type type) const {
  assert(graph_ != nullptr && "set_type: node is not attached to a graph");
  graph_->ref_node(raw_nid)->set_type(type);
  graph_->invalidate_traversal_caches();
}

Type Node_class::get_type() const {
  assert(graph_ != nullptr && "get_type: node is not attached to a graph");
  return graph_->ref_node(raw_nid)->get_type();
}

bool Node_class::is_loop_break() const {
  assert(graph_ != nullptr && "is_loop_break: node is not attached to a graph");
  return graph_->ref_node(raw_nid)->is_loop_break();
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
  auto pin     = graph_->find_or_create_pin(*this, port_id);
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
  auto pin     = graph_->find_or_create_pin(*this, port_id);
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
  auto pin     = graph_->find_pin(*this, port_id, true);
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

auto Node_class::out_pins() const -> absl::InlinedVector<Pin_class, 4> {
  assert(graph_ != nullptr && "out_pins: node is not attached to a graph");
  return graph_->get_driver_pins(*this);
}

auto Node_class::inp_pins() const -> absl::InlinedVector<Pin_class, 4> {
  assert(graph_ != nullptr && "inp_pins: node is not attached to a graph");
  return graph_->get_sink_pins(*this);
}

bool Node_class::has_out_edges() const {
  assert(graph_ != nullptr && "has_out_edges: node is not attached to a graph");
  const Nid self_nid = raw_nid & ~static_cast<Nid>(2);
  auto*     self     = graph_->ref_node(self_nid);
  // Node-as-pin (port 0): scan node-entry edges, skip back-edges (bit 1 = sink).
  for (auto vid : self->get_edges(self_nid, graph_->overflow_sets_)) {
    if (!(vid & static_cast<Vid>(2))) {
      return true;
    }
  }
  // Other pins: walk the pin linked list.
  for (Pid cur_pin = self->get_next_pin_id(); cur_pin != 0;) {
    const Pid canonical_pin = (cur_pin & ~static_cast<Pid>(2)) | static_cast<Pid>(1);
    auto*     pin_entry     = graph_->ref_pin(canonical_pin);
    for (auto vid : pin_entry->get_edges(canonical_pin, graph_->overflow_sets_)) {
      if (!(vid & static_cast<Vid>(2))) {
        return true;
      }
    }
    cur_pin = pin_entry->get_next_pin_id();
  }
  return false;
}

bool Node_class::has_inp_edges() const {
  assert(graph_ != nullptr && "has_inp_edges: node is not attached to a graph");
  const Nid self_nid = raw_nid & ~static_cast<Nid>(2);
  auto*     self     = graph_->ref_node(self_nid);
  // Node-as-pin (port 0): scan node-entry edges, keep back-edges (bit 1 = sink).
  for (auto vid : self->get_edges(self_nid, graph_->overflow_sets_)) {
    if (vid & static_cast<Vid>(2)) {
      return true;
    }
  }
  // Other pins.
  for (Pid cur_pin = self->get_next_pin_id(); cur_pin != 0;) {
    const Pid canonical_pin = (cur_pin & ~static_cast<Pid>(2)) | static_cast<Pid>(1);
    auto*     pin_entry     = graph_->ref_pin(canonical_pin);
    for (auto vid : pin_entry->get_edges(canonical_pin, graph_->overflow_sets_)) {
      if (vid & static_cast<Vid>(2)) {
        return true;
      }
    }
    cur_pin = pin_entry->get_next_pin_id();
  }
  return false;
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
    auto gio = owner_lib_->io_at_unlocked(gid);
    assert(gio != nullptr);
    if (gio == nullptr) {
      return;
    }
    subnode_gio = gio.get();
  }

  // Debug-only structural cycle check. Cycles are explicitly disallowed —
  // they make hier traversal nonsensical (and previously could infinite-loop
  // fast_hier/forward_hier, which have no runtime guard). Catching at the
  // call site that creates the cycle gives a localized failure instead of
  // an infinite loop deep inside an iterator. Compiled out under NDEBUG.
  assert(!would_create_cycle(gid) && "set_subnode: structure-tree cycle detected");

  nid       &= ~static_cast<Nid>(3);
  auto pool  = get_overflow_pool();
  ref_node(nid)->set_subnode(nid, gid, pool);

  // Persistent hierarchy: add a child to this graph's tree representing
  // this subnode instance. Only add if not already tracked (re-calling
  // set_subnode on the same node just updates the target Gid).
  if (tree_ && subnode_tree_pos_.find(nid) == subnode_tree_pos_.end()) {
    const Tree_pos child_pos = tree_->add_child(static_cast<Tree_pos>(ROOT));
    subnode_tree_pos_.emplace(nid, child_pos);
    tree_pos_to_nid_.emplace(child_pos, nid);
  }

  // Stamp node type so the forward iterator can O(1) tell whether this
  // subnode is a loop_break boundary (a cut-point for forward/backward
  // ordering). Bit 0 of Type encodes is_loop_break (odd == loop_break).
  //
  // The order is computed per-body from local edges and never descends, so a
  // sub-instance of a sequential module (inputs -> internal flop -> outputs)
  // is only cut if the *instance node itself* carries the bit. An instance is
  // a loop_break iff:
  //   (a) the subnode declares a loop_break boundary pin -- the only signal
  //       available for an ABSENT body (true blackbox, e.g. a liberty cell), or
  //   (b) the subnode's PRESENT body contains any loop_break cell (flop /
  //       memory / latch, or a nested loop_break sub-instance). This realizes
  //       "the flop inside the module is the loop break" without requiring the
  //       producer to annotate boundary pins.
  if (subnode_gio != nullptr) {
    bool has_loop_break = false;
    for (const auto& decl : subnode_gio->input_pin_decls_) {
      if (decl.loop_break) {
        has_loop_break = true;
        break;
      }
    }
    if (!has_loop_break) {
      for (const auto& decl : subnode_gio->output_pin_decls_) {
        if (decl.loop_break) {
          has_loop_break = true;
          break;
        }
      }
    }
    if (!has_loop_break && subnode_gio->has_graph()) {
      // Scan the present body. Built bottom-up (children stamped before parents),
      // an already-stamped nested loop_break sub-instance is itself is_loop_break,
      // so this composes through wrapper modules. A forward reference whose body
      // is not yet materialized falls through to "not loop_break"; re-running
      // set_subnode once the body exists corrects the stamp.
      if (auto body = subnode_gio->get_graph(); body != nullptr) {
        for (auto bn : body->fast_class()) {
          if (bn.is_loop_break()) {
            has_loop_break = true;
            break;
          }
        }
      }
    }
    ref_node(nid)->set_type(has_loop_break ? static_cast<Type>(3) : static_cast<Type>(2));
  }

  invalidate_traversal_caches();
}

bool Graph::would_create_cycle(Gid target_gid) const noexcept {
  if (target_gid == Gid_invalid || self_gid_ == Gid_invalid || owner_lib_ == nullptr) {
    // Orphan or unbound graphs have no library context to walk; the runtime
    // active_graphs_ guard in HierIterator covers them as a fallback.
    return false;
  }
  if (target_gid == self_gid_) {
    return true;  // direct self-instantiation
  }

  ankerl::unordered_dense::set<Gid> visited;
  std::vector<Gid>                  stack;
  stack.push_back(target_gid);
  while (!stack.empty()) {
    const Gid current = stack.back();
    stack.pop_back();
    if (current == self_gid_) {
      return true;
    }
    if (!visited.insert(current).second) {
      continue;
    }
    if (!owner_lib_->has_graph(current)) {
      continue;
    }
    auto graph = owner_lib_->get_graph(current);
    if (!graph) {
      continue;
    }
    // Walking subnode_tree_pos_ touches one entry per live submodule
    // instance (≪ node_table size). Stale entries left by delete_node are
    // gated by is_alive() + has_subnode() — same defensive check
    // HierIterator uses.
    for (const auto& [nid, tree_pos] : graph->subnode_tree_pos_) {
      (void)tree_pos;
      const auto* entry = graph->ref_node(nid);
      if (!entry->is_alive() || !entry->has_subnode()) {
        continue;
      }
      stack.push_back(entry->get_subnode());
    }
  }
  return false;
}

void Graph::add_edge(Vid driver_id, Vid sink_id) {
  assert_accessible();
  driver_id = driver_id | 2;
  sink_id   = sink_id & ~2;
  add_edge_int(driver_id, sink_id);
  add_edge_int(sink_id, driver_id);
  patch_traversal_caches_for_edge(driver_id, sink_id, +1);
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
  const Vid driver_vid = static_cast<Vid>(driver_pin.get_debug_pid()) | static_cast<Vid>(2);
  const Vid sink_vid   = static_cast<Vid>(sink_pin.get_debug_pid()) & ~static_cast<Vid>(2);
  del_edge_int(driver_pin.get_debug_pid(), sink_pin.get_debug_pid());
  patch_traversal_caches_for_edge(driver_vid, sink_vid, -1);
}

FastClassRange Graph::fast_class() const noexcept { return FastClassRange(const_cast<Graph*>(this)); }

ForwardClassRange Graph::forward_class(bool loop_break_first, bool loop_break_last) const noexcept {
  assert_accessible();
  return ForwardClassRange(const_cast<Graph*>(this), loop_break_first, loop_break_last);
}

BackwardClassRange Graph::backward_class(bool loop_break_first, bool loop_break_last) const noexcept {
  assert_accessible();
  return BackwardClassRange(const_cast<Graph*>(this), loop_break_first, loop_break_last);
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
  return FastClassIterator(graph_, kFirstUserNodeIdx, graph_->node_table.size());
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
  stack_.push_back(Frame{root_graph, kFirstUserNodeIdx, root_graph->node_table.size()});
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
      stack_.push_back(Frame{child_graph, kFirstUserNodeIdx, child_graph->node_table.size()});
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
  // structure-tree root) and an empty instance chain.
  stack_.push_back(Frame{root_graph,
                         kFirstUserNodeIdx,
                         root_graph->node_table.size(),
                         static_cast<Tree_pos>(ROOT),
                         std::make_shared<std::vector<Nid>>()});
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
  return Node_class(frame.graph, root_gid_, frame.hier_pos, raw_nid, frame.path);
}

auto FastHierIterator::operator++() -> FastHierIterator& {
  Frame&      frame = stack_.back();
  const auto& entry = frame.graph->node_table[frame.node_idx];
  if (entry.has_subnode() && frame.graph->owner_lib_ != nullptr) {
    const Gid   sub = entry.get_subnode();
    const auto* lib = frame.graph->owner_lib_;
    if (lib->has_graph(sub) && active_graphs_.find(sub) == active_graphs_.end()) {
      Graph*         child_graph = const_cast<Graph*>(lib->get_graph(sub).get());
      const Nid      subnode_nid = static_cast<Nid>(frame.node_idx) << 2;
      // Stable Tree_pos from the structure tree that set_subnode built.
      auto           it          = frame.graph->subnode_tree_pos_.find(subnode_nid);
      const Tree_pos child_pos   = (it != frame.graph->subnode_tree_pos_.end()) ? it->second : static_cast<Tree_pos>(ROOT);
      auto           child_path  = std::make_shared<std::vector<Nid>>(*frame.path);
      child_path->push_back(subnode_nid);
      ++frame.node_idx;
      active_graphs_.insert(sub);
      stack_.push_back(Frame{child_graph, kFirstUserNodeIdx, child_graph->node_table.size(), child_pos, std::move(child_path)});
      advance();
      return *this;
    }
  }
  ++frame.node_idx;
  advance();
  return *this;
}

FastHierIterator FastHierRange::begin() const { return FastHierIterator(graph_); }

ForwardFlatRange Graph::forward_flat(bool loop_break_first, bool loop_break_last) const noexcept {
  assert_accessible();
  return ForwardFlatRange(const_cast<Graph*>(this), loop_break_first, loop_break_last);
}

ForwardHierRange Graph::forward_hier(bool loop_break_first, bool loop_break_last) const noexcept {
  assert_accessible();
  return ForwardHierRange(const_cast<Graph*>(this), loop_break_first, loop_break_last);
}

BackwardFlatRange Graph::backward_flat(bool loop_break_first, bool loop_break_last) const noexcept {
  assert_accessible();
  return BackwardFlatRange(const_cast<Graph*>(this), loop_break_first, loop_break_last);
}

BackwardHierRange Graph::backward_hier(bool loop_break_first, bool loop_break_last) const noexcept {
  assert_accessible();
  return BackwardHierRange(const_cast<Graph*>(this), loop_break_first, loop_break_last);
}

// --- ForwardClassIterator ---
//
// The iterator replays the topological emission order using the cached Pass-2
// Nid list and initial in-edge counts. Pass 1 scans storage order with a
// working copy of in-edge counts (so multiple iterators can coexist without
// clobbering the cache). Pass 2 reads the cache directly. Tail re-scans
// storage order for cycle survivors.

ForwardClassIterator::ForwardClassIterator(Graph* graph, bool loop_break_first, bool loop_break_last)
    : graph_(graph), loop_break_first_(loop_break_first), loop_break_last_(loop_break_last) {
  if (graph_ == nullptr) {
    phase_ = Phase::End;
    return;
  }
  graph_->ensure_forward_caches();
  node_count_ = graph_->node_table.size();
  if (node_count_ <= kFirstUserNodeIdx) {
    phase_ = Phase::End;
    return;
  }
  working_remaining_in_ = graph_->forward_remaining_in_cache_;
  emitted_bits_.assign((node_count_ + 63) / 64, 0);
  phase_ = Phase::Pass1;
  idx_   = kFirstUserNodeIdx;
  advance();
}

ForwardClassIterator::ForwardClassIterator(ForwardClassIterator&& o) noexcept            = default;
ForwardClassIterator& ForwardClassIterator::operator=(ForwardClassIterator&& o) noexcept = default;

bool ForwardClassIterator::is_source(size_t idx) const noexcept { return graph_->forward_is_source(idx); }

bool ForwardClassIterator::is_emitted(size_t idx) const noexcept { return (emitted_bits_[idx >> 6] >> (idx & 63)) & 1ULL; }

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
    if (sink_idx < kFirstUserNodeIdx || sink_idx >= node_count_) {
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

  // Inline edge iteration: avoid building an EdgeRange (which allocates a
  // scratch vector) for every node/pin we walk. For forward propagation we
  // only care about outgoing edges (bit 2 == 0), so we can decode slots
  // directly and skip incoming edges without ever materializing them.
  constexpr uint64_t SLOT_MASK  = (1ULL << 16) - 1;
  constexpr uint64_t SIGN_BIT   = 1ULL << 15;
  constexpr uint64_t DRIVER_BIT = 1ULL << 1;
  constexpr uint64_t PIN_BIT    = 1ULL << 0;
  constexpr uint64_t MAG_MASK   = (1ULL << 13) - 1;

  auto decode_inline_slot = [&](uint64_t raw, uint64_t self_num) -> Vid {
    const bool     neg        = (raw & SIGN_BIT) != 0;
    const bool     driver     = (raw & DRIVER_BIT) != 0;
    const bool     pin        = (raw & PIN_BIT) != 0;
    const uint64_t mag        = (raw >> 2) & MAG_MASK;
    const int64_t  delta      = neg ? -static_cast<int64_t>(mag) : static_cast<int64_t>(mag);
    const uint64_t target_num = self_num - delta;
    return static_cast<Vid>((target_num << 2) | (driver ? DRIVER_BIT : 0) | (pin ? PIN_BIT : 0));
  };

  {
    const auto&    node     = node_table[driver_idx];
    const uint64_t self_num = static_cast<uint64_t>(driver_nid) >> 2;
    if (node.use_overflow) {
      for (auto vid : overflow[node.sedges_.overflow_idx]) {
        if (vid & static_cast<Vid>(2)) {
          continue;
        }
        try_dec(sink_idx_of(vid));
      }
    } else {
      const uint64_t packed = node.sedges_.sedges;
      for (int slot = 0; slot < 4; ++slot) {
        const uint64_t raw = (packed >> (slot * 16)) & SLOT_MASK;
        if (raw == 0 || (raw & DRIVER_BIT) != 0) {
          continue;
        }
        try_dec(sink_idx_of(decode_inline_slot(raw, self_num)));
      }
      const uint64_t extra = node.sedges_extra;
      for (int slot = 0; slot < 3; ++slot) {
        const uint64_t raw = (extra >> (slot * 16)) & SLOT_MASK;
        if (raw == 0 || (raw & DRIVER_BIT) != 0) {
          continue;
        }
        try_dec(sink_idx_of(decode_inline_slot(raw, self_num)));
      }
      if (node.ledge0 && !(node.ledge0 & 2)) {
        try_dec(sink_idx_of(node.ledge0));
      }
      if (node.ledge1 && !(node.ledge1 & 2)) {
        try_dec(sink_idx_of(node.ledge1));
      }
    }
  }
  for (Pid pin_vid = node_table[driver_idx].get_next_pin_id(); pin_vid != 0;) {
    const Pid   canonical_pin = (pin_vid & ~static_cast<Pid>(2)) | static_cast<Pid>(1);
    const auto* pin           = graph_->ref_pin(canonical_pin);
    if (pin->use_overflow) {
      for (auto edge_vid : overflow[pin->sedges_.overflow_idx]) {
        if (edge_vid & static_cast<Vid>(2)) {
          continue;
        }
        try_dec(sink_idx_of(edge_vid));
      }
    } else {
      const uint64_t self_num = static_cast<uint64_t>(canonical_pin) >> 2;
      const uint64_t packed   = pin->sedges_.sedges;
      for (int slot = 0; slot < 4; ++slot) {
        const uint64_t raw = (packed >> (slot * 16)) & SLOT_MASK;
        if (raw == 0 || (raw & DRIVER_BIT) != 0) {
          continue;
        }
        try_dec(sink_idx_of(decode_inline_slot(raw, self_num)));
      }
      if (pin->ledge0 && !(pin->ledge0 & 2)) {
        try_dec(sink_idx_of(pin->ledge0));
      }
      if (pin->ledge1 && !(pin->ledge1 & 2)) {
        try_dec(sink_idx_of(pin->ledge1));
      }
    }
    pin_vid = pin->get_next_pin_id();
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
        const bool src = is_source(i);
        if (src || working_remaining_in_[i] == 0) {
          mark_emitted(i);
          propagate(i, i);
          // loop_break nodes are the only user-range sources. They are always
          // marked here (so Tail skips them) but only yielded now when
          // loop_break_first_; if loop_break_last_, they are replayed in
          // the LoopLast phase instead/also.
          if (src && !loop_break_first_) {
            continue;
          }
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
      idx_   = kFirstUserNodeIdx;
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
      phase_ = loop_break_last_ ? Phase::LoopLast : Phase::End;
      idx_   = kFirstUserNodeIdx;
      if (phase_ == Phase::End) {
        current_idx_ = 0;
        return;
      }
      continue;
    }
    if (phase_ == Phase::LoopLast) {
      // Replay every live loop_break (source) node after all combinational
      // and cycle-tail nodes. Already marked emitted in Pass1, so we test the
      // source predicate directly rather than the emitted bitset.
      while (idx_ < node_count_) {
        const size_t i = idx_++;
        if (!graph_->node_table[i].is_alive() || !is_source(i)) {
          continue;
        }
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

Node_class ForwardClassIterator::operator*() const { return Node_class(graph_, static_cast<Nid>(current_idx_) << 2); }

ForwardClassIterator& ForwardClassIterator::operator++() {
  advance();
  return *this;
}

ForwardClassIterator ForwardClassRange::begin() const { return ForwardClassIterator(graph_, loop_break_first_, loop_break_last_); }

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

ForwardFlatIterator::ForwardFlatIterator(Graph* root_graph, bool loop_break_first, bool loop_break_last)
    : loop_break_first_(loop_break_first), loop_break_last_(loop_break_last) {
  if (root_graph == nullptr) {
    return;
  }
  root_graph->assert_accessible();
  top_graph_ = root_graph->self_gid_;
  if (top_graph_ != Gid_invalid) {
    active_graphs_.insert(top_graph_);
  }
  stack_.push_back(Frame{root_graph, ForwardClassIterator(root_graph, loop_break_first_, loop_break_last_)});
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
  auto&       frame    = stack_.back();
  const Nid   cur_nid  = (*frame.it).get_debug_nid();
  const auto& entry    = frame.graph->node_table[static_cast<size_t>(cur_nid >> 2)];
  const auto* lib      = frame.graph->owner_lib_;
  // A loop_break subnode emitted both first and last (loop_break_first_ &&
  // loop_break_last_) must be descended into only once — on its first
  // emission. Skip the descent when this emission is the LoopLast replay.
  const bool  skip_sub = loop_break_first_ && frame.it.current_is_loop_break_replay();
  ++frame.it;
  if (entry.has_subnode() && lib != nullptr && !skip_sub) {
    const Gid sub = entry.get_subnode();
    if (lib->has_graph(sub) && active_graphs_.find(sub) == active_graphs_.end()) {
      Graph* child = const_cast<Graph*>(lib->get_graph(sub).get());
      active_graphs_.insert(sub);
      stack_.push_back(Frame{child, ForwardClassIterator(child, loop_break_first_, loop_break_last_)});
    }
  }
  advance();
  return *this;
}

ForwardFlatIterator ForwardFlatRange::begin() const { return ForwardFlatIterator(graph_, loop_break_first_, loop_break_last_); }

// --- ForwardHierIterator ---

ForwardHierIterator::ForwardHierIterator(Graph* root_graph, bool loop_break_first, bool loop_break_last)
    : loop_break_first_(loop_break_first), loop_break_last_(loop_break_last) {
  if (root_graph == nullptr) {
    return;
  }
  root_graph->assert_accessible();
  root_gid_ = root_graph->self_gid_;
  if (root_gid_ != Gid_invalid) {
    active_graphs_.insert(root_gid_);
  }
  stack_.push_back(Frame{root_graph,
                         ForwardClassIterator(root_graph, loop_break_first_, loop_break_last_),
                         static_cast<Tree_pos>(ROOT),
                         std::make_shared<std::vector<Nid>>()});
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
  return Node_class(frame.graph, root_gid_, frame.hier_pos, raw_nid, frame.path);
}

ForwardHierIterator& ForwardHierIterator::operator++() {
  auto&       frame    = stack_.back();
  const Nid   cur_nid  = (*frame.it).get_debug_nid();
  const auto& entry    = frame.graph->node_table[static_cast<size_t>(cur_nid >> 2)];
  const auto* lib      = frame.graph->owner_lib_;
  // Descend into a loop_break subnode only on its first emission (see the
  // flat iterator for the rationale); skip on the LoopLast replay.
  const bool  skip_sub = loop_break_first_ && frame.it.current_is_loop_break_replay();
  ++frame.it;
  if (entry.has_subnode() && lib != nullptr && !skip_sub) {
    const Gid sub = entry.get_subnode();
    if (lib->has_graph(sub) && active_graphs_.find(sub) == active_graphs_.end()) {
      Graph*         child      = const_cast<Graph*>(lib->get_graph(sub).get());
      auto           it         = frame.graph->subnode_tree_pos_.find(cur_nid);
      const Tree_pos child_pos  = (it != frame.graph->subnode_tree_pos_.end()) ? it->second : static_cast<Tree_pos>(ROOT);
      auto           child_path = std::make_shared<std::vector<Nid>>(*frame.path);
      child_path->push_back(cur_nid & ~static_cast<Nid>(3));
      active_graphs_.insert(sub);
      stack_.push_back(
          Frame{child, ForwardClassIterator(child, loop_break_first_, loop_break_last_), child_pos, std::move(child_path)});
    }
  }
  advance();
  return *this;
}

ForwardHierIterator ForwardHierRange::begin() const { return ForwardHierIterator(graph_, loop_break_first_, loop_break_last_); }

// --- BackwardClassIterator ---
//
// Replays the reverse topological emission order using backward_pass2_cache_ and
// initial out-edge counts.

BackwardClassIterator::BackwardClassIterator(Graph* graph, bool loop_break_first, bool loop_break_last)
    : graph_(graph), loop_break_first_(loop_break_first), loop_break_last_(loop_break_last) {
  if (graph_ == nullptr) {
    phase_ = Phase::End;
    return;
  }
  graph_->ensure_backward_caches();
  node_count_ = graph_->node_table.size();
  if (node_count_ <= kFirstUserNodeIdx) {
    phase_ = Phase::End;
    return;
  }
  working_remaining_out_ = graph_->backward_remaining_out_cache_;
  emitted_bits_.assign((node_count_ + 63) / 64, 0);
  phase_ = Phase::Pass1;
  idx_   = node_count_;
  advance();
}

BackwardClassIterator::BackwardClassIterator(BackwardClassIterator&& o) noexcept            = default;
BackwardClassIterator& BackwardClassIterator::operator=(BackwardClassIterator&& o) noexcept = default;

bool BackwardClassIterator::is_sink(size_t idx) const noexcept { return graph_->backward_is_sink(idx); }

bool BackwardClassIterator::is_emitted(size_t idx) const noexcept { return (emitted_bits_[idx >> 6] >> (idx & 63)) & 1ULL; }

void BackwardClassIterator::mark_emitted(size_t idx) noexcept { emitted_bits_[idx >> 6] |= (1ULL << (idx & 63)); }

void BackwardClassIterator::propagate(size_t sink_idx, size_t /*cursor*/) {
  if (is_sink(sink_idx)) {
    return;
  }

  auto driver_idx_of = [&](Vid vid) -> size_t {
    Nid driver_nid;
    if (vid & static_cast<Vid>(1)) {
      const Pid driver_pid = (static_cast<Pid>(vid) & ~static_cast<Pid>(2)) | static_cast<Pid>(1);
      driver_nid           = graph_->ref_pin(driver_pid)->get_master_nid();
    } else {
      driver_nid = static_cast<Nid>(vid);
    }
    driver_nid = driver_nid & ~static_cast<Nid>(3);
    return static_cast<size_t>(driver_nid >> 2);
  };

  auto try_dec = [&](size_t driver_idx) {
    if (driver_idx < kFirstUserNodeIdx || driver_idx >= node_count_) {
      return;
    }
    if (is_emitted(driver_idx) || is_sink(driver_idx)) {
      return;
    }
    if (working_remaining_out_[driver_idx] == 0) {
      return;
    }
    --working_remaining_out_[driver_idx];
  };

  const Nid sink_nid   = static_cast<Nid>(sink_idx) << 2;
  auto      node_edges = graph_->node_table[sink_idx].get_edges(sink_nid, graph_->overflow_sets_);
  for (auto vid : node_edges) {
    if (!(vid & static_cast<Vid>(2))) {
      continue;
    }
    try_dec(driver_idx_of(vid));
  }
  for (Pid pin_vid = graph_->node_table[sink_idx].get_next_pin_id(); pin_vid != 0;) {
    const Pid   canonical_pin = (pin_vid & ~static_cast<Pid>(2)) | static_cast<Pid>(1);
    const auto* pin           = graph_->ref_pin(canonical_pin);  // hoist: one lookup per list step (was two)
    for (auto edge_vid : pin->get_edges(canonical_pin, graph_->overflow_sets_)) {
      if (!(edge_vid & static_cast<Vid>(2))) {
        continue;
      }
      try_dec(driver_idx_of(edge_vid));
    }
    pin_vid = pin->get_next_pin_id();
  }
}

void BackwardClassIterator::advance() {
  while (true) {
    if (phase_ == Phase::Pass1) {
      while (idx_ > kFirstUserNodeIdx) {
        const size_t i = --idx_;
        if (!graph_->node_table[i].is_alive() || is_emitted(i)) {
          continue;
        }
        const bool snk = is_sink(i);
        if (snk || working_remaining_out_[i] == 0) {
          mark_emitted(i);
          propagate(i, i);
          // loop_break nodes are the only user-range sinks. Mirror the forward
          // iterator: always mark here, but yield now only when
          // loop_break_first_; otherwise replay them in the LoopLast phase.
          if (snk && !loop_break_first_) {
            continue;
          }
          current_idx_ = i;
          return;
        }
      }
      phase_      = Phase::Pass2;
      pass2_head_ = 0;
      continue;
    }
    if (phase_ == Phase::Pass2) {
      const auto& cache = graph_->backward_pass2_cache_;
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
      idx_   = node_count_;
      continue;
    }
    if (phase_ == Phase::Tail) {
      while (idx_ > kFirstUserNodeIdx) {
        const size_t i = --idx_;
        if (!graph_->node_table[i].is_alive() || is_emitted(i)) {
          continue;
        }
        mark_emitted(i);
        current_idx_ = i;
        return;
      }
      phase_ = loop_break_last_ ? Phase::LoopLast : Phase::End;
      idx_   = node_count_;
      if (phase_ == Phase::End) {
        current_idx_ = 0;
        return;
      }
      continue;
    }
    if (phase_ == Phase::LoopLast) {
      // Replay every live loop_break (sink) node after all combinational and
      // cycle-tail nodes, in the same high→low order as Pass1.
      while (idx_ > kFirstUserNodeIdx) {
        const size_t i = --idx_;
        if (!graph_->node_table[i].is_alive() || !is_sink(i)) {
          continue;
        }
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

Node_class BackwardClassIterator::operator*() const { return Node_class(graph_, static_cast<Nid>(current_idx_) << 2); }

BackwardClassIterator& BackwardClassIterator::operator++() {
  advance();
  return *this;
}

BackwardClassIterator BackwardClassRange::begin() const {
  return BackwardClassIterator(graph_, loop_break_first_, loop_break_last_);
}

size_t BackwardClassRange::size() const {
  size_t n = 0;
  for (auto it = begin(); it != end(); ++it) {
    ++n;
  }
  return n;
}

Node_class BackwardClassRange::front() const {
  auto it = begin();
  assert(it != end() && "BackwardClassRange::front() on empty range");
  return *it;
}

bool BackwardClassRange::empty() const { return begin() == end(); }

// --- BackwardFlatIterator ---

BackwardFlatIterator::BackwardFlatIterator(Graph* root_graph, bool loop_break_first, bool loop_break_last)
    : loop_break_first_(loop_break_first), loop_break_last_(loop_break_last) {
  if (root_graph == nullptr) {
    return;
  }
  root_graph->assert_accessible();
  top_graph_ = root_graph->self_gid_;
  if (top_graph_ != Gid_invalid) {
    active_graphs_.insert(top_graph_);
  }
  stack_.push_back(Frame{root_graph, BackwardClassIterator(root_graph, loop_break_first_, loop_break_last_)});
  advance();
}

void BackwardFlatIterator::advance() {
  while (!stack_.empty()) {
    auto& frame = stack_.back();
    if (frame.it == BackwardClassIterator{}) {
      stack_.pop_back();
      continue;
    }
    return;  // positioned
  }
}

Node_class BackwardFlatIterator::operator*() const {
  const auto& frame   = stack_.back();
  const Nid   raw_nid = (*frame.it).get_debug_nid();
  return Node_class(frame.graph, top_graph_, raw_nid);
}

BackwardFlatIterator& BackwardFlatIterator::operator++() {
  auto&       frame    = stack_.back();
  const Nid   cur_nid  = (*frame.it).get_debug_nid();
  const auto& entry    = frame.graph->node_table[static_cast<size_t>(cur_nid >> 2)];
  const auto* lib      = frame.graph->owner_lib_;
  const bool  skip_sub = loop_break_first_ && frame.it.current_is_loop_break_replay();
  ++frame.it;
  if (entry.has_subnode() && lib != nullptr && !skip_sub) {
    const Gid sub = entry.get_subnode();
    if (lib->has_graph(sub) && active_graphs_.find(sub) == active_graphs_.end()) {
      Graph* child = const_cast<Graph*>(lib->get_graph(sub).get());
      active_graphs_.insert(sub);
      stack_.push_back(Frame{child, BackwardClassIterator(child, loop_break_first_, loop_break_last_)});
    }
  }
  advance();
  return *this;
}

BackwardFlatIterator BackwardFlatRange::begin() const { return BackwardFlatIterator(graph_, loop_break_first_, loop_break_last_); }

// --- BackwardHierIterator ---

BackwardHierIterator::BackwardHierIterator(Graph* root_graph, bool loop_break_first, bool loop_break_last)
    : loop_break_first_(loop_break_first), loop_break_last_(loop_break_last) {
  if (root_graph == nullptr) {
    return;
  }
  root_graph->assert_accessible();
  root_gid_ = root_graph->self_gid_;
  if (root_gid_ != Gid_invalid) {
    active_graphs_.insert(root_gid_);
  }
  stack_.push_back(Frame{root_graph,
                         BackwardClassIterator(root_graph, loop_break_first_, loop_break_last_),
                         static_cast<Tree_pos>(ROOT),
                         std::make_shared<std::vector<Nid>>()});
  advance();
}

void BackwardHierIterator::advance() {
  while (!stack_.empty()) {
    auto& frame = stack_.back();
    if (frame.it == BackwardClassIterator{}) {
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

Node_class BackwardHierIterator::operator*() const {
  const auto& frame   = stack_.back();
  const Nid   raw_nid = (*frame.it).get_debug_nid();
  return Node_class(frame.graph, root_gid_, frame.hier_pos, raw_nid, frame.path);
}

BackwardHierIterator& BackwardHierIterator::operator++() {
  auto&       frame    = stack_.back();
  const Nid   cur_nid  = (*frame.it).get_debug_nid();
  const auto& entry    = frame.graph->node_table[static_cast<size_t>(cur_nid >> 2)];
  const auto* lib      = frame.graph->owner_lib_;
  const bool  skip_sub = loop_break_first_ && frame.it.current_is_loop_break_replay();
  ++frame.it;
  if (entry.has_subnode() && lib != nullptr && !skip_sub) {
    const Gid sub = entry.get_subnode();
    if (lib->has_graph(sub) && active_graphs_.find(sub) == active_graphs_.end()) {
      Graph*         child      = const_cast<Graph*>(lib->get_graph(sub).get());
      auto           it         = frame.graph->subnode_tree_pos_.find(cur_nid);
      const Tree_pos child_pos  = (it != frame.graph->subnode_tree_pos_.end()) ? it->second : static_cast<Tree_pos>(ROOT);
      auto           child_path = std::make_shared<std::vector<Nid>>(*frame.path);
      child_path->push_back(cur_nid & ~static_cast<Nid>(3));
      active_graphs_.insert(sub);
      stack_.push_back(
          Frame{child, BackwardClassIterator(child, loop_break_first_, loop_break_last_), child_pos, std::move(child_path)});
    }
  }
  advance();
  return *this;
}

BackwardHierIterator BackwardHierRange::begin() const { return BackwardHierIterator(graph_, loop_break_first_, loop_break_last_); }

// --- Hier_instance members ---

Gid Hier_instance::get_target_gid() const {
  if (parent_graph_ == nullptr || !parent_graph_->is_node_valid(parent_nid_)) {
    return Gid_invalid;
  }
  const auto* entry = parent_graph_->ref_node(parent_nid_);
  if (!entry->has_subnode()) {
    return Gid_invalid;
  }
  return entry->get_subnode();
}

std::shared_ptr<Graph> Hier_instance::get_target_graph() const {
  const Gid target = get_target_gid();
  if (target == Gid_invalid || parent_graph_ == nullptr || parent_graph_->owner_lib_ == nullptr) {
    return {};
  }
  if (!parent_graph_->owner_lib_->has_graph(target)) {
    return {};
  }
  // owner_lib_ is a const GraphLibrary* on Graph (libraries are immutable from
  // the graph's perspective) — cast to get the non-const get_graph overload
  // since Hier_instance is designed to hand out a mutable handle for
  // navigation/mutation, consistent with how Graph::get_io returns a
  // mutable GraphIO.
  auto* lib = const_cast<GraphLibrary*>(parent_graph_->owner_lib_);
  return lib->get_graph(target);
}

Node_class Hier_instance::get_parent_node() const {
  if (!is_valid()) {
    return Node_class();
  }
  // Build a hier-context Node_class matching the key that fast_hier/forward_hier
  // would assign to this same subnode node during their traversal — hier_pos
  // is the parent frame's hier_pos, not this instance's own tree_pos.
  return Node_class(parent_graph_, root_gid_, hier_pos_, parent_nid_);
}

bool Hier_instance::is_valid() const noexcept {
  if (parent_graph_ == nullptr) {
    return false;
  }
  if (!parent_graph_->is_node_valid(parent_nid_)) {
    return false;
  }
  const auto* entry = parent_graph_->ref_node(parent_nid_);
  return entry->has_subnode();
}

// --- HierIterator / HierRange ---
//
// The walker keeps one Frame per currently-open tree level. advance_to_next
// _instance moves the top frame's pre-order cursor forward until it lands on
// a Tree_pos whose reverse-lookup hit is still a live subnode (stale tombstone
// tree positions left behind by delete_node are silently skipped). When a
// frame's iterator reaches end, the frame pops and its Gid is released from
// active_graphs_. operator++ is responsible for pushing the target-graph
// frame when the yielded instance expands into an as-yet-unvisited subgraph.

HierIterator::HierIterator(Graph* root_graph) {
  if (root_graph == nullptr || root_graph->tree_ == nullptr) {
    return;
  }
  root_gid_  = root_graph->self_gid_;
  // Seed the top frame with pre_order over the root graph's tree, plus a
  // hier_pos of ROOT so the yielded instances match the top-level naming that
  // fast_hier and forward_hier produce (their root frame also uses ROOT).
  auto* tree = root_graph->tree_.get();
  stack_.push_back(Frame{root_graph,
                         Tree::pre_order_iterator(static_cast<Tree_pos>(ROOT), tree, false),
                         Tree::pre_order_iterator(INVALID, tree, false),
                         static_cast<Tree_pos>(ROOT)});
  if (root_gid_ != Gid_invalid) {
    active_graphs_.insert(root_gid_);
  }
  advance_to_next_instance();
}

void HierIterator::advance_to_next_instance() {
  while (!stack_.empty()) {
    Frame& frame = stack_.back();
    while (frame.cur != frame.end) {
      const Tree_pos pos = (*frame.cur).get_debug_nid();
      if (pos == static_cast<Tree_pos>(ROOT)) {
        // ROOT is a structural placeholder — it never corresponds to a
        // subnode. Skip it silently.
        ++frame.cur;
        continue;
      }
      auto it = frame.graph->tree_pos_to_nid_.find(pos);
      if (it == frame.graph->tree_pos_to_nid_.end()) {
        // Orphan tree node (shouldn't happen in normal operation — every
        // tree node is inserted by set_subnode, which also updates the map).
        // Skip defensively so a future tree-only API can't hang iteration.
        ++frame.cur;
        continue;
      }
      const Nid owner_nid = it->second;
      if (!frame.graph->is_node_valid(owner_nid)) {
        // Stale entry left after delete_node — the node is tombstoned but
        // the tree position / map entry weren't cleaned up (current
        // delete_node doesn't touch the structure tree). Skip.
        ++frame.cur;
        continue;
      }
      const auto* entry = frame.graph->ref_node(owner_nid);
      if (!entry->has_subnode()) {
        ++frame.cur;
        continue;
      }
      return;
    }
    const Gid popped = frame.graph->self_gid_;
    stack_.pop_back();
    if (popped != Gid_invalid) {
      active_graphs_.erase(popped);
    }
  }
}

Hier_instance HierIterator::operator*() const {
  const Frame&   frame     = stack_.back();
  const Tree_pos pos       = (*frame.cur).get_debug_nid();
  auto           nid_it    = frame.graph->tree_pos_to_nid_.find(pos);
  const Nid      owner_nid = nid_it->second;
  return Hier_instance(frame.graph, root_gid_, frame.hier_pos, pos, owner_nid);
}

HierIterator& HierIterator::operator++() {
  Frame&         frame     = stack_.back();
  const Tree_pos this_pos  = (*frame.cur).get_debug_nid();
  auto           it        = frame.graph->tree_pos_to_nid_.find(this_pos);
  const Nid      owner_nid = (it != frame.graph->tree_pos_to_nid_.end()) ? it->second : static_cast<Nid>(0);
  const auto*    entry     = frame.graph->ref_node(owner_nid);
  const Gid      sub       = entry->get_subnode();
  const auto*    lib       = frame.graph->owner_lib_;
  ++frame.cur;
  if (sub != Gid_invalid && lib != nullptr && lib->has_graph(sub) && active_graphs_.find(sub) == active_graphs_.end()) {
    Graph* child = const_cast<Graph*>(lib->get_graph(sub).get());
    if (child->tree_ != nullptr) {
      auto* child_tree = child->tree_.get();
      active_graphs_.insert(sub);
      // this_pos is the parent subnode's tree_pos within frame.graph — it
      // becomes the hier_pos for every instance yielded from the child
      // frame, matching fast_hier's semantics.
      stack_.push_back(Frame{child,
                             Tree::pre_order_iterator(static_cast<Tree_pos>(ROOT), child_tree, false),
                             Tree::pre_order_iterator(INVALID, child_tree, false),
                             this_pos});
    }
  }
  advance_to_next_instance();
  return *this;
}

HierIterator HierRange::begin() const { return HierIterator(graph_); }

HierRange Graph::hier_range() const noexcept {
  assert_accessible();
  return HierRange(const_cast<Graph*>(this));
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
  // In a HIER traversal, the reported sinks must cross module boundaries
  // (see inp_edges/out_edges resolution). Class/Flat keep the local view.
  if (node.is_hier() && owner_lib_ != nullptr) {
    return out_edges_hier(node);
  }
  return out_edges_local(node);
}

auto Graph::out_edges_local(Node_class node) -> std::vector<Edge_class> {
  std::vector<Edge_class> out;
  const Nid               self_nid = node.get_debug_nid() & ~static_cast<Nid>(2);
  auto*                   self     = ref_node(self_nid);

  // Pre-build a "context template" Pin_class — every emitted pin inherits the
  // node's traversal context, so we set it once and copy.
  Pin_class self_driver(this, self_nid | static_cast<Pid>(2));
  self_driver.context_  = node.context_;
  self_driver.root_gid_ = node.root_gid_;
  self_driver.hier_pos_ = node.hier_pos_;

  // 1) NodeEntry-level out edges (driver pin == node-as-pin(0))
  for (auto vid : self->get_edges(self_nid, overflow_sets_)) {
    if (vid & static_cast<Vid>(2)) {
      continue;
    }
    Edge_class e{};
    e.driver = self_driver;
    if (vid & static_cast<Vid>(1)) {
      e.sink           = Pin_class(this, static_cast<Pid>(vid));
      e.sink.context_  = node.context_;
      e.sink.root_gid_ = node.root_gid_;
      e.sink.hier_pos_ = node.hier_pos_;
    } else {
      // node-as-pin sink: original code did not inherit context here
      e.sink = Pin_class(this, static_cast<Nid>(vid) & ~static_cast<Nid>(2));
    }
    out.push_back(std::move(e));
  }

  // 2) Walk pin linked list inline (avoids std::vector<Pin_class> alloc and
  //    per-pin std::vector<Edge_class> alloc that used to happen via the
  //    recursive out_edges(Pin_class) path).
  for (Pid cur_pin = self->get_next_pin_id(); cur_pin != 0;) {
    const Pid canonical_pin = (cur_pin & ~static_cast<Pid>(2)) | static_cast<Pid>(1);
    auto*     pin_entry     = ref_pin(canonical_pin);

    Pin_class pin_driver(this, canonical_pin | static_cast<Pid>(2));
    pin_driver.context_  = node.context_;
    pin_driver.root_gid_ = node.root_gid_;
    pin_driver.hier_pos_ = node.hier_pos_;

    for (auto vid : pin_entry->get_edges(canonical_pin, overflow_sets_)) {
      if (vid & static_cast<Vid>(2)) {
        continue;  // back edge (inp_edge)
      }
      Edge_class e{};
      e.driver = pin_driver;
      if (vid & static_cast<Vid>(1)) {
        e.sink           = Pin_class(this, static_cast<Pid>(vid));
        e.sink.context_  = node.context_;
        e.sink.root_gid_ = node.root_gid_;
        e.sink.hier_pos_ = node.hier_pos_;
      } else {
        e.sink = Pin_class(this, static_cast<Nid>(vid) & ~static_cast<Nid>(2));
      }
      out.push_back(std::move(e));
    }

    cur_pin = pin_entry->get_next_pin_id();
  }
  return out;
}

auto Graph::inp_edges(Node_class node) -> std::vector<Edge_class> {
  assert_accessible();
  assert_node_exists(node);
  if (node.is_hier() && owner_lib_ != nullptr) {
    return inp_edges_hier(node);
  }
  return inp_edges_local(node);
}

auto Graph::inp_edges_local(Node_class node) -> std::vector<Edge_class> {
  std::vector<Edge_class> out;
  const Nid               self_nid = node.get_debug_nid() & ~static_cast<Nid>(2);
  auto*                   self     = ref_node(self_nid);

  Pin_class self_sink(this, self_nid & ~static_cast<Pid>(2));
  self_sink.context_  = node.context_;
  self_sink.root_gid_ = node.root_gid_;
  self_sink.hier_pos_ = node.hier_pos_;

  // 1) NodeEntry-level inp edges (sink pin == node-as-pin(0))
  for (auto vid : self->get_edges(self_nid, overflow_sets_)) {
    if (!(vid & static_cast<Vid>(2))) {
      continue;
    }
    Edge_class e{};
    e.sink = self_sink;
    if (vid & static_cast<Vid>(1)) {
      e.driver           = Pin_class(this, static_cast<Pid>(vid));
      e.driver.context_  = node.context_;
      e.driver.root_gid_ = node.root_gid_;
      e.driver.hier_pos_ = node.hier_pos_;
    } else {
      // node-as-pin driver: original code did not inherit context here
      e.driver = Pin_class(this, static_cast<Nid>(vid) | static_cast<Nid>(2));
    }
    out.push_back(std::move(e));
  }

  // 2) Walk pin linked list inline.
  for (Pid cur_pin = self->get_next_pin_id(); cur_pin != 0;) {
    const Pid canonical_pin = (cur_pin & ~static_cast<Pid>(2)) | static_cast<Pid>(1);
    auto*     pin_entry     = ref_pin(canonical_pin);

    Pin_class pin_sink(this, canonical_pin);
    pin_sink.context_  = node.context_;
    pin_sink.root_gid_ = node.root_gid_;
    pin_sink.hier_pos_ = node.hier_pos_;

    for (auto vid : pin_entry->get_edges(canonical_pin, overflow_sets_)) {
      if (!(vid & static_cast<Vid>(2))) {
        continue;  // forward edge (out_edge)
      }
      Edge_class e{};
      e.sink = pin_sink;
      if (vid & static_cast<Vid>(1)) {
        e.driver           = Pin_class(this, static_cast<Pid>(vid));
        e.driver.context_  = node.context_;
        e.driver.root_gid_ = node.root_gid_;
        e.driver.hier_pos_ = node.hier_pos_;
      } else {
        e.driver = Pin_class(this, static_cast<Nid>(vid) | static_cast<Nid>(2));
      }
      out.push_back(std::move(e));
    }

    cur_pin = pin_entry->get_next_pin_id();
  }
  return out;
}

// --- Cross-boundary (hierarchical) edge resolution -------------------------

Pid Graph::find_pin_or_zero(Nid nid, Port_id port_id, bool driver) const {
  const Nid base = nid & ~static_cast<Nid>(3);
  if (port_id == 0) {
    return driver ? (base | static_cast<Pid>(2)) : base;  // node-as-pin
  }
  const auto* self = ref_node(base);
  for (Pid cur = self->get_next_pin_id(); cur != 0;) {
    const Pid   canonical = (cur & ~static_cast<Pid>(2)) | static_cast<Pid>(1);  // real-pin sink form
    const auto* pin       = ref_pin(canonical);
    const auto  port      = pin->get_port_id();
    if (port == port_id) {
      return driver ? (canonical | static_cast<Pid>(2)) : canonical;
    }
    if (port > port_id) {
      break;  // pin list is sorted by ascending port_id
    }
    cur = pin->get_next_pin_id();
  }
  return 0;
}

Nid Graph::master_nid_of_pid(Pid pid) const {
  if (pid & static_cast<Pid>(1)) {  // real pin
    const Pid canonical = (pid & ~static_cast<Pid>(2)) | static_cast<Pid>(1);
    return ref_pin(canonical)->get_master_nid() & ~static_cast<Nid>(3);
  }
  return pid & ~static_cast<Nid>(3);  // node-as-pin
}

Port_id Graph::port_of_pid(Pid pid) const {
  if (pid & static_cast<Pid>(1)) {
    const Pid canonical = (pid & ~static_cast<Pid>(2)) | static_cast<Pid>(1);
    return ref_pin(canonical)->get_port_id();
  }
  return 0;  // node-as-pin == port 0
}

Graph* Graph::hier_path_to_insts(Graph* root, const std::vector<Nid>& chain, std::vector<HierInst>& path) {
  Graph* g = root;
  for (const Nid raw : chain) {
    if (g == nullptr || g->owner_lib_ == nullptr) {
      return nullptr;
    }
    const Nid base = raw & ~static_cast<Nid>(3);
    if (!g->is_node_valid(base)) {
      return nullptr;
    }
    const auto*    entry = g->ref_node(base);
    const auto     it    = g->subnode_tree_pos_.find(base);
    const Tree_pos tp    = (it != g->subnode_tree_pos_.end()) ? it->second : static_cast<Tree_pos>(ROOT);
    if (!entry->has_subnode()) {
      return nullptr;
    }
    const Gid child_gid = entry->get_subnode();
    if (!g->owner_lib_->has_graph(child_gid)) {
      return nullptr;
    }
    path.push_back(HierInst{g, base, tp});
    g = const_cast<Graph*>(g->owner_lib_->get_graph(child_gid).get());
  }
  return g;  // body graph at the chain end
}

bool Graph::reconstruct_hier_path(Graph* root, Gid body_gid, Tree_pos body_hier_pos, std::vector<HierInst>& path) {
  if (root == nullptr || root->owner_lib_ == nullptr) {
    return false;
  }
  // Structural cycles are only asserted-against in debug set_subnode and are not
  // re-checked on load; bound the descent so a cyclic library cannot overflow
  // the stack here (the streaming resolvers use kHierResolveMaxDepth likewise).
  if (path.size() >= static_cast<size_t>(kHierResolveMaxDepth)) {
    return false;
  }
  const auto* lib = root->owner_lib_;
  for (const auto& [nid, tree_pos] : root->subnode_tree_pos_) {
    const auto* entry = root->ref_node(nid);
    if (!entry->is_alive() || !entry->has_subnode()) {
      continue;
    }
    const Gid child_gid = entry->get_subnode();
    path.push_back(HierInst{root, nid, tree_pos});
    if (child_gid == body_gid && tree_pos == body_hier_pos) {
      return true;  // path ends at the instance wrapping the target body
    }
    if (lib->has_graph(child_gid)) {
      Graph* child = const_cast<Graph*>(lib->get_graph(child_gid).get());
      if (reconstruct_hier_path(child, body_gid, body_hier_pos, path)) {
        return true;
      }
    }
    path.pop_back();
  }
  return false;
}

void Graph::resolve_hier_driver(Graph* g, std::vector<HierInst> path, Pid driver_pid, std::vector<HierLeaf>& out, int depth) {
  if (depth > kHierResolveMaxDepth) {
    return;
  }
  // The leaf's own instance chain == the inst_nids currently on `path`.
  const auto make_chain = [&path]() {
    auto c = std::make_shared<std::vector<Nid>>();
    c->reserve(path.size());
    for (const auto& h : path) {
      c->push_back(h.inst_nid);
    }
    return c;
  };
  const Nid master = g->master_nid_of_pid(driver_pid);

  if (master == INPUT_NODE) {
    // Driver pin on this body's INPUT_NODE == a module input port.
    if (path.empty()) {
      out.push_back(HierLeaf{g, driver_pid, static_cast<Tree_pos>(ROOT), make_chain()});  // root primary input: visible
      return;
    }
    const HierInst up = path.back();
    path.pop_back();
    const Port_id port      = g->port_of_pid(driver_pid);
    const Pid     inst_sink = up.parent->find_pin_or_zero(up.inst_nid, port, /*driver=*/false);
    if (inst_sink == 0) {
      return;  // module input unconnected one level up
    }
    for (const auto& e : up.parent->inp_edges(Pin_class(up.parent, inst_sink))) {
      resolve_hier_driver(up.parent, path, e.driver.get_debug_pid(), out, depth + 1);
    }
    return;
  }

  if (master != OUTPUT_NODE && master != CONST_NODE) {
    const auto* entry = g->ref_node(master);
    if (entry->has_subnode() && g->owner_lib_ != nullptr) {
      // Driver pin on a sub-instance == the instance's output port. Cross down
      // into the body's OUTPUT_NODE sink for the same port.
      const Gid   child_gid = entry->get_subnode();
      const auto* lib       = g->owner_lib_;
      if (lib->has_graph(child_gid)) {
        Graph*         child          = const_cast<Graph*>(lib->get_graph(child_gid).get());
        const Port_id  port           = g->port_of_pid(driver_pid);
        const auto     it             = g->subnode_tree_pos_.find(master);
        const Tree_pos tp             = (it != g->subnode_tree_pos_.end()) ? it->second : static_cast<Tree_pos>(ROOT);
        const Pid      child_out_sink = child->find_pin_or_zero(OUTPUT_NODE, port, /*driver=*/false);
        if (child_out_sink != 0) {
          path.push_back(HierInst{g, master, tp});
          for (const auto& e : child->inp_edges(Pin_class(child, child_out_sink))) {
            resolve_hier_driver(child, path, e.driver.get_debug_pid(), out, depth + 1);
          }
        }
        return;
      }
    }
  }

  // Real leaf driver (ordinary node, CONST, or unresolvable boundary).
  out.push_back(HierLeaf{g, driver_pid, path.empty() ? static_cast<Tree_pos>(ROOT) : path.back().inst_tree_pos, make_chain()});
}

void Graph::resolve_hier_sink(Graph* g, std::vector<HierInst> path, Pid sink_pid, std::vector<HierLeaf>& out, int depth) {
  if (depth > kHierResolveMaxDepth) {
    return;
  }
  const auto make_chain = [&path]() {
    auto c = std::make_shared<std::vector<Nid>>();
    c->reserve(path.size());
    for (const auto& h : path) {
      c->push_back(h.inst_nid);
    }
    return c;
  };
  const Nid master = g->master_nid_of_pid(sink_pid);

  if (master == OUTPUT_NODE) {
    // Sink pin on this body's OUTPUT_NODE == a module output port.
    if (path.empty()) {
      out.push_back(HierLeaf{g, sink_pid, static_cast<Tree_pos>(ROOT), make_chain()});  // root primary output: visible
      return;
    }
    const HierInst up = path.back();
    path.pop_back();
    const Port_id port        = g->port_of_pid(sink_pid);
    const Pid     inst_driver = up.parent->find_pin_or_zero(up.inst_nid, port, /*driver=*/true);
    if (inst_driver == 0) {
      return;  // module output unconnected one level up
    }
    for (const auto& e : up.parent->out_edges(Pin_class(up.parent, inst_driver))) {
      resolve_hier_sink(up.parent, path, e.sink.get_debug_pid(), out, depth + 1);
    }
    return;
  }

  if (master != INPUT_NODE && master != CONST_NODE) {
    const auto* entry = g->ref_node(master);
    if (entry->has_subnode() && g->owner_lib_ != nullptr) {
      // Sink pin on a sub-instance == the instance's input port. Cross down
      // into the body's INPUT_NODE driver for the same port.
      const Gid   child_gid = entry->get_subnode();
      const auto* lib       = g->owner_lib_;
      if (lib->has_graph(child_gid)) {
        Graph*         child           = const_cast<Graph*>(lib->get_graph(child_gid).get());
        const Port_id  port            = g->port_of_pid(sink_pid);
        const auto     it              = g->subnode_tree_pos_.find(master);
        const Tree_pos tp              = (it != g->subnode_tree_pos_.end()) ? it->second : static_cast<Tree_pos>(ROOT);
        const Pid      child_in_driver = child->find_pin_or_zero(INPUT_NODE, port, /*driver=*/true);
        if (child_in_driver != 0) {
          path.push_back(HierInst{g, master, tp});
          for (const auto& e : child->out_edges(Pin_class(child, child_in_driver))) {
            resolve_hier_sink(child, path, e.sink.get_debug_pid(), out, depth + 1);
          }
        }
        return;
      }
    }
  }

  out.push_back(HierLeaf{g, sink_pid, path.empty() ? static_cast<Tree_pos>(ROOT) : path.back().inst_tree_pos, make_chain()});
}

// Build the root-to-body instance chain for `node` (the resolver's starting
// path). Prefers the handle's stored full chain (unambiguous even for reused
// sub-graphs); falls back to a DFS reconstruction for chain-less handles.
// Returns false when the node cannot be located (caller degrades to local).
bool Graph::hier_base_path(Node_class node, std::vector<HierInst>& base_path) {
  const auto* lib = owner_lib_;
  if (lib == nullptr || node.root_gid_ == Gid_invalid || !lib->has_graph(node.root_gid_)) {
    return true;  // no hierarchy context: empty path, `this` acts as root
  }
  Graph* root = const_cast<Graph*>(lib->get_graph(node.root_gid_).get());
  if (root == this) {
    return true;  // node lives in the root graph: empty path
  }
  const auto& chain = node.hier_path_;
  if (chain) {
    return hier_path_to_insts(root, *chain, base_path) == this;
  }
  return reconstruct_hier_path(root, get_gid(), node.get_hier_pos(), base_path);
}

auto Graph::inp_edges_hier(Node_class node) -> std::vector<Edge_class> {
  std::vector<HierInst> base_path;
  if (!hier_base_path(node, base_path)) {
    return inp_edges_local(node);  // not locatable in the hierarchy: degrade to local
  }

  std::vector<Edge_class> result;
  std::vector<HierLeaf>   leaves;
  for (const auto& local : inp_edges_local(node)) {
    leaves.clear();
    resolve_hier_driver(this, base_path, local.driver.get_debug_pid(), leaves, 0);
    for (const auto& leaf : leaves) {
      Edge_class e{};
      e.sink              = local.sink;  // near side: node's pin, already hier context
      e.sink.hier_path_   = node.hier_path_;
      e.driver            = Pin_class(leaf.graph, leaf.pid);
      e.driver.context_   = node.context_;
      e.driver.root_gid_  = node.root_gid_;
      e.driver.hier_pos_  = leaf.hier_pos;
      e.driver.hier_path_ = leaf.path;
      result.push_back(std::move(e));
    }
  }
  return result;
}

auto Graph::out_edges_hier(Node_class node) -> std::vector<Edge_class> {
  std::vector<HierInst> base_path;
  if (!hier_base_path(node, base_path)) {
    return out_edges_local(node);  // not locatable in the hierarchy: degrade to local
  }

  std::vector<Edge_class> result;
  std::vector<HierLeaf>   leaves;
  for (const auto& local : out_edges_local(node)) {
    leaves.clear();
    resolve_hier_sink(this, base_path, local.sink.get_debug_pid(), leaves, 0);
    for (const auto& leaf : leaves) {
      Edge_class e{};
      e.driver            = local.driver;  // near side: node's pin, already hier context
      e.driver.hier_path_ = node.hier_path_;
      e.sink              = Pin_class(leaf.graph, leaf.pid);
      e.sink.context_     = node.context_;
      e.sink.root_gid_    = node.root_gid_;
      e.sink.hier_pos_    = leaf.hier_pos;
      e.sink.hier_path_   = leaf.path;
      result.push_back(std::move(e));
    }
  }
  return result;
}

std::string Graph::hier_local_name(Graph* g, Nid nid) {
  const Nid base = nid & ~static_cast<Nid>(3);
  // Built-in singletons have no instance name. INPUT/OUTPUT contribute no node
  // component (the pin's port name stands alone); CONST gets a stable label.
  if (base == INPUT_NODE || base == OUTPUT_NODE) {
    return {};
  }
  if (base == CONST_NODE) {
    return "const";
  }
  if (g == nullptr || !g->is_node_valid(base)) {
    return "n" + std::to_string(static_cast<uint64_t>(base) >> 2);
  }
  const Node_class n(g, base);  // class context: `name` is per-node (flat storage)
  if (n.attr(attrs::name).has()) {
    return std::string(n.attr(attrs::name).get());
  }
  if (const auto gio = n.get_subnode_io()) {
    return std::string(gio->get_name());  // instantiated module (type) name
  }
  return "n" + std::to_string(static_cast<uint64_t>(base) >> 2);
}

std::string Graph::build_hier_name(Graph* graph, Gid root_gid, const std::shared_ptr<const std::vector<Nid>>& path, Nid raw_nid) {
  std::string out;
  if (graph == nullptr) {
    return out;
  }
  if (path && !path->empty() && root_gid != Gid_invalid && graph->owner_lib_ != nullptr && graph->owner_lib_->has_graph(root_gid)) {
    Graph*                root = const_cast<Graph*>(graph->owner_lib_->get_graph(root_gid).get());
    std::vector<HierInst> insts;
    if (hier_path_to_insts(root, *path, insts) != nullptr) {
      for (const auto& h : insts) {
        out += hier_local_name(h.parent, h.inst_nid);
        out += '.';
      }
    }
  }
  out += hier_local_name(graph, raw_nid);
  return out;
}

std::string Node_class::get_hier_name() const {
  if (graph_ == nullptr) {
    return {};
  }
  return Graph::build_hier_name(graph_, root_gid_, hier_path_, raw_nid);
}

void Node_class::set_name(std::string_view name) const {
  assert(graph_ != nullptr && "set_name: node is not attached to a graph");
  attr(attrs::name).set(std::string(name));
}

std::string_view Node_class::get_name() const {
  if (graph_ == nullptr) {
    return {};
  }
  const auto a = attr(attrs::name);
  return a.has() ? std::string_view{a.get()} : std::string_view{};
}

std::string Pin_class::get_hier_name() const {
  if (graph_ == nullptr) {
    return {};
  }
  std::string out = Graph::build_hier_name(graph_, root_gid_, hier_path_, graph_->master_nid_of_pid(pin_pid));
  if (get_port_id() != 0) {  // a real pin: append its port name; node-as-pin == the node itself
    const auto pname = get_pin_name();
    if (!pname.empty()) {
      if (!out.empty()) {  // primary-IO leaf has no node component — port name stands alone
        out += '.';
      }
      out += pname;
    }
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
    self_driver_pin.context_   = pin.context_;
    self_driver_pin.root_gid_  = pin.root_gid_;
    self_driver_pin.hier_pos_  = pin.hier_pos_;
    self_driver_pin.hier_path_ = pin.hier_path_;

    std::vector<Edge_class> out;
    for (auto vid : edges) {
      if (vid & 2) {
        continue;  // skip back edges
      }
      if (vid & 1) {
        Edge_class e{};
        e.driver          = self_driver_pin;
        e.sink            = make_pin_class(static_cast<Pid>(vid));
        e.sink.context_   = pin.context_;
        e.sink.root_gid_  = pin.root_gid_;
        e.sink.hier_pos_  = pin.hier_pos_;
        e.sink.hier_path_ = pin.hier_path_;
        out.push_back(std::move(e));
      } else {
        const Nid  sink_nid = static_cast<Nid>(vid);
        Edge_class e{};
        e.driver          = self_driver_pin;
        e.sink            = Pin_class(this, sink_nid & ~static_cast<Nid>(2));
        e.sink.context_   = pin.context_;
        e.sink.root_gid_  = pin.root_gid_;
        e.sink.hier_pos_  = pin.hier_pos_;
        e.sink.hier_path_ = pin.hier_path_;
        out.push_back(std::move(e));
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
      e.driver          = context_driver_pin;
      e.sink            = make_pin_class(sink_pid);
      e.sink.context_   = pin.context_;
      e.sink.root_gid_  = pin.root_gid_;
      e.sink.hier_pos_  = pin.hier_pos_;
      e.sink.hier_path_ = pin.hier_path_;
      out.push_back(std::move(e));
      continue;
    }

    const Nid sink_nid = static_cast<Nid>(vid);

    Edge_class e{};
    e.driver = context_driver_pin;
    e.sink   = Pin_class(this, sink_nid & ~static_cast<Nid>(2));
    out.push_back(std::move(e));
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
    self_sink_pin.context_   = pin.context_;
    self_sink_pin.root_gid_  = pin.root_gid_;
    self_sink_pin.hier_pos_  = pin.hier_pos_;
    self_sink_pin.hier_path_ = pin.hier_path_;

    std::vector<Edge_class> out;
    for (auto vid : edges) {
      if (!(vid & 2)) {
        continue;  // skip local/forward edges
      }
      if (vid & 1) {
        Edge_class e{};
        e.driver            = make_pin_class(static_cast<Pid>(vid));
        e.driver.context_   = pin.context_;
        e.driver.root_gid_  = pin.root_gid_;
        e.driver.hier_pos_  = pin.hier_pos_;
        e.driver.hier_path_ = pin.hier_path_;
        e.sink              = self_sink_pin;
        out.push_back(std::move(e));
      } else {
        const Nid  driver_nid = static_cast<Nid>(vid);
        Edge_class e{};
        e.driver            = Pin_class(this, driver_nid | static_cast<Nid>(2));
        e.driver.context_   = pin.context_;
        e.driver.root_gid_  = pin.root_gid_;
        e.driver.hier_pos_  = pin.hier_pos_;
        e.driver.hier_path_ = pin.hier_path_;
        e.sink              = self_sink_pin;
        out.push_back(std::move(e));
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
      e.driver            = make_pin_class(driver_pid);
      e.driver.context_   = pin.context_;
      e.driver.root_gid_  = pin.root_gid_;
      e.driver.hier_pos_  = pin.hier_pos_;
      e.driver.hier_path_ = pin.hier_path_;
      e.sink              = context_sink_pin;
      out.push_back(std::move(e));
      continue;
    }

    const Nid driver_nid = static_cast<Nid>(vid);

    Edge_class e{};
    e.driver = Pin_class(this, driver_nid | static_cast<Nid>(2));
    e.sink   = context_sink_pin;
    out.push_back(std::move(e));
  }

  return out;
}

auto Graph::get_pins(Node_class node) -> absl::InlinedVector<Pin_class, 4> {
  assert_accessible();
  assert_node_exists(node);
  absl::InlinedVector<Pin_class, 4> out;
  const Nid                         self_nid = node.get_debug_nid() & ~static_cast<Nid>(2);
  auto*                             self     = ref_node(self_nid);

  Pid cur_pin = self->get_next_pin_id();
  while (cur_pin != 0) {
    const Pid canonical_pin = (cur_pin & ~static_cast<Pid>(2)) | static_cast<Pid>(1);
    out.push_back(make_pin_class(canonical_pin));
    cur_pin = ref_pin(canonical_pin)->get_next_pin_id();
  }

  return out;
}

auto Graph::get_driver_pins(Node_class node) -> absl::InlinedVector<Pin_class, 4> {
  assert_accessible();
  assert_node_exists(node);
  absl::InlinedVector<Pin_class, 4> out;
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

auto Graph::get_sink_pins(Node_class node) -> absl::InlinedVector<Pin_class, 4> {
  assert_accessible();
  assert_node_exists(node);
  absl::InlinedVector<Pin_class, 4> out;
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
  nid                 &= ~static_cast<Nid>(3);
  const Nid actual_id  = nid >> 2;
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

void Graph::add_edge_int(Vid self_id, Vid other_id) {
  auto pool      = get_overflow_pool();
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
  // here next_pin is the raw pin_table index (not << 2), nid is << 2.
  // The pin linked list is kept sorted by ascending port_id, so we insert at the right slot.
  Nid        actual_nid        = nid >> 2;
  auto       node              = &node_table[actual_nid];
  const Pid  new_pid_canonical = (next_pin << 2) | 1;
  const auto new_port          = pin_table[next_pin].get_port_id();

  Pid head = node->get_next_pin_id();
  if (head == 0 || pin_table[head >> 2].get_port_id() > new_port) {
    pin_table[next_pin].set_next_pin_id(head);
    node->set_next_pin_id(new_pid_canonical);
    return;
  }
  Pid cur = head >> 2;
  while (true) {
    Pid nxt = pin_table[cur].get_next_pin_id();
    if (nxt == 0 || pin_table[nxt >> 2].get_port_id() > new_port) {
      pin_table[next_pin].set_next_pin_id(nxt);
      pin_table[cur].set_next_pin_id(new_pid_canonical);
      return;
    }
    cur = nxt >> 2;
  }
}

void Graph::display_graph() const {
  assert_accessible();
  for (Pid pid = 1; pid < pin_table.size(); ++pid) {
    // ref_pin/get_edges expect a canonical Pid ((index << 2) | 1), not a raw
    // table index — otherwise every entry decodes against the wrong self index.
    const Pid cpid = (pid << 2) | static_cast<Pid>(1);
    auto      p    = ref_pin(cpid);
    std::cout << "PinEntry " << pid << "  node=" << p->get_master_nid() << " port=" << p->get_port_id() << "\n";
    if (p->has_edges()) {
      auto sed = p->get_edges(cpid, overflow_sets_);
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
      const auto gio = owner_lib_->io_at_unlocked(entry->get_subnode());
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
  tree_pos_to_nid_.clear();
  for (size_t i = 1; i < node_table.size(); ++i) {
    if (!node_table[i].is_alive() || !node_table[i].has_subnode()) {
      continue;
    }
    const Nid      subnode_nid = static_cast<Nid>(i) << 2;
    const Tree_pos child_pos   = tree_->add_child(static_cast<Tree_pos>(ROOT));
    subnode_tree_pos_.emplace(subnode_nid, child_pos);
    tree_pos_to_nid_.emplace(child_pos, subnode_nid);
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

  // Exclusive: the source-map fold below mutates *srcmap_sp_ and the per-graph
  // locators/attr stores (load/load_merge already take the unique lock).
  std::unique_lock lock(registry_mu_);

  // Deterministic gid order (the map iterates in arbitrary order).
  std::vector<Gid> io_gids;
  io_gids.reserve(graph_ios_.size());
  for (const auto& [gid, gio] : graph_ios_) {
    if (gio) {
      io_gids.push_back(gid);
    }
  }
  std::sort(io_gids.begin(), io_gids.end());

  // --- source-map union (library base ∪ per-graph deltas) ---
  // Folded in before the body pass so a collision remap can rewrite a graph's
  // srcid attribute values (and re-dirty it) ahead of save_body. Only fresh
  // delta entries can remap; ids already in the base (= previously saved, the
  // ones clean bodies reference) never move.
  for (const Gid gid : io_gids) {
    const auto git = graphs_.find(gid);
    if (git == graphs_.end() || !git->second || git->second->deleted_) {
      continue;
    }
    Graph& g = *git->second;
    if (g.srcloc_.empty()) {
      continue;
    }
    const auto remap = srcmap_sp_->merge(g.srcloc_);
    if (!remap.empty() && g.has_attr(attrs::srcid)) {
      auto& ids     = g.attr_store(attrs::srcid);
      bool  changed = false;
      for (auto& [key, value] : ids) {
        if (const auto rit = remap.find(value); rit != remap.end()) {
          value   = rit->second;
          changed = true;
        }
      }
      if (changed) {
        g.dirty_ = true;  // body must re-save with the rewritten ids
      }
    }
    g.srcloc_.clear();  // entries now live in the base; resolution chains to it
  }
  // The fold above always runs (graph deltas must land in the shared base);
  // only the write is deferred when a co-sharer owns srcmap.txt persistence.
  if (persist_srcmap_) {
    srcmap_sp_->save(db_path);
  }

  // --- library.txt (declarations, text format) ---
  {
    std::ofstream ofs(fs::path(db_path) / "library.txt");
    assert(ofs.good() && "GraphLibrary::save: cannot open library.txt");
    ofs << "hhds_graphlib 1\n";
    for (const Gid gid : io_gids) {
      const auto& gio = graph_ios_.at(gid);
      ofs << "graph_io " << gid << " " << gio->get_name() << "\n";
      auto emit_pin = [&ofs](const char* direction, const GraphIO::DeclaredIoPin& pin) {
        ofs << "  " << direction << " " << pin.port_id << " " << pin.name;
        if (pin.loop_break) {
          ofs << " loop_break";
        }
        if (pin.bits) {
          ofs << " bits=" << pin.bits;
        }
        if (pin.unsign) {
          ofs << " unsigned";
        }
        ofs << "\n";
      };
      for (const auto& pin : gio->input_pin_decls_) {
        emit_pin("input", pin);
      }
      for (const auto& pin : gio->output_pin_decls_) {
        emit_pin("output", pin);
      }
    }
    // Preserve (name, gid) pairs for deleted graphs so that recreating by name
    // reuses the original gid. Parent graphs store subnode gids in their
    // binary bodies; this lets those references survive delete + recreate
    // across save/load.
    std::vector<std::pair<Gid, std::string>> deleted(deleted_name_to_id_.size());
    {
      size_t k = 0;
      for (const auto& [name, gid] : deleted_name_to_id_) {
        deleted[k++] = {gid, name};
      }
    }
    std::sort(deleted.begin(), deleted.end());
    for (const auto& [gid, name] : deleted) {
      ofs << "graph_io_deleted " << gid << " " << name << "\n";
    }
  }

  // --- graph body directories ---
  for (const Gid gid : io_gids) {
    const auto it = graphs_.find(gid);
    if (it == graphs_.end() || !it->second || it->second->deleted_ || !it->second->dirty_) {
      continue;
    }
    const auto dir = fs::path(db_path) / ("graph_" + std::to_string(gid));
    it->second->save_body(dir.string());
  }
}

void GraphLibrary::load(const std::string& db_path) {
  namespace fs = std::filesystem;

  std::unique_lock lock(registry_mu_);

  // Clear current state.
  for (auto& [gid, g] : graphs_) {
    if (g) {
      g->invalidate_from_library();
    }
  }
  graph_ios_.clear();
  graphs_.clear();
  graph_slot_states_.clear();
  graph_slot_abort_pending_.clear();
  graph_name_to_id_.clear();
  deleted_name_to_id_.clear();
  live_count_ = 0;
  mutation_epoch_.store(1, std::memory_order_release);
  // (gid-keyed maps need no slot-0 reservation)

  // Source-provenance base: state = what's on disk (missing file -> empty).
  // A borrower of a shared map defers loading to the owning sharer.
  if (persist_srcmap_) {
    (void)srcmap_sp_->load(db_path);
  }

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
        // Record so recreating by this name reuses the original gid (gid-keyed
        // map: no slot to reserve).
        deleted_name_to_id_[name] = gid;
        current_gio.reset();
      } else if (line.substr(0, 9) == "graph_io ") {
        // "graph_io <gid> <name>"
        std::istringstream ss(line.substr(9));
        Gid                gid;
        std::string        name;
        ss >> gid >> name;
        current_gio = create_io_impl_unlocked(gid, name);
      } else if (line.size() > 2 && line[0] == ' ' && line[1] == ' ') {
        assert(current_gio && "GraphLibrary::load: pin decl without graph_io");
        std::istringstream ss(line.substr(2));
        std::string        direction;
        Port_id            port_id;
        std::string        name;
        ss >> direction >> port_id >> name;
        bool        loop_break = false;
        uint32_t    bits       = 0;
        bool        unsign     = false;
        std::string token;
        while (ss >> token) {
          if (token == "loop_break" || token == "loop_last") {  // "loop_last": legacy token
            loop_break = true;
          } else if (token == "unsigned") {
            unsign = true;
          } else if (token.rfind("bits=", 0) == 0) {
            bits = static_cast<uint32_t>(std::stoul(token.substr(5)));
          }
        }
        // We hold registry_mu_ exclusively here, so we can't reach into
        // GraphIO::add_input/add_output (those call get_graph() which would
        // reacquire the lock as a reader). The body hasn't been materialized
        // yet, so this only needs to populate the GraphIO declaration vectors.
        GraphIO::DeclaredIoPin decl{name, port_id, loop_break, bits, unsign};
        if (direction == "input") {
          current_gio->input_pin_decls_.push_back(decl);
          current_gio->declared_io_pins_.emplace(
              current_gio->input_pin_decls_.back().name,
              GraphIO::DeclaredIoPinRef{GraphIO::IoDirection::Input, current_gio->input_pin_decls_.size() - 1});
        } else {
          current_gio->output_pin_decls_.push_back(decl);
          current_gio->declared_io_pins_.emplace(
              current_gio->output_pin_decls_.back().name,
              GraphIO::DeclaredIoPinRef{GraphIO::IoDirection::Output, current_gio->output_pin_decls_.size() - 1});
        }
        note_graph_mutation();
      }
    }
  }

  // --- Load graph bodies (deterministic gid order) ---
  std::vector<Gid> io_gids;
  io_gids.reserve(graph_ios_.size());
  for (const auto& [gid, gio] : graph_ios_) {
    if (gio) {
      io_gids.push_back(gid);
    }
  }
  std::sort(io_gids.begin(), io_gids.end());
  for (const Gid gid : io_gids) {
    const auto& gio = graph_ios_.at(gid);
    const auto  dir = fs::path(db_path) / ("graph_" + std::to_string(gid));
    if (fs::exists(dir / "body.bin")) {
      auto graph = create_graph_body_loaded_unlocked(gio);
      graph->load_body(dir.string());
    }
  }
}

void GraphLibrary::load_merge(const std::string& db_path) {
  namespace fs = std::filesystem;

  std::unique_lock lock(registry_mu_);

  // --- Parse the incoming library.txt into entries (no mutation yet) ---
  struct Entry {
    Gid                                 src_gid = Gid_invalid;
    std::string                         name;
    std::vector<GraphIO::DeclaredIoPin> inputs;
    std::vector<GraphIO::DeclaredIoPin> outputs;
  };
  std::vector<Entry> entries;
  {
    std::ifstream ifs(fs::path(db_path) / "library.txt");
    assert(ifs.good() && "GraphLibrary::load_merge: cannot open library.txt");
    std::string line;
    std::getline(ifs, line);  // header: "hhds_graphlib 1"
    size_t cur = static_cast<size_t>(-1);
    while (std::getline(ifs, line)) {
      if (line.empty()) {
        continue;
      }
      if (line.substr(0, 17) == "graph_io_deleted ") {
        cur = static_cast<size_t>(-1);  // deleted graphs are not merged
      } else if (line.substr(0, 9) == "graph_io ") {
        std::istringstream ss(line.substr(9));
        Gid                gid;
        std::string        name;
        ss >> gid >> name;
        entries.push_back(Entry{gid, name, {}, {}});
        cur = entries.size() - 1;
      } else if (line.size() > 2 && line[0] == ' ' && line[1] == ' ' && cur != static_cast<size_t>(-1)) {
        std::istringstream ss(line.substr(2));
        std::string        direction;
        Port_id            port_id;
        std::string        name;
        ss >> direction >> port_id >> name;
        bool        loop_break = false;
        uint32_t    bits       = 0;
        bool        unsign     = false;
        std::string token;
        while (ss >> token) {
          if (token == "loop_break" || token == "loop_last") {  // "loop_last": legacy token
            loop_break = true;
          } else if (token == "unsigned") {
            unsign = true;
          } else if (token.rfind("bits=", 0) == 0) {
            bits = static_cast<uint32_t>(std::stoul(token.substr(5)));
          }
        }
        GraphIO::DeclaredIoPin decl{name, port_id, loop_break, bits, unsign};
        (direction == "input" ? entries[cur].inputs : entries[cur].outputs).push_back(decl);
      }
    }
  }

  // --- Merge the incoming library's source map (payload dedup + id remap) ---
  // Done before the bodies load so the remap can rewrite each absorbed body's
  // srcid attribute values below. Matching spans share ids by construction
  // (hash-minted), so the remap is empty unless a true hash collision was
  // probe-resolved differently by the two libraries.
  Source_locator incoming_srcmap;
  (void)incoming_srcmap.load(db_path);
  const auto src_remap = srcmap_sp_->merge(incoming_srcmap);

  // --- Assign each incoming graph a gid in THIS library + build the remap ---
  absl::flat_hash_map<Gid, Gid>    remap;           // src_gid -> dst_gid in this
  std::vector<std::pair<Gid, Gid>> bodies_to_load;  // (src_gid, dst_gid)
  for (const auto& e : entries) {
    if (auto existing = find_io_unlocked(e.name)) {
      // Dedup by name. Load the incoming body only if ours is an IO-only stub.
      const Gid dst    = existing->get_gid();
      remap[e.src_gid] = dst;
      if (!has_graph_unlocked(dst)) {
        bodies_to_load.emplace_back(e.src_gid, dst);
      }
      continue;
    }
    // New name → its canonical (name-hash) gid, probed on collision. Identical
    // to e.src_gid when the source also used name-hash gids (the common case).
    const Gid dst = pick_gid_for_name_unlocked(e.name);
    auto      gio = create_io_impl_unlocked(dst, e.name);
    for (const auto& d : e.inputs) {
      gio->input_pin_decls_.push_back(d);
      gio->declared_io_pins_.emplace(gio->input_pin_decls_.back().name,
                                     GraphIO::DeclaredIoPinRef{GraphIO::IoDirection::Input, gio->input_pin_decls_.size() - 1});
    }
    for (const auto& d : e.outputs) {
      gio->output_pin_decls_.push_back(d);
      gio->declared_io_pins_.emplace(gio->output_pin_decls_.back().name,
                                     GraphIO::DeclaredIoPinRef{GraphIO::IoDirection::Output, gio->output_pin_decls_.size() - 1});
    }
    remap[e.src_gid] = dst;
    bodies_to_load.emplace_back(e.src_gid, dst);
  }

  // --- Load bodies (from their SOURCE gid dir) + remap Sub references ---
  std::sort(bodies_to_load.begin(), bodies_to_load.end());
  for (const auto& [src_gid, dst_gid] : bodies_to_load) {
    const auto dir = fs::path(db_path) / ("graph_" + std::to_string(src_gid));
    if (!fs::exists(dir / "body.bin")) {
      continue;
    }
    auto gio   = io_at_unlocked(dst_gid);
    auto graph = create_graph_body_loaded_unlocked(gio);
    graph->load_body(dir.string());
    // Mark dirty so a subsequent save() writes this absorbed body into the
    // merged library (loaded bodies are otherwise clean and save() skips them).
    graph->dirty_ = true;
    // Rewrite each Sub's subnode gid through the remap (identity → no-op, which
    // is the all-name-hash case). A subnode gid absent from the remap is an
    // external reference satisfied by another input at the same canonical gid.
    for (auto node : graph->fast_class()) {
      const Gid old = node.get_subnode_gid();
      if (old == Gid_invalid) {
        continue;
      }
      if (const auto it = remap.find(old); it != remap.end() && it->second != old) {
        graph->set_subnode(node, it->second);
      }
    }
    // Rewrite srcid attribute values through the source-map remap (identity →
    // no-op, the common all-hash-agree case). The body is already marked dirty.
    if (!src_remap.empty() && graph->has_attr(attrs::srcid)) {
      auto& ids = graph->attr_store(attrs::srcid);
      for (auto& [key, value] : ids) {
        if (const auto it = src_remap.find(value); it != src_remap.end()) {
          value = it->second;
        }
      }
    }
  }
}

}  // namespace hhds
