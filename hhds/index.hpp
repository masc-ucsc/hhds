// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <utility>

#include "graph_sizing.hpp"
#include "tree.hpp"

// Opaque, small, hashable index types for using nodes/pins as keys in user maps
// (std::unordered_map, absl::flat_hash_map, etc.). Prefer these over using
// Node_class / Pin_class directly as map keys — the handle carries traversal
// context (graph pointer, hierarchy state, shared_ptr) that is wasted on every
// probe. The indexes here are plain data and cheap to copy, hash, and compare.
//
// Three flavors:
//   Class_index — per-graph-body key. One integer (the raw nid/pid). Two
//                 different bodies can produce equal Class_index values, so
//                 this is safe only when the containing map scope is limited
//                 to a single graph body.
//   Flat_index  — library-wide key. Two integers: (gid, nid). Two
//                 instantiations of the same body produce the *same*
//                 Flat_index because they share body storage.
//   Hier_index  — per-instance key. Two integers: (hier_pos, nid). Two
//                 instantiations of the same body produce *different*
//                 Hier_index values because their hierarchy position differs.

namespace hhds {

struct Class_index {
  Nid value = 0;

  [[nodiscard]] constexpr bool operator==(const Class_index& other) const noexcept { return value == other.value; }
  [[nodiscard]] constexpr bool operator!=(const Class_index& other) const noexcept { return value != other.value; }

  template <typename H>
  friend H AbslHashValue(H h, const Class_index& x) {
    return H::combine(std::move(h), x.value);
  }
};

struct Flat_index {
  Gid gid   = Gid_invalid;
  Nid value = 0;

  [[nodiscard]] constexpr bool operator==(const Flat_index& other) const noexcept {
    return gid == other.gid && value == other.value;
  }
  [[nodiscard]] constexpr bool operator!=(const Flat_index& other) const noexcept { return !(*this == other); }

  template <typename H>
  friend H AbslHashValue(H h, const Flat_index& x) {
    return H::combine(std::move(h), x.gid, x.value);
  }
};

// Hier_index identifies a node within a hierarchical traversal.
//
// Uniqueness: guaranteed within one level of subnode instantiation. The
// (gid, hier_pos) pair distinguishes instances of the same graph reached
// via different subnode positions. Deep hierarchies with a commonly-
// instantiated grand-subgraph can collide — see graph.hpp notes.
struct Hier_index {
  Gid      gid      = Gid_invalid;  // current graph of the node
  Tree_pos hier_pos = 0;            // per-instance token (position in the parent graph's structure tree)
  Nid      value    = 0;            // raw_nid of the node

  [[nodiscard]] constexpr bool operator==(const Hier_index& other) const noexcept {
    return gid == other.gid && hier_pos == other.hier_pos && value == other.value;
  }
  [[nodiscard]] constexpr bool operator!=(const Hier_index& other) const noexcept { return !(*this == other); }

  template <typename H>
  friend H AbslHashValue(H h, const Hier_index& x) {
    return H::combine(std::move(h), x.gid, x.hier_pos, x.value);
  }
};

}  // namespace hhds

namespace std {

template <>
struct hash<hhds::Class_index> {
  [[nodiscard]] size_t operator()(const hhds::Class_index& x) const noexcept { return std::hash<hhds::Nid>{}(x.value); }
};

template <>
struct hash<hhds::Flat_index> {
  [[nodiscard]] size_t operator()(const hhds::Flat_index& x) const noexcept {
    const size_t h1 = std::hash<hhds::Gid>{}(x.gid);
    const size_t h2 = std::hash<hhds::Nid>{}(x.value);
    return h1 ^ (h2 + 0x9e3779b97f4a7c15ULL + (h1 << 6U) + (h1 >> 2U));
  }
};

template <>
struct hash<hhds::Hier_index> {
  [[nodiscard]] size_t operator()(const hhds::Hier_index& x) const noexcept {
    const size_t h1 = std::hash<hhds::Gid>{}(x.gid);
    const size_t h2 = std::hash<int64_t>{}(x.hier_pos);
    const size_t h3 = std::hash<hhds::Nid>{}(x.value);
    size_t       h  = h1 ^ (h2 + 0x9e3779b97f4a7c15ULL + (h1 << 6U) + (h1 >> 2U));
    return h ^ (h3 + 0x9e3779b97f4a7c15ULL + (h << 6U) + (h >> 2U));
  }
};

}  // namespace std
