// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <vector>

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
//   Definition_index — library-wide key. Two integers: (gid, nid). Two
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

struct Definition_index {
  Gid gid   = Gid_invalid;
  Nid value = 0;

  [[nodiscard]] constexpr bool operator==(const Definition_index& other) const noexcept {
    return gid == other.gid && value == other.value;
  }
  [[nodiscard]] constexpr bool operator!=(const Definition_index& other) const noexcept { return !(*this == other); }

  template <typename H>
  friend H AbslHashValue(H h, const Definition_index& x) {
    return H::combine(std::move(h), x.gid, x.value);
  }
};

// Compatibility spelling for downstream migrations. It identifies shared
// definition storage and is not a flattened physical occurrence.
using Flat_index = Definition_index;

struct Occurrence_step {
  Definition_index        subnode;
  std::optional<uint64_t> ordinal;

  [[nodiscard]] bool operator==(const Occurrence_step&) const noexcept = default;
};

namespace detail {
struct Occurrence_path_storage;
struct Hierarchy_view_state;
}  // namespace detail

// Structural instance identity. Views intern paths in a parent-pointer table;
// the small handle is the fast equality/hash path within one view, while
// cross-view comparisons fall back to the structural steps.
class Occurrence_path {
public:
  Occurrence_path() = default;

  [[nodiscard]] Gid                              root_gid() const noexcept { return root_gid_; }
  [[nodiscard]] std::span<const Occurrence_step> steps() const;
  [[nodiscard]] uint32_t                         interned_handle() const noexcept { return handle_; }
  [[nodiscard]] size_t                           hash() const noexcept;
  [[nodiscard]] bool                             operator==(const Occurrence_path& other) const noexcept;
  [[nodiscard]] bool operator!=(const Occurrence_path& other) const noexcept { return !(*this == other); }

  template <typename H>
  friend H AbslHashValue(H h, const Occurrence_path& x) {
    return H::combine(std::move(h), x.hash());
  }

private:
  Occurrence_path(Gid root_gid, std::shared_ptr<const detail::Occurrence_path_storage> storage, uint32_t handle)
      : root_gid_(root_gid), storage_(std::move(storage)), handle_(handle) {}

  Gid                                                    root_gid_ = Gid_invalid;
  std::shared_ptr<const detail::Occurrence_path_storage> storage_;
  uint32_t                                               handle_ = 0;

  friend class Grouped_hierarchy_view;
  friend class Occurrences_view;
  friend class Subnode_occurrence;
  friend class HierIterator;
  friend class Hier_instance;
  friend struct detail::Hierarchy_view_state;
  friend struct detail::Occurrence_path_storage;
};

struct Occurrence_index {
  Occurrence_path  path;
  Definition_index object;

  [[nodiscard]] bool operator==(const Occurrence_index&) const noexcept = default;

  template <typename H>
  friend H AbslHashValue(H h, const Occurrence_index& x) {
    return H::combine(std::move(h), x.path, x.object.gid, x.object.value);
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
struct hash<hhds::Definition_index> {
  [[nodiscard]] size_t operator()(const hhds::Definition_index& x) const noexcept {
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

template <>
struct hash<hhds::Occurrence_path> {
  [[nodiscard]] size_t operator()(const hhds::Occurrence_path& x) const noexcept { return x.hash(); }
};

template <>
struct hash<hhds::Occurrence_index> {
  [[nodiscard]] size_t operator()(const hhds::Occurrence_index& x) const noexcept {
    const size_t h1 = x.path.hash();
    const size_t h2 = hash<hhds::Definition_index>{}(x.object);
    return h1 ^ (h2 + 0x9e3779b97f4a7c15ULL + (h1 << 6U) + (h1 >> 2U));
  }
};

}  // namespace std
