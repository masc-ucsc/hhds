// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// tree.hpp
#include <sys/stat.h>
#include <sys/types.h>

#include <algorithm>
#include <array>
#include <bitset>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <iterator>
#include <memory>
#include <optional>
#include <ostream>
#include <queue>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "hhds/attr.hpp"
#include "hhds/attrs/name.hpp"
#include "hhds/graph_sizing.hpp"
#include "hhds/tree_print.hpp"
#include "iassert.hpp"

#ifdef TESTING
#undef I
#define I(cond, msg)                 \
  do {                               \
    if (!(cond))                     \
      throw std::runtime_error(msg); \
  } while (0)
#endif

// Logging control
#define ENABLE_LOGGING          0
#define ENABLE_CREATION_LOGGING 0

// Logging macros
#if ENABLE_CREATION_LOGGING
#define LOG_CREAT_DEBUG(...)                             \
  do {                                                   \
    std::printf("[DEBUG] %s:%d - ", __FILE__, __LINE__); \
    std::printf(__VA_ARGS__);                            \
    std::printf("\n");                                   \
  } while (0)
#else
#define LOG_CREAT_DEBUG(...)
#endif

#if ENABLE_LOGGING
#define LOG_DEBUG(...)                                   \
  do {                                                   \
    std::printf("[DEBUG] %s:%d - ", __FILE__, __LINE__); \
    std::printf(__VA_ARGS__);                            \
    std::printf("\n");                                   \
  } while (0)
#define LOG_INFO(...)                                   \
  do {                                                  \
    std::printf("[INFO] %s:%d - ", __FILE__, __LINE__); \
    std::printf(__VA_ARGS__);                           \
    std::printf("\n");                                  \
  } while (0)
#define LOG_WARN(...)                                   \
  do {                                                  \
    std::printf("[WARN] %s:%d - ", __FILE__, __LINE__); \
    std::printf(__VA_ARGS__);                           \
    std::printf("\n");                                  \
  } while (0)
#define LOG_ERROR(...)                                   \
  do {                                                   \
    std::printf("[ERROR] %s:%d - ", __FILE__, __LINE__); \
    std::printf(__VA_ARGS__);                            \
    std::printf("\n");                                   \
  } while (0)
#else
#define LOG_DEBUG(...)
#define LOG_INFO(...)
#define LOG_WARN(...)
#define LOG_ERROR(...)
#endif

namespace hhds {
/** NOTES for future contributors:
 * Tree_pointers tracks child links directly with 64-bit pointers per slot,
 * eliminating the short-delta indirection entirely. The structure now uses:
 * - 3 x 64-bit Tree_pos fields (parent and sibling links) = 192 bits
 * - 1 x std::array<Tree_pos, 8> for child pointers = 8 * 64 = 512 bits
 * - Bookkeeping fields (num_short_del_occ, is_leaf) = ~24 bits
 * - Total: ~728 bits (~91 bytes) before alignment; the struct remains 64-byte aligned for cache locality.
 *
 * This is larger than the previous 512-bit (64-byte) structure but eliminates
 * expensive bit manipulation operations for much better performance.
 * The structure is aligned to 64 bytes for cache efficiency.
 */

using Tree_pos = int64_t;
using Tid      = Tree_pos;

// Opaque, hashable keys for using Tree::Node_class handles in user-owned
// maps (std::unordered_map, absl::flat_hash_map). Mirrors the graph
// Class_index/Flat_index contract from hhds/index.hpp but in the tree
// address space (Tree_pos values, Tid for the containing tree body).
//
//   Tree_class_index — per-tree-body key. One integer (current_pos).
//                      Safe only when the containing map scope is limited
//                      to a single tree body.
//   Tree_flat_index  — forest-wide key. Two integers: (tid, current_pos).
//
// Tree_hier_index is intentionally absent: Tree::Node_class does not yet
// carry hierarchy context (unlike Graph::Node_class). Add it alongside the
// machinery that tracks hier_pos in the tree handle.
struct Tree_class_index {
  Tree_pos value = 0;

  [[nodiscard]] constexpr bool operator==(const Tree_class_index& other) const noexcept { return value == other.value; }
  [[nodiscard]] constexpr bool operator!=(const Tree_class_index& other) const noexcept { return value != other.value; }

  template <typename H>
  friend H AbslHashValue(H h, const Tree_class_index& x) {
    return H::combine(std::move(h), x.value);
  }
};

struct Tree_flat_index {
  Tid      tid   = 0;
  Tree_pos value = 0;

  [[nodiscard]] constexpr bool operator==(const Tree_flat_index& other) const noexcept {
    return tid == other.tid && value == other.value;
  }
  [[nodiscard]] constexpr bool operator!=(const Tree_flat_index& other) const noexcept { return !(*this == other); }

  template <typename H>
  friend H AbslHashValue(H h, const Tree_flat_index& x) {
    return H::combine(std::move(h), x.tid, x.value);
  }
};

// Hier_index for trees. Like Graph's Hier_index, the hier_pos is a Tree_pos
// into the *expansion tree* (an auxiliary hhds::Tree that records one node
// per hierarchical visit). Two instantiations of the same subtree body get
// different hier_pos values, so Tree_hier_index distinguishes per-instance
// state that Tree_flat_index collapses.
struct Tree_hier_index {
  Tree_pos hier_pos = 0;
  Tree_pos value    = 0;

  [[nodiscard]] constexpr bool operator==(const Tree_hier_index& other) const noexcept {
    return hier_pos == other.hier_pos && value == other.value;
  }
  [[nodiscard]] constexpr bool operator!=(const Tree_hier_index& other) const noexcept { return !(*this == other); }

  template <typename H>
  friend H AbslHashValue(H h, const Tree_hier_index& x) {
    return H::combine(std::move(h), x.hier_pos, x.value);
  }
};

static constexpr int16_t  CHUNK_SHIFT = 3;                 // The number of bits in a chunk offset
static constexpr int16_t  CHUNK_SIZE  = 1 << CHUNK_SHIFT;  // Size of a chunk in bits
static constexpr int16_t  CHUNK_MASK  = CHUNK_SIZE - 1;    // Mask for chunk offset
static constexpr Tree_pos INVALID     = 0;                 // This is invalid for all pointers other than parent
static constexpr Tree_pos ROOT        = 1 << CHUNK_SHIFT;  // ROOT ID

// Now using full 64-bit addressing - much simpler!
static constexpr uint64_t MAX_TREE_SIZE = UINT64_MAX;  // Maximum number of chunks in the tree

class Forest;
class TreeIO;
class TreeCursor;
class ForestCursor;

class __attribute__((aligned(64))) Tree_pointers {  // NOLINT(readability-magic-numbers)
private:
  // Full 64-bit pointers - no more bit packing
  Tree_pos parent;
  Tree_pos next_sibling;
  Tree_pos prev_sibling;

  // Direct child pointers per slot within the chunk
  std::array<Tree_pos, CHUNK_SIZE> first_child_ptrs;
  std::array<Tree_pos, CHUNK_SIZE> last_child_ptrs;
  std::array<Type, CHUNK_SIZE>     types;

  // Track number of occupied entries in the chunk
  uint16_t num_short_del_occ;

  // leaf flag
  bool is_leaf;

public:
  /* DEFAULT CONSTRUCTOR */
  Tree_pointers()
      : parent(INVALID)
      , next_sibling(INVALID)
      , prev_sibling(INVALID)
      , first_child_ptrs{}
      , last_child_ptrs{}
      , types{}
      , num_short_del_occ(0)
      , is_leaf(true) {
    first_child_ptrs.fill(INVALID);
    last_child_ptrs.fill(INVALID);
    types.fill(0);
  }

  /* PARAM CONSTRUCTOR */
  Tree_pointers(Tree_pos p)
      : parent(p)
      , next_sibling(INVALID)
      , prev_sibling(INVALID)
      , first_child_ptrs{}
      , last_child_ptrs{}
      , types{}
      , num_short_del_occ(0)
      , is_leaf(true) {
    first_child_ptrs.fill(INVALID);
    last_child_ptrs.fill(INVALID);
    types.fill(0);
  }

  // Getters
  [[nodiscard]] Tree_pos get_parent() const { return parent; }
  [[nodiscard]] Tree_pos get_next_sibling() const { return next_sibling; }
  [[nodiscard]] Tree_pos get_prev_sibling() const { return prev_sibling; }
  [[nodiscard]] Tree_pos get_first_child_at(int16_t index) const { return first_child_ptrs[static_cast<size_t>(index)]; }
  [[nodiscard]] Tree_pos get_last_child_at(int16_t index) const { return last_child_ptrs[static_cast<size_t>(index)]; }
  [[nodiscard]] Type     get_type_at(int16_t index) const { return types[static_cast<size_t>(index)]; }

  // Getter for num_occ
  [[nodiscard]] uint16_t get_num_short_del_occ() const { return num_short_del_occ; }

  // getter for is_leaf
  [[nodiscard]] bool get_is_leaf() const { return is_leaf; }

  // Operators
  constexpr bool operator==(const Tree_pointers& other) const {
    return parent == other.parent && next_sibling == other.next_sibling && prev_sibling == other.prev_sibling
           && first_child_ptrs == other.first_child_ptrs && last_child_ptrs == other.last_child_ptrs && types == other.types
           && num_short_del_occ == other.num_short_del_occ && is_leaf == other.is_leaf;
  }

  constexpr bool operator!=(const Tree_pointers& other) const { return !(*this == other); }
  void           invalidate() { parent = INVALID; }

  // Add setters
  void set_parent(Tree_pos p) { parent = p; }
  void set_next_sibling(Tree_pos n) { next_sibling = n; }
  void set_prev_sibling(Tree_pos p) { prev_sibling = p; }
  void set_first_child_at(int16_t index, Tree_pos value) { first_child_ptrs[static_cast<size_t>(index)] = value; }
  void set_last_child_at(int16_t index, Tree_pos value) { last_child_ptrs[static_cast<size_t>(index)] = value; }
  void set_type_at(int16_t index, Type value) { types[static_cast<size_t>(index)] = value; }
  void set_num_short_del_occ(uint16_t n) { num_short_del_occ = n; }
  void set_is_leaf(bool l) { this->is_leaf = l; }

  // Helper methods for subnode links
  [[nodiscard]] bool     has_subnode() const { return parent < 0; }
  [[nodiscard]] Tree_pos get_subnode() const { return parent; }
  void                   set_subnode(Tree_pos ref) { parent = ref; }
};  // Tree_pointers class

class Tree : public std::enable_shared_from_this<Tree>, public Attr_host {
private:
  std::vector<Tree_pointers>   pointers_stack;
  std::vector<std::bitset<64>> validity_stack;
  std::vector<Tree_pos>        subnode_refs;
  Forest*                      forest_ptr;
  std::weak_ptr<Forest>        forest_owner_;
  std::weak_ptr<TreeIO>        treeio_owner_;
  Tid                          self_tid_   = INVALID;
  std::string                  name_       = "tree";
  uint64_t                     generation_ = 1;
  mutable bool                 dirty_      = true;

  void _ensure_subnode_ref_capacity(Tree_pos required_pos) {
    if (required_pos < static_cast<Tree_pos>(subnode_refs.size())) {
      return;
    }

    size_t       new_size = subnode_refs.empty() ? static_cast<size_t>(CHUNK_SIZE * 2) : subnode_refs.size();
    const size_t min_size = static_cast<size_t>(required_pos + 1);
    while (new_size < min_size) {
      new_size *= 2;
    }
    subnode_refs.resize(new_size, INVALID);
  }

  [[nodiscard]] inline bool _check_idx_exists(const Tree_pos& idx) const noexcept {
    return idx > INVALID && idx < static_cast<Tree_pos>(pointers_stack.size() << CHUNK_SHIFT);
  }

  [[nodiscard]] inline bool _contains_data(const Tree_pos& idx) const noexcept {
    const auto bitset_idx = idx >> 6;
    const auto bit_pos    = idx & 63;
    return bitset_idx < static_cast<Tree_pos>(validity_stack.size()) && validity_stack[bitset_idx][bit_pos];
  }

  inline void _set_data_valid(const Tree_pos& idx) noexcept {
    const auto bitset_idx = idx >> 6;
    const auto bit_pos    = idx & 63;
    if (bitset_idx >= static_cast<Tree_pos>(validity_stack.size())) {
      validity_stack.resize(bitset_idx + 1);
    }
    validity_stack[bitset_idx][bit_pos] = true;
  }

  inline void _set_data_invalid(const Tree_pos& idx) noexcept {
    const auto bitset_idx = idx >> 6;
    const auto bit_pos    = idx & 63;
    if (bitset_idx < static_cast<Tree_pos>(validity_stack.size())) {
      validity_stack[bitset_idx][bit_pos] = false;
    }
  }

  Tree_pos _create_space() {
    const auto start_pos = static_cast<Tree_pos>(pointers_stack.size() << CHUNK_SHIFT);
    pointers_stack.emplace_back();
    _set_data_valid(start_pos);
    return static_cast<Tree_pos>(pointers_stack.size() - 1);
  }

  Tree_pos _insert_chunk_after(const Tree_pos& curr) {
    const auto new_chunk_id = _create_space();

    pointers_stack[new_chunk_id].set_prev_sibling(curr);
    pointers_stack[new_chunk_id].set_next_sibling(pointers_stack[curr].get_next_sibling());
    pointers_stack[curr].set_next_sibling(new_chunk_id);

    if (pointers_stack[new_chunk_id].get_next_sibling() != INVALID) {
      pointers_stack[pointers_stack[new_chunk_id].get_next_sibling()].set_prev_sibling(new_chunk_id);
    }

    pointers_stack[new_chunk_id].set_parent(pointers_stack[curr].get_parent());

    return new_chunk_id;
  }

  void _update_parent_pointer(const Tree_pos& first_child, const Tree_pos& new_parent_id) {
    I(_check_idx_exists(first_child), "First child index out of range");
    I(_check_idx_exists(new_parent_id), "New parent index out of range");

    auto curr_chunk_id = (first_child >> CHUNK_SHIFT);
    while (curr_chunk_id != INVALID) {
      pointers_stack[curr_chunk_id].set_parent(new_parent_id);
      curr_chunk_id = pointers_stack[curr_chunk_id].get_next_sibling();
    }
  }

  Tree_pos _try_fit_child_ptr(const Tree_pos& parent_id, const Tree_pos& child_id) {
    I(_check_idx_exists(parent_id), "parent_id index out of range");
    I(_check_idx_exists(child_id), "child_id index out of range");

    const auto parent_chunk_id     = (parent_id >> CHUNK_SHIFT);
    const auto parent_chunk_offset = (parent_id & CHUNK_MASK);

    pointers_stack[parent_chunk_id].set_first_child_at(static_cast<int16_t>(parent_chunk_offset), child_id);
    pointers_stack[parent_chunk_id].set_is_leaf(false);

    return parent_id;
  }

  Tree* _get_forest_tree(Tree_pos subtree_tid);
  void  bind_forest_owner(std::weak_ptr<Forest> forest_owner) { forest_owner_ = std::move(forest_owner); }
  void  bind_treeio_owner(std::weak_ptr<TreeIO> treeio_owner, Tid self_tid, std::string_view name) {
    treeio_owner_ = std::move(treeio_owner);
    self_tid_     = self_tid;
    name_         = std::string(name);
  }
  Tree_pos _append_after_last_sibling(Tree_pos last_sibling_pos, Tree_pos parent_pos) {
    const auto last_chunk_id   = last_sibling_pos >> CHUNK_SHIFT;
    Tree_pos   new_sibling_pos = (last_chunk_id << CHUNK_SHIFT) + pointers_stack[last_chunk_id].get_num_short_del_occ();

    if ((new_sibling_pos & CHUNK_MASK) == CHUNK_MASK) {
      new_sibling_pos = _insert_chunk_after(new_sibling_pos >> CHUNK_SHIFT) << CHUNK_SHIFT;
    } else {
      ++new_sibling_pos;
      _set_data_valid(new_sibling_pos);
    }

    pointers_stack[new_sibling_pos >> CHUNK_SHIFT].set_num_short_del_occ(new_sibling_pos & CHUNK_MASK);
    pointers_stack[parent_pos >> CHUNK_SHIFT].set_is_leaf(false);
    pointers_stack[parent_pos >> CHUNK_SHIFT].set_last_child_at(static_cast<int16_t>(parent_pos & CHUNK_MASK), new_sibling_pos);

    return new_sibling_pos;
  }

public:
  class Node_class;

  struct PrintContext;

  struct PrintOptions {
    std::span<const Type_entry>                                                                       type_table = {};
    std::function<std::string(const Node_class&)>                                                     node_text;
    std::vector<std::pair<std::string, std::function<std::optional<std::string>(const Node_class&)>>> attributes;
    std::string_view                                                                                  indent        = "  ";
    bool                                                                                              show_types    = true;
    bool                                                                                              show_subnodes = true;
    std::function<bool(std::ostream&, const Node_class&, const PrintContext&)>                        format_node;
  };
  // PrintContext holds a Node_class by value, so its definition is below,
  // after Node_class is complete.

  [[nodiscard]] static std::shared_ptr<Tree> create(Forest* forest = nullptr) { return std::shared_ptr<Tree>(new Tree(forest)); }

  class Node_class {
  public:
    enum class Context : uint8_t { Class, Flat, Hier };

    Node_class() = default;
    Node_class(Tree* tree_value, Tree_pos current_pos_value)
        : tree_ptr(tree_value), current_pos(current_pos_value), generation(tree_value != nullptr ? tree_value->generation_ : 0) {}
    explicit Node_class(Tree_pos current_pos_value) : current_pos(current_pos_value) {}
    // Flat context: node produced by a cross-forest traversal that visits
    // every unique subtree body once. root_tid identifies the top tree body
    // where traversal started.
    Node_class(Tree* tree_value, Tree_pos current_pos_value, Tid root_tid_value)
        : tree_ptr(tree_value)
        , current_pos(current_pos_value)
        , generation(tree_value != nullptr ? tree_value->generation_ : 0)
        , context_(Context::Flat)
        , root_tid_(root_tid_value) {}
    // Hier context: node produced by a per-instance traversal. hier_pos is a
    // position into the auxiliary expansion tree (hier_tree); hier_tids maps
    // each hier_pos to the Tid of the body visited at that instance.
    Node_class(Tree* tree_value, Tree_pos current_pos_value, Tid root_tid_value, std::shared_ptr<Tree> hier_tree_value,
               std::shared_ptr<std::vector<Tid>> hier_tids_value, Tree_pos hier_pos_value)
        : tree_ptr(tree_value)
        , current_pos(current_pos_value)
        , generation(tree_value != nullptr ? tree_value->generation_ : 0)
        , context_(Context::Hier)
        , root_tid_(root_tid_value)
        , hier_tree_(std::move(hier_tree_value))
        , hier_tids_(std::move(hier_tids_value))
        , hier_pos_(hier_pos_value) {}

    [[nodiscard]] Tree*    get_tree() const noexcept { return tree_ptr; }
    [[nodiscard]] Tree_pos get_debug_nid() const noexcept { return current_pos; }
    [[nodiscard]] bool     is_valid() const noexcept {
      return tree_ptr != nullptr && generation == tree_ptr->generation_ && tree_ptr->_check_idx_exists(current_pos)
             && tree_ptr->_contains_data(current_pos);
    }
    [[nodiscard]] bool     is_invalid() const noexcept { return !is_valid(); }
    [[nodiscard]] Context  get_context() const noexcept { return context_; }
    [[nodiscard]] bool     is_class() const noexcept { return context_ == Context::Class; }
    [[nodiscard]] bool     is_flat() const noexcept { return context_ == Context::Flat; }
    [[nodiscard]] bool     is_hier() const noexcept { return context_ == Context::Hier; }
    [[nodiscard]] Tid      get_current_tid() const noexcept { return tree_ptr != nullptr ? tree_ptr->get_tid() : INVALID; }
    [[nodiscard]] Tid      get_root_tid() const noexcept { return context_ == Context::Class ? get_current_tid() : root_tid_; }
    [[nodiscard]] Tree_pos get_hier_pos() const noexcept { return hier_pos_; }
    [[nodiscard]] const std::shared_ptr<Tree>&             get_hier_tree() const noexcept { return hier_tree_; }
    [[nodiscard]] const std::shared_ptr<std::vector<Tid>>& get_hier_tids() const noexcept { return hier_tids_; }

    // Opaque, hashable keys for use in user-owned maps. Prefer these over using
    // Node_class directly as a map key.
    [[nodiscard]] Tree_class_index get_class_index() const noexcept { return Tree_class_index{current_pos}; }
    [[nodiscard]] Tree_flat_index  get_flat_index() const noexcept {
      I(tree_ptr != nullptr, "get_flat_index: node is not attached to a tree");
      return Tree_flat_index{tree_ptr->get_tid(), current_pos};
    }
    [[nodiscard]] Tree_hier_index get_hier_index() const noexcept {
      I(context_ == Context::Hier, "get_hier_index: requires hier traversal context");
      return Tree_hier_index{hier_pos_, current_pos};
    }
    [[nodiscard]] auto       add_child() const -> Node_class;
    [[nodiscard]] Node_class append_sibling() const;
    [[nodiscard]] Node_class insert_next_sibling() const;
    void                     set_subnode(const std::shared_ptr<TreeIO>& treeio) const;
    void                     set_type(Type type) const;
    [[nodiscard]] Type       get_type() const;
    void                     del_node() const;

    [[nodiscard]] Node_class parent() const;
    [[nodiscard]] Node_class first_child() const;
    [[nodiscard]] Node_class last_child() const;
    [[nodiscard]] Node_class next_sibling() const;
    [[nodiscard]] Node_class prev_sibling() const;
    [[nodiscard]] bool       is_leaf() const;
    [[nodiscard]] bool       is_first_child() const;
    [[nodiscard]] bool       is_last_child() const;
    [[nodiscard]] bool       is_subnode() const;
    [[nodiscard]] Tree*      get_subnode() const;
    [[nodiscard]] Tid        get_subnode_tid() const;

    [[nodiscard]] auto pre_order_class() const;
    [[nodiscard]] auto post_order_class() const;
    [[nodiscard]] auto sibling_order() const;
    [[nodiscard]] auto pre_order_flat() const;
    [[nodiscard]] auto pre_order_hier() const;
    [[nodiscard]] auto post_order_flat() const;
    [[nodiscard]] auto post_order_hier() const;

    template <Attribute Tag>
    [[nodiscard]] AttrRef<Tag> attr(Tag = {}) const {
      I(tree_ptr != nullptr, "attr: node is not attached to a tree");
      return AttrRef<Tag>(tree_ptr, make_node_attr_key(static_cast<uint64_t>(current_pos)));
    }

    [[nodiscard]] bool operator==(const Node_class& other) const noexcept { return current_pos == other.current_pos; }
    [[nodiscard]] bool operator!=(const Node_class& other) const noexcept { return !(*this == other); }

    template <typename H>
    friend H AbslHashValue(H h, const Node_class& node) {
      return H::combine(std::move(h), node.current_pos);
    }

  private:
    Tree*                             tree_ptr    = nullptr;
    Tree_pos                          current_pos = INVALID;
    uint64_t                          generation  = 0;
    Context                           context_    = Context::Class;
    Tid                               root_tid_   = INVALID;
    std::shared_ptr<Tree>             hier_tree_;
    std::shared_ptr<std::vector<Tid>> hier_tids_;
    Tree_pos                          hier_pos_ = INVALID;

    friend class Tree;
  };

  struct PrintContext {
    const Tree&         tree;
    const PrintOptions& options;
    size_t              depth;
    Node_class          node;

    void emit_indent(std::ostream& os) const;
    void emit_indent(std::ostream& os, size_t d) const;

    [[nodiscard]] std::vector<Node_class> get_children() const;

    void print_children_default(std::ostream& os) const;
    void print_child_default(std::ostream& os, const Node_class& child) const;
  };

  Tree(const Tree&)            = delete;
  Tree& operator=(const Tree&) = delete;
  Tree(Tree&&)                 = delete;
  Tree& operator=(Tree&&)      = delete;

  [[nodiscard]] Node_class get_root_node() const { return as_class(get_root()); }

  // Reverse-lookup from an opaque index key back to a Node_class. These are
  // the only supported way to round-trip a handle across a save/load or any
  // other scope where the live Node_class instance doesn't survive —
  // Tree_pos is an internal addressing type and is not part of the public
  // API surface.
  [[nodiscard]] Node_class get_node(Tree_class_index idx) const { return as_class(idx.value); }
  [[nodiscard]] Node_class get_node(Tree_hier_index idx) const {
    // Reconstructs a Hier-context handle without the traversal's expansion
    // tree. Valid for attr/type access at `idx.hier_pos`; it cannot continue
    // to descend into subnodes (those need the original traversal result).
    if (!(_check_idx_exists(idx.value) && _contains_data(idx.value))) {
      return Node_class();
    }
    return Node_class(const_cast<Tree*>(this), idx.value, get_tid(), nullptr, nullptr, idx.hier_pos);
  }

  // Node_class-level navigation and mutation is done on the handle itself
  // (e.g. `node.first_child()`, `node.add_child()`). See hhds/tree.hpp's
  // Tree::Node_class definition for the full list; Tree-level forwarders
  // taking Node_class are intentionally absent to keep the public API
  // narrow.
  void       clear();
  Node_class add_root_node() { return as_class(add_root()); }
  void       set_name(std::string_view n);
  [[nodiscard]] std::string_view        get_name() const { return name_; }
  [[nodiscard]] Tid                     get_tid() const noexcept { return self_tid_; }
  [[nodiscard]] std::shared_ptr<TreeIO> get_io() const { return treeio_owner_.lock(); }

  void print(std::ostream& os) const { print(os, get_root(), PrintOptions{}); }
  void print(std::ostream& os, const PrintOptions& options) const { print(os, get_root(), options); }
  void print(std::ostream& os, Node_class start_node) const { print(os, start_node.get_debug_nid(), PrintOptions{}); }
  void print(std::ostream& os, Node_class start_node, const PrintOptions& options) const {
    print(os, start_node.get_debug_nid(), options);
  }

  [[nodiscard]] std::string print() const { return print(PrintOptions{}); }
  [[nodiscard]] std::string print(const PrintOptions& options) const;

  void                      dump(std::ostream& os) const { dump(os, get_root(), PrintOptions{}); }
  void                      dump(std::ostream& os, const PrintOptions& options) const { dump(os, get_root(), options); }
  [[nodiscard]] std::string dump() const { return dump(PrintOptions{}); }
  [[nodiscard]] std::string dump(const PrintOptions& options) const;

  // Per-node data recovered during read_dump (node_text and attributes are not stored in tree)
  struct NodeData {
    Tree_pos                                         pos;
    std::string                                      node_text;
    std::vector<std::pair<std::string, std::string>> attributes;
  };

  struct ReadDumpResult {
    std::shared_ptr<Tree> tree;
    std::vector<NodeData> nodes;  // one per node, in pre-order
  };

  void write_dump(std::ostream& os, const PrintOptions& options) const;
  void write_dump(const std::string& filename, const PrintOptions& options) const;

  [[nodiscard]] static ReadDumpResult read_dump(std::istream& is, std::span<const Type_entry> type_table);
  [[nodiscard]] static ReadDumpResult read_dump(const std::string& filename, std::span<const Type_entry> type_table);

  [[nodiscard]] bool is_dirty() const noexcept { return dirty_; }

private:
  // Raw-Tree_pos navigation / mutation primitives. These are the
  // implementation backbone; every public entry point converts between
  // Node_class handles and these internal calls. Not for public use —
  // Tree_pos is an internal addressing type.
  [[nodiscard]] Node_class as_class(Tree_pos node_pos) const {
    return (_check_idx_exists(node_pos) && _contains_data(node_pos)) ? Node_class(const_cast<Tree*>(this), node_pos) : Node_class();
  }
  [[nodiscard]] Tree_pos get_root() const { return ROOT; }

  [[nodiscard]] inline Tree_pos get_parent(const Tree_pos& node_pos) const {
    return pointers_stack[node_pos >> CHUNK_SHIFT].get_parent();
  }
  [[nodiscard]] Tree_pos   get_last_child(const Tree_pos& parent_pos) const;
  [[nodiscard]] Tree_pos   get_first_child(const Tree_pos& parent_pos) const;
  [[nodiscard]] Type       get_type(const Tree_pos& node_pos) const {
    I(_check_idx_exists(node_pos), "get_type: Node index out of range");
    const auto chunk_id     = (node_pos >> CHUNK_SHIFT);
    const auto chunk_offset = static_cast<int16_t>(node_pos & CHUNK_MASK);
    return pointers_stack[chunk_id].get_type_at(chunk_offset);
  }
  [[nodiscard]] bool        is_last_child(const Tree_pos& node_pos) const;
  [[nodiscard]] bool        is_first_child(const Tree_pos& node_pos) const;
  [[nodiscard]] Tree_pos    get_sibling_next(const Tree_pos& node_pos) const;
  [[nodiscard]] Tree_pos    get_sibling_prev(const Tree_pos& node_pos) const;
  [[nodiscard]] inline bool is_leaf(const Tree_pos& node_pos) const {
    return pointers_stack[node_pos >> CHUNK_SHIFT].get_is_leaf();
  }
  Tree_pos   append_sibling(const Tree_pos& sibling_pos);
  Tree_pos   add_child(const Tree_pos& parent_pos);
  Tree_pos   add_root();
  void       set_type(const Tree_pos& node_pos, Type type) {
    I(_check_idx_exists(node_pos), "set_type: Node index out of range");
    dirty_                  = true;
    const auto chunk_id     = (node_pos >> CHUNK_SHIFT);
    const auto chunk_offset = static_cast<int16_t>(node_pos & CHUNK_MASK);
    pointers_stack[chunk_id].set_type_at(chunk_offset, type);
  }
  void       delete_leaf(const Tree_pos& leaf_pos);
  void       delete_subtree(const Tree_pos& subtree_root_pos);
  void       set_subnode(const Tree_pos& node_pos, Tid subnode_tid);
  Tree_pos   insert_next_sibling(const Tree_pos& sibling_pos);

  void print(std::ostream& os, Tree_pos start_pos, const PrintOptions& options) const;
  [[nodiscard]] std::string print(Tree_pos start_pos, const PrintOptions& options) const;
  void                      dump(std::ostream& os, Tree_pos start_pos, const PrintOptions& options) const;
  [[nodiscard]] std::string dump(Tree_pos start_pos, const PrintOptions& options) const;

  // Debug helper that dumps raw chunk/pointer state for every slot. Not part
  // of the public contract — shape and output format can change at any time.
  void _debug_print_chunks(int deep = 0) {
    for (size_t i = 0; i < pointers_stack.size(); i++) {
      std::cout << "Index: " << (i << CHUNK_SHIFT) << " Parent: " << pointers_stack[i].get_parent()
                << " Data: " << (_contains_data(i << CHUNK_SHIFT) ? "VALID" : "INVALID") << std::endl;
      std::cout << "First Child[0]: " << pointers_stack[i].get_first_child_at(0) << " ";
      std::cout << "Next Sibling: " << pointers_stack[i].get_next_sibling() << " ";
      std::cout << "Prev Sibling: " << pointers_stack[i].get_prev_sibling() << " ";
      std::cout << "Num Occ: " << pointers_stack[i].get_num_short_del_occ() << std::endl;
      std::cout << "Is Leaf: " << pointers_stack[i].get_is_leaf() << std::endl;
      std::cout << std::endl;
    }

    if (!deep) {
      return;
    }

    std::cout << std::endl;
    for (Tree_pos i = ROOT; i < static_cast<Tree_pos>(pointers_stack.size() << CHUNK_SHIFT); ++i) {
      if (_contains_data(i)) {
        std::cout << "Index: " << i << " Data: VALID" << std::endl;
        std::cout << "PAR : " << get_parent(i) << std::endl;
        std::cout << "FC  : " << get_first_child(i) << std::endl;
        std::cout << "LC  : " << get_last_child(i) << std::endl;
        std::cout << "NS  : " << get_sibling_next(i) << std::endl;
        std::cout << "PS  : " << get_sibling_prev(i) << std::endl;
        std::cout << std::endl;
      }
    }
  }

public:
  template <typename Derived>
  class traversal_iterator_base {
  protected:
    Tree_pos    current;
    const Tree* tree_ptr;
    bool        m_follow_subtrees;

  public:
    using iterator_category = std::forward_iterator_tag;
    using value_type        = Node_class;
    using difference_type   = std::ptrdiff_t;
    using pointer           = void;
    using reference         = Node_class;

    traversal_iterator_base(Tree_pos start, const Tree* tree, bool follow_refs)
        : current(start), tree_ptr(tree), m_follow_subtrees(follow_refs) {}

    bool       operator==(const traversal_iterator_base& other) const { return current == other.current; }
    bool       operator!=(const traversal_iterator_base& other) const { return current != other.current; }
    Node_class operator*() const { return tree_ptr->as_class(current); }

  protected:
    Tree_pos handle_subnode_ref(Tree_pos node_pos) {
      if (m_follow_subtrees && tree_ptr->forest_ptr) {
        Tree_pos subnode_tid = tree_ptr->get_subnode(node_pos);
        if (subnode_tid != INVALID) {
          if (subnode_tid < 0) {
            return ROOT;
          }
        }
      }
      return INVALID;
    }
  };

  class sibling_order_iterator : public traversal_iterator_base<sibling_order_iterator> {
  private:
    using base = traversal_iterator_base<sibling_order_iterator>;
    using base::current;
    using base::tree_ptr;

  public:
    using iterator_category = std::forward_iterator_tag;
    using value_type        = Node_class;
    using difference_type   = std::ptrdiff_t;
    using pointer           = void;
    using reference         = Node_class;

    sibling_order_iterator(Tree_pos start, Tree* tree, bool follow_refs) : base(start, tree, follow_refs) {}

    sibling_order_iterator& operator++() {
      Tree_pos subnode_root = this->handle_subnode_ref(this->current);
      if (subnode_root != INVALID) {
        this->current = subnode_root;
        return *this;
      }
      if (tree_ptr->get_sibling_next(current) != INVALID) {
        current = tree_ptr->get_sibling_next(current);
      } else {
        current = INVALID;
      }
      return *this;
    }
  };

  class sibling_order_range {
  private:
    Tree_pos m_start;
    Tree*    m_tree_ptr;
    bool     m_follow_subtrees;

  public:
    sibling_order_range(Tree_pos start, Tree* tree, bool follow_subtrees = false)
        : m_start(start), m_tree_ptr(tree), m_follow_subtrees(follow_subtrees) {}

    sibling_order_iterator begin() {
      const Tree_pos start = (m_tree_ptr != nullptr && m_tree_ptr->_contains_data(m_start)) ? m_start : INVALID;
      return sibling_order_iterator(start, m_tree_ptr, m_follow_subtrees);
    }
    sibling_order_iterator end() { return sibling_order_iterator(INVALID, m_tree_ptr, m_follow_subtrees); }
  };

private:
  sibling_order_range sibling_order(Tree_pos start, bool follow_subtrees = false) {
    return sibling_order_range(start, this, follow_subtrees);
  }

public:
  sibling_order_range sibling_order(Node_class start, bool follow_subtrees = false) {
    return sibling_order_range(start.get_debug_nid(), this, follow_subtrees);
  }

  class const_sibling_order_iterator : public traversal_iterator_base<const_sibling_order_iterator> {
  private:
    using base = traversal_iterator_base<const_sibling_order_iterator>;
    using base::current;
    using base::tree_ptr;

  public:
    using iterator_category = std::forward_iterator_tag;
    using value_type        = Node_class;
    using difference_type   = std::ptrdiff_t;
    using pointer           = void;
    using reference         = Node_class;

    const_sibling_order_iterator(Tree_pos start, const Tree* tree, bool follow_refs) : base(start, tree, follow_refs) {}

    const_sibling_order_iterator& operator++() {
      Tree_pos subnode_root = this->handle_subnode_ref(this->current);
      if (subnode_root != INVALID) {
        this->current = subnode_root;
        return *this;
      }
      if (tree_ptr->get_sibling_next(current) != INVALID) {
        current = tree_ptr->get_sibling_next(current);
      } else {
        current = INVALID;
      }
      return *this;
    }
  };

  class const_sibling_order_range {
  private:
    Tree_pos    m_start;
    const Tree* m_tree_ptr;
    bool        m_follow_subtrees;

  public:
    const_sibling_order_range(Tree_pos start, const Tree* tree, bool follow_subtrees = false)
        : m_start(start), m_tree_ptr(tree), m_follow_subtrees(follow_subtrees) {}

    const_sibling_order_iterator begin() const {
      const Tree_pos start = (m_tree_ptr != nullptr && m_tree_ptr->_contains_data(m_start)) ? m_start : INVALID;
      return const_sibling_order_iterator(start, m_tree_ptr, m_follow_subtrees);
    }
    const_sibling_order_iterator end() const { return const_sibling_order_iterator(INVALID, m_tree_ptr, m_follow_subtrees); }
  };

private:
  const_sibling_order_range sibling_order(Tree_pos start) const { return const_sibling_order_range(start, this); }

public:
  const_sibling_order_range sibling_order(Node_class start) const {
    return const_sibling_order_range(start.get_debug_nid(), this);
  }

  class pre_order_iterator {
  private:
    Tree_pos    current;
    Tree_pos    start;
    const Tree* tree_ptr;

    inline Tree_pos fast_get_first_child(Tree_pos parent_index) const {
      const auto chunk_id     = (parent_index >> CHUNK_SHIFT);
      const auto chunk_offset = (parent_index & CHUNK_MASK);
      return tree_ptr->pointers_stack[chunk_id].get_first_child_at(static_cast<int16_t>(chunk_offset));
    }

    inline Tree_pos fast_get_sibling_next(Tree_pos sibling_id) const {
      const auto curr_chunk_id     = (sibling_id >> CHUNK_SHIFT);
      const auto curr_chunk_offset = (sibling_id & CHUNK_MASK);

      if (curr_chunk_offset < CHUNK_MASK) {
        const auto next_chunk_occ = tree_ptr->pointers_stack[curr_chunk_id].get_num_short_del_occ();
        if ((curr_chunk_offset + 1) <= next_chunk_occ) {
          return (curr_chunk_id << CHUNK_SHIFT) + curr_chunk_offset + 1;
        }
      }

      const auto next_sibling_chunk = tree_ptr->pointers_stack[curr_chunk_id].get_next_sibling();
      return static_cast<Tree_pos>(next_sibling_chunk << CHUNK_SHIFT);
    }

    inline Tree_pos fast_get_parent(Tree_pos index) const { return tree_ptr->pointers_stack[index >> CHUNK_SHIFT].get_parent(); }
    inline bool     fast_is_leaf(Tree_pos index) const { return tree_ptr->pointers_stack[index >> CHUNK_SHIFT].get_is_leaf(); }

  public:
    using iterator_category = std::forward_iterator_tag;
    using value_type        = Node_class;
    using difference_type   = std::ptrdiff_t;
    using pointer           = void;
    using reference         = Node_class;

    pre_order_iterator(Tree_pos start_pos, const Tree* tree, bool /* follow_subtrees */ = false)
        : current(start_pos), start(start_pos), tree_ptr(tree) {}

    pre_order_iterator& operator++() {
      if (!fast_is_leaf(current)) {
        auto child = fast_get_first_child(current);
        if (child != INVALID) {
          current = child;
          return *this;
        }
      }

      auto sibling = fast_get_sibling_next(current);
      if (sibling != INVALID) {
        current = sibling;
        return *this;
      }

      auto parent = fast_get_parent(current);
      while (parent != start && parent != INVALID) {
        auto parent_sibling = fast_get_sibling_next(parent);
        if (parent_sibling != INVALID) {
          current = parent_sibling;
          return *this;
        }
        parent = fast_get_parent(parent);
      }

      current = INVALID;
      return *this;
    }

    bool       operator==(const pre_order_iterator& other) const { return current == other.current; }
    bool       operator!=(const pre_order_iterator& other) const { return current != other.current; }
    Node_class operator*() const { return tree_ptr->as_class(current); }
  };

  class pre_order_range {
  private:
    Tree_pos    m_start;
    const Tree* m_tree_ptr;
    bool        m_follow_subtrees;

  public:
    pre_order_range(Tree_pos start, const Tree* tree, bool follow_subtrees = false)
        : m_start(start), m_tree_ptr(tree), m_follow_subtrees(follow_subtrees) {}

    pre_order_iterator begin() const {
      const Tree_pos start = (m_tree_ptr != nullptr && m_tree_ptr->_contains_data(m_start)) ? m_start : INVALID;
      return pre_order_iterator(start, m_tree_ptr, m_follow_subtrees);
    }
    pre_order_iterator end() const { return pre_order_iterator(INVALID, m_tree_ptr, m_follow_subtrees); }
  };

private:
  pre_order_range pre_order(Tree_pos start) const { return pre_order_range(start, this, false); }

public:
  pre_order_range pre_order() const { return pre_order_range(ROOT, this, false); }
  pre_order_range pre_order(Node_class start) const { return pre_order_range(start.get_debug_nid(), this, false); }

  class pre_order_iterator_with_subtrees : public traversal_iterator_base<pre_order_iterator_with_subtrees> {
  public:
    std::set<Tree_pos>    visited_subtrees;
    Tree*                 current_tree;
    Tree*                 main_tree;
    std::vector<Tree_pos> prev_trees;
    std::vector<Tree_pos> return_to_nodes;

  private:
    using base = traversal_iterator_base<pre_order_iterator_with_subtrees>;
    using base::current;
    using base::m_follow_subtrees;

  public:
    using iterator_category = std::forward_iterator_tag;
    using value_type        = Node_class;
    using difference_type   = std::ptrdiff_t;
    using pointer           = void;
    using reference         = Node_class;

    pre_order_iterator_with_subtrees(Tree_pos start, Tree* tree, bool follow_refs)
        : base(start, tree, follow_refs), current_tree(tree), main_tree(tree) {}

    pre_order_iterator_with_subtrees& operator++() {
      if (m_follow_subtrees && current_tree->forest_ptr) {
        Tree_pos ref = current_tree->get_subnode(current);
        if (ref != INVALID) {
          if (ref < 0 && visited_subtrees.find(ref) == visited_subtrees.end()) {
            visited_subtrees.insert(ref);
            prev_trees.push_back(ref);
            return_to_nodes.push_back(current);
            current_tree = current_tree->_get_forest_tree(ref);
            current      = ROOT;
            return *this;
          }
        }
      }

      if (!current_tree->is_leaf(current)) {
        auto new_cur = current_tree->get_first_child(current);
        if (new_cur != INVALID) {
          current = new_cur;
          return *this;
        }
      }

      auto nxt = current_tree->get_sibling_next(current);
      if (nxt != INVALID) {
        current = nxt;
        return *this;
      }

      auto parent = current_tree->get_parent(current);
      while (parent != ROOT && parent != INVALID) {
        auto parent_sibling = current_tree->get_sibling_next(parent);
        if (parent_sibling != INVALID) {
          current = parent_sibling;
          return *this;
        }
        parent = current_tree->get_parent(parent);
      }

      if (current_tree != main_tree) {
        Tree_pos r_node;
        if (prev_trees.size() <= 1) {
          current_tree = main_tree;
          r_node       = return_to_nodes.back();
          return_to_nodes.pop_back();
          prev_trees.clear();
        } else {
          prev_trees.pop_back();
          current_tree = current_tree->_get_forest_tree(prev_trees.back());
          r_node       = return_to_nodes.back();
          return_to_nodes.pop_back();
        }

        if (!current_tree->is_leaf(r_node)) {
          auto new_cur = current_tree->get_first_child(r_node);
          if (new_cur != INVALID) {
            current = new_cur;
            return *this;
          }
        }

        current = current_tree->get_sibling_next(r_node);
        if (current != INVALID) {
          return *this;
        }
      }

      current = INVALID;
      return *this;
    }

    bool operator==(const pre_order_iterator_with_subtrees& other) const {
      return current == other.current && current_tree == other.current_tree;
    }
    bool       operator!=(const pre_order_iterator_with_subtrees& other) const { return !(*this == other); }
    Node_class operator*() const { return current_tree->as_class(current); }
  };

  class pre_order_range_with_subtrees {
  private:
    Tree_pos m_start;
    Tree*    m_tree_ptr;
    bool     m_follow_subtrees;

  public:
    pre_order_range_with_subtrees(Tree_pos start, Tree* tree, bool follow_subtrees = false)
        : m_start(start), m_tree_ptr(tree), m_follow_subtrees(follow_subtrees) {}

    pre_order_iterator_with_subtrees begin() {
      const Tree_pos start = (m_tree_ptr != nullptr && m_tree_ptr->_contains_data(m_start)) ? m_start : INVALID;
      return pre_order_iterator_with_subtrees(start, m_tree_ptr, m_follow_subtrees);
    }
    pre_order_iterator_with_subtrees end() { return pre_order_iterator_with_subtrees(INVALID, m_tree_ptr, m_follow_subtrees); }
  };

private:
  pre_order_range_with_subtrees pre_order_with_subtrees(Tree_pos start, bool follow_subtrees = false) {
    return pre_order_range_with_subtrees(start, this, follow_subtrees);
  }

public:
  pre_order_range_with_subtrees pre_order_with_subtrees(bool follow_subtrees = false) {
    return pre_order_range_with_subtrees(ROOT, this, follow_subtrees);
  }
  pre_order_range_with_subtrees pre_order_with_subtrees(Node_class start, bool follow_subtrees = false) {
    return pre_order_range_with_subtrees(start.get_debug_nid(), this, follow_subtrees);
  }

  class post_order_iterator : public traversal_iterator_base<post_order_iterator> {
  private:
    using base = traversal_iterator_base<post_order_iterator>;
    using base::current;
    using base::tree_ptr;
    Tree_pos start;

    void descend_to_first_post_order() {
      while (current != INVALID && tree_ptr->get_first_child(current) != INVALID) {
        current = tree_ptr->get_first_child(current);
      }
    }

  public:
    using iterator_category = std::forward_iterator_tag;
    using value_type        = Node_class;
    using difference_type   = std::ptrdiff_t;
    using pointer           = void;
    using reference         = Node_class;

    post_order_iterator(Tree_pos start_pos, Tree* tree, bool follow_refs) : base(start_pos, tree, follow_refs), start(start_pos) {
      descend_to_first_post_order();
    }

    post_order_iterator& operator++() {
      Tree_pos subnode_root = this->handle_subnode_ref(this->current);
      if (subnode_root != INVALID) {
        this->current = subnode_root;
        return *this;
      }

      if (current == start) {
        current = INVALID;
      } else if (tree_ptr->get_sibling_next(current) != INVALID) {
        auto next = tree_ptr->get_sibling_next(current);
        while (tree_ptr->get_first_child(next) != INVALID) {
          next = tree_ptr->get_first_child(next);
        }

        current = next;
      } else {
        current = tree_ptr->get_parent(current);
      }
      return *this;
    }
  };

  class post_order_range {
  private:
    Tree_pos m_start;
    Tree*    m_tree_ptr;
    bool     m_follow_subtrees;

  public:
    post_order_range(Tree_pos start, Tree* tree, bool follow_subtrees = false)
        : m_start(start), m_tree_ptr(tree), m_follow_subtrees(follow_subtrees) {}

    post_order_iterator begin() {
      const Tree_pos start = (m_tree_ptr != nullptr && m_tree_ptr->_contains_data(m_start)) ? m_start : INVALID;
      return post_order_iterator(start, m_tree_ptr, m_follow_subtrees);
    }
    post_order_iterator end() { return post_order_iterator(INVALID, m_tree_ptr, m_follow_subtrees); }
  };

private:
  post_order_range post_order(Tree_pos start, bool follow_subtrees = false) {
    return post_order_range(start, this, follow_subtrees);
  }

public:
  post_order_range post_order(bool follow_subtrees = false) {
    return post_order_range(ROOT, this, follow_subtrees);
  }
  post_order_range post_order(Node_class start, bool follow_subtrees = false) {
    return post_order_range(start.get_debug_nid(), this, follow_subtrees);
  }

  // Flat/hier traversals — cross subnode references across the forest.
  //
  // pre_order_flat / post_order_flat visit each unique subtree body exactly
  // once (deduplicated by Tid). Nodes are emitted with Flat context, so
  // get_flat_index() is the natural map key.
  //
  // pre_order_hier / post_order_hier visit each *instance* of a subtree body
  // in its caller context — same body can appear multiple times. Nodes are
  // emitted with Hier context backed by an auxiliary expansion tree; two
  // instances get different Tree_hier_index values because their hier_pos
  // differ in that expansion tree.
  //
  // Both walks guard against cycles: flat dedups by Tid globally, hier keeps
  // an "active on stack" Tid set. Returned vectors own their own state —
  // hier additionally pins the expansion tree via shared_ptr on each Node.
private:
  [[nodiscard]] std::vector<Node_class> pre_order_flat(Tree_pos start) const {
    std::vector<Node_class> out;
    if (!_contains_data(start)) {
      return out;
    }
    std::set<Tid> visited_tids;
    if (self_tid_ != INVALID) {
      visited_tids.insert(self_tid_);
    }
    const_cast<Tree*>(this)->pre_order_flat_impl(start, self_tid_, visited_tids, out);
    return out;
  }

public:
  [[nodiscard]] std::vector<Node_class> pre_order_flat() const { return pre_order_flat(static_cast<Tree_pos>(ROOT)); }
  [[nodiscard]] std::vector<Node_class> pre_order_flat(Node_class start) const { return pre_order_flat(start.get_debug_nid()); }

private:
  [[nodiscard]] std::vector<Node_class> post_order_flat(Tree_pos start) const {
    std::vector<Node_class> out;
    if (!_contains_data(start)) {
      return out;
    }
    std::set<Tid> visited_tids;
    if (self_tid_ != INVALID) {
      visited_tids.insert(self_tid_);
    }
    const_cast<Tree*>(this)->post_order_flat_impl(start, self_tid_, visited_tids, out);
    return out;
  }

public:
  [[nodiscard]] std::vector<Node_class> post_order_flat() const { return post_order_flat(static_cast<Tree_pos>(ROOT)); }
  [[nodiscard]] std::vector<Node_class> post_order_flat(Node_class start) const { return post_order_flat(start.get_debug_nid()); }

private:
  [[nodiscard]] std::vector<Node_class> pre_order_hier(Tree_pos start) const {
    std::vector<Node_class> out;
    if (!_contains_data(start)) {
      return out;
    }
    auto           hier_tree     = Tree::create();
    const Tree_pos root_hier_pos = hier_tree->add_root();
    auto           hier_tids     = std::make_shared<std::vector<Tid>>();
    if (static_cast<size_t>(root_hier_pos + 1) > hier_tids->size()) {
      hier_tids->resize(static_cast<size_t>(root_hier_pos + 1), INVALID);
    }
    (*hier_tids)[static_cast<size_t>(root_hier_pos)] = self_tid_;
    std::set<Tid> active_tids;
    if (self_tid_ != INVALID) {
      active_tids.insert(self_tid_);
    }
    const_cast<Tree*>(this)->pre_order_hier_impl(start, self_tid_, hier_tree, hier_tids, root_hier_pos, active_tids, out);
    return out;
  }

public:
  [[nodiscard]] std::vector<Node_class> pre_order_hier() const { return pre_order_hier(static_cast<Tree_pos>(ROOT)); }
  [[nodiscard]] std::vector<Node_class> pre_order_hier(Node_class start) const { return pre_order_hier(start.get_debug_nid()); }

private:
  [[nodiscard]] std::vector<Node_class> post_order_hier(Tree_pos start) const {
    std::vector<Node_class> out;
    if (!_contains_data(start)) {
      return out;
    }
    auto           hier_tree     = Tree::create();
    const Tree_pos root_hier_pos = hier_tree->add_root();
    auto           hier_tids     = std::make_shared<std::vector<Tid>>();
    if (static_cast<size_t>(root_hier_pos + 1) > hier_tids->size()) {
      hier_tids->resize(static_cast<size_t>(root_hier_pos + 1), INVALID);
    }
    (*hier_tids)[static_cast<size_t>(root_hier_pos)] = self_tid_;
    std::set<Tid> active_tids;
    if (self_tid_ != INVALID) {
      active_tids.insert(self_tid_);
    }
    const_cast<Tree*>(this)->post_order_hier_impl(start, self_tid_, hier_tree, hier_tids, root_hier_pos, active_tids, out);
    return out;
  }

public:
  [[nodiscard]] std::vector<Node_class> post_order_hier() const { return post_order_hier(static_cast<Tree_pos>(ROOT)); }
  [[nodiscard]] std::vector<Node_class> post_order_hier(Node_class start) const { return post_order_hier(start.get_debug_nid()); }

private:
  [[nodiscard]] bool     has_subnode(Tree_pos node_pos) const { return get_subnode(node_pos) < 0; }
  [[nodiscard]] size_t   get_subnode_index(Tree_pos node_pos) const { return static_cast<size_t>(-get_subnode(node_pos) - 1); }
  [[nodiscard]] Tree_pos make_subnode_ref(size_t subnode_index) const { return static_cast<Tree_pos>(-(subnode_index + 1)); }
  [[nodiscard]] Tree_pos get_subnode(Tree_pos node_pos) const {
    I(_check_idx_exists(node_pos), "get_subnode: Node index out of range");
    return node_pos < static_cast<Tree_pos>(subnode_refs.size()) ? subnode_refs[node_pos] : INVALID;
  }

public:
  // Subnode access is handle-level: node.is_subnode(), node.get_subnode()
  // (returns Tree*), node.get_subnode_tid() (debug). Tree-level Node_class
  // forwarders intentionally omitted.
  [[nodiscard]] auto get_subs() const -> std::span<const Node_class> {
    if (!subs_cache_valid_) {
      subs_cache_.clear();
      for (Tree_pos node_pos = ROOT; node_pos < static_cast<Tree_pos>(pointers_stack.size() << CHUNK_SHIFT); ++node_pos) {
        if (_contains_data(node_pos) && get_subnode(node_pos) != INVALID) {
          subs_cache_.push_back(Node_class(node_pos));
        }
      }
      subs_cache_valid_ = true;
    }
    return subs_cache_;
  }
private:
  [[nodiscard]] TreeCursor create_cursor(Tree_pos start);

public:
  [[nodiscard]] TreeCursor create_cursor();
  [[nodiscard]] TreeCursor create_cursor(Node_class start);

  friend class Forest;
  friend class TreeCursor;
  friend class ForestCursor;
  friend class Graph;  // graph uses raw Tree_pos for its internal hier-expansion tree cache

private:
  // Binary persistence — saves/loads body data (pointers_stack, validity_stack, subnode_refs).
  // dir_path is the tree-specific directory (e.g., "db/tree_1/").
  void save_body(const std::string& dir_path) const;
  void load_body(const std::string& dir_path);

  struct PrintAlign {
    size_t pos_width  = 0;  // max digits in %pos (to align '=')
    size_t name_width = 0;  // max node_text length among nodes with ':' (to align ':')
    size_t body_width = 0;  // max width between '= ' and ' @(' (to align '@')
  };

  [[nodiscard]] Type_entry                 resolve_print_type(Type type, const PrintOptions& options) const;
  [[nodiscard]] size_t                     node_body_width(Tree_pos c, size_t name_width, const PrintOptions& options) const;
  [[nodiscard]] std::optional<std::string> node_text_override(Tree_pos node_pos, const PrintOptions& options) const;
  void print_node(std::ostream& os, Tree_pos node_pos, size_t depth, const PrintAlign& align, const PrintOptions& options) const;
  void dump_node(std::ostream& os, Tree_pos node_pos, const std::string& prefix, bool is_last, const PrintOptions& options) const;
  void write_dump_node(std::ostream& os, Tree_pos node_pos, const std::string& prefix, bool is_last,
                       const PrintOptions& options) const;
  void scan_align_group(PrintAlign& align, Tree_pos first_child, const PrintOptions& options) const;
  [[nodiscard]] PrintAlign compute_sibling_align(Tree_pos first_child, const PrintOptions& options) const;
  void                     recompute_body_width(PrintAlign& align, Tree_pos first_child, const PrintOptions& options) const;
  void                     recompute_body_width_recurse(PrintAlign& align, Tree_pos first_child, const PrintOptions& options) const;
  void                     attr_note_modified() noexcept override { dirty_ = true; }
  mutable std::vector<Node_class> subs_cache_;
  mutable bool                    subs_cache_valid_ = false;

  // Eager flat/hier walkers shared by pre_order_flat/hier and post_order_flat/hier.
  // Either descend into the subnode reference (when unvisited / not on the
  // active stack) or into the node's regular children — mirrors the subnode
  // semantics used by the existing pre_order_iterator_with_subtrees: a
  // subnode ref replaces the children subtree during traversal.
  void pre_order_flat_impl(Tree_pos node_pos, Tid root_tid, std::set<Tid>& visited_tids, std::vector<Node_class>& out) {
    out.emplace_back(this, node_pos, root_tid);
    const Tid subnode_ref = get_subnode(node_pos);
    if (subnode_ref != INVALID && subnode_ref < 0 && forest_ptr != nullptr
        && visited_tids.find(subnode_ref) == visited_tids.end()) {
      Tree* subtree = _get_forest_tree(subnode_ref);
      if (subtree != nullptr && subtree->_contains_data(subtree->get_root())) {
        visited_tids.insert(subnode_ref);
        subtree->pre_order_flat_impl(subtree->get_root(), root_tid, visited_tids, out);
      }
    } else {
      for (Tree_pos child = get_first_child(node_pos); child != INVALID; child = get_sibling_next(child)) {
        pre_order_flat_impl(child, root_tid, visited_tids, out);
      }
    }
  }

  void post_order_flat_impl(Tree_pos node_pos, Tid root_tid, std::set<Tid>& visited_tids, std::vector<Node_class>& out) {
    const Tid subnode_ref = get_subnode(node_pos);
    if (subnode_ref != INVALID && subnode_ref < 0 && forest_ptr != nullptr
        && visited_tids.find(subnode_ref) == visited_tids.end()) {
      Tree* subtree = _get_forest_tree(subnode_ref);
      if (subtree != nullptr && subtree->_contains_data(subtree->get_root())) {
        visited_tids.insert(subnode_ref);
        subtree->post_order_flat_impl(subtree->get_root(), root_tid, visited_tids, out);
      }
    } else {
      for (Tree_pos child = get_first_child(node_pos); child != INVALID; child = get_sibling_next(child)) {
        post_order_flat_impl(child, root_tid, visited_tids, out);
      }
    }
    out.emplace_back(this, node_pos, root_tid);
  }

  void pre_order_hier_impl(Tree_pos node_pos, Tid root_tid, std::shared_ptr<Tree> hier_tree,
                           std::shared_ptr<std::vector<Tid>> hier_tids, Tree_pos hier_pos, std::set<Tid>& active_tids,
                           std::vector<Node_class>& out) {
    out.emplace_back(this, node_pos, root_tid, hier_tree, hier_tids, hier_pos);
    const Tid subnode_ref = get_subnode(node_pos);
    if (subnode_ref != INVALID && subnode_ref < 0 && forest_ptr != nullptr && active_tids.find(subnode_ref) == active_tids.end()) {
      Tree* subtree = _get_forest_tree(subnode_ref);
      if (subtree != nullptr && subtree->_contains_data(subtree->get_root())) {
        const Tree_pos child_hier_pos = hier_tree->add_child(hier_pos);
        if (static_cast<size_t>(child_hier_pos + 1) > hier_tids->size()) {
          hier_tids->resize(static_cast<size_t>(child_hier_pos + 1), INVALID);
        }
        (*hier_tids)[static_cast<size_t>(child_hier_pos)] = subnode_ref;
        active_tids.insert(subnode_ref);
        subtree->pre_order_hier_impl(subtree->get_root(), root_tid, hier_tree, hier_tids, child_hier_pos, active_tids, out);
        active_tids.erase(subnode_ref);
      }
    } else {
      for (Tree_pos child = get_first_child(node_pos); child != INVALID; child = get_sibling_next(child)) {
        pre_order_hier_impl(child, root_tid, hier_tree, hier_tids, hier_pos, active_tids, out);
      }
    }
  }

  void post_order_hier_impl(Tree_pos node_pos, Tid root_tid, std::shared_ptr<Tree> hier_tree,
                            std::shared_ptr<std::vector<Tid>> hier_tids, Tree_pos hier_pos, std::set<Tid>& active_tids,
                            std::vector<Node_class>& out) {
    const Tid subnode_ref = get_subnode(node_pos);
    if (subnode_ref != INVALID && subnode_ref < 0 && forest_ptr != nullptr && active_tids.find(subnode_ref) == active_tids.end()) {
      Tree* subtree = _get_forest_tree(subnode_ref);
      if (subtree != nullptr && subtree->_contains_data(subtree->get_root())) {
        const Tree_pos child_hier_pos = hier_tree->add_child(hier_pos);
        if (static_cast<size_t>(child_hier_pos + 1) > hier_tids->size()) {
          hier_tids->resize(static_cast<size_t>(child_hier_pos + 1), INVALID);
        }
        (*hier_tids)[static_cast<size_t>(child_hier_pos)] = subnode_ref;
        active_tids.insert(subnode_ref);
        subtree->post_order_hier_impl(subtree->get_root(), root_tid, hier_tree, hier_tids, child_hier_pos, active_tids, out);
        active_tids.erase(subnode_ref);
      }
    } else {
      for (Tree_pos child = get_first_child(node_pos); child != INVALID; child = get_sibling_next(child)) {
        post_order_hier_impl(child, root_tid, hier_tree, hier_tids, hier_pos, active_tids, out);
      }
    }
    out.emplace_back(this, node_pos, root_tid, hier_tree, hier_tids, hier_pos);
  }

  explicit Tree(Forest* forest = nullptr) : forest_ptr(forest) { register_attr_tag<attrs::name_t>("hhds::attrs::name"); }
};

class TreeIO : public std::enable_shared_from_this<TreeIO> {
private:
  std::weak_ptr<Forest> forest_owner_;
  Tid                   tid_;
  std::string           name_;

  TreeIO(std::weak_ptr<Forest> forest_owner, Tid tid, std::string name)
      : forest_owner_(std::move(forest_owner)), tid_(tid), name_(std::move(name)) {}

public:
  TreeIO(const TreeIO&)            = delete;
  TreeIO& operator=(const TreeIO&) = delete;
  TreeIO(TreeIO&&)                 = delete;
  TreeIO& operator=(TreeIO&&)      = delete;

  [[nodiscard]] Tid                         get_tid() const noexcept { return tid_; }
  [[nodiscard]] std::string_view            get_name() const noexcept { return name_; }
  [[nodiscard]] std::shared_ptr<Forest>     get_forest() const { return forest_owner_.lock(); }
  [[nodiscard]] std::shared_ptr<Tree>       get_tree();
  [[nodiscard]] std::shared_ptr<const Tree> get_tree() const;
  [[nodiscard]] std::shared_ptr<Tree>       create_tree();
  [[nodiscard]] bool                        has_tree() const;
  void                                      clear();

private:
  void invalidate_from_forest() noexcept {
    forest_owner_.reset();
    name_.clear();
  }

  friend class Forest;
};

inline auto Tree::Node_class::add_child() const -> Node_class {
  I(tree_ptr != nullptr, "add_child: node is not attached to a tree");
  return tree_ptr->as_class(tree_ptr->add_child(current_pos));
}

inline Tree::Node_class Tree::Node_class::append_sibling() const {
  I(tree_ptr != nullptr, "append_sibling: node is not attached to a tree");
  return tree_ptr->as_class(tree_ptr->append_sibling(current_pos));
}

inline Tree::Node_class Tree::Node_class::insert_next_sibling() const {
  I(tree_ptr != nullptr, "insert_next_sibling: node is not attached to a tree");
  return tree_ptr->as_class(tree_ptr->insert_next_sibling(current_pos));
}

inline void Tree::Node_class::set_subnode(const std::shared_ptr<TreeIO>& treeio) const {
  I(tree_ptr != nullptr, "set_subnode: node is not attached to a tree");
  I(treeio != nullptr, "set_subnode: null TreeIO");
  tree_ptr->set_subnode(current_pos, treeio->get_tid());
}

inline void Tree::Node_class::set_type(Type type) const {
  I(tree_ptr != nullptr, "set_type: node is not attached to a tree");
  tree_ptr->set_type(current_pos, type);
}

inline Type Tree::Node_class::get_type() const {
  I(tree_ptr != nullptr, "get_type: node is not attached to a tree");
  return tree_ptr->get_type(current_pos);
}

inline void Tree::Node_class::del_node() const {
  I(tree_ptr != nullptr, "del_node: node is not attached to a tree");
  tree_ptr->delete_subtree(current_pos);
}

inline Tree::Node_class Tree::Node_class::parent() const {
  I(tree_ptr != nullptr, "parent: node is not attached to a tree");
  return tree_ptr->as_class(tree_ptr->get_parent(current_pos));
}

inline Tree::Node_class Tree::Node_class::first_child() const {
  I(tree_ptr != nullptr, "first_child: node is not attached to a tree");
  return tree_ptr->as_class(tree_ptr->get_first_child(current_pos));
}

inline Tree::Node_class Tree::Node_class::last_child() const {
  I(tree_ptr != nullptr, "last_child: node is not attached to a tree");
  return tree_ptr->as_class(tree_ptr->get_last_child(current_pos));
}

inline Tree::Node_class Tree::Node_class::next_sibling() const {
  I(tree_ptr != nullptr, "next_sibling: node is not attached to a tree");
  return tree_ptr->as_class(tree_ptr->get_sibling_next(current_pos));
}

inline Tree::Node_class Tree::Node_class::prev_sibling() const {
  I(tree_ptr != nullptr, "prev_sibling: node is not attached to a tree");
  return tree_ptr->as_class(tree_ptr->get_sibling_prev(current_pos));
}

inline bool Tree::Node_class::is_leaf() const {
  I(tree_ptr != nullptr, "is_leaf: node is not attached to a tree");
  return tree_ptr->is_leaf(current_pos);
}

inline bool Tree::Node_class::is_first_child() const {
  I(tree_ptr != nullptr, "is_first_child: node is not attached to a tree");
  return tree_ptr->is_first_child(current_pos);
}

inline bool Tree::Node_class::is_last_child() const {
  I(tree_ptr != nullptr, "is_last_child: node is not attached to a tree");
  return tree_ptr->is_last_child(current_pos);
}

inline bool Tree::Node_class::is_subnode() const {
  I(tree_ptr != nullptr, "is_subnode: node is not attached to a tree");
  return tree_ptr->has_subnode(current_pos);
}

inline Tid Tree::Node_class::get_subnode_tid() const {
  I(tree_ptr != nullptr, "get_subnode_tid: node is not attached to a tree");
  return tree_ptr->get_subnode(current_pos);
}

inline Tree* Tree::Node_class::get_subnode() const {
  I(tree_ptr != nullptr, "get_subnode: node is not attached to a tree");
  const auto tid = tree_ptr->get_subnode(current_pos);
  if (tid == INVALID || tid >= 0 || tree_ptr->forest_ptr == nullptr) {
    return nullptr;
  }
  return tree_ptr->_get_forest_tree(tid);
}

inline auto Tree::Node_class::pre_order_class() const {
  I(tree_ptr != nullptr, "pre_order_class: node is not attached to a tree");
  return tree_ptr->pre_order(*this);
}

inline auto Tree::Node_class::post_order_class() const {
  I(tree_ptr != nullptr, "post_order_class: node is not attached to a tree");
  return tree_ptr->post_order(*this);
}

inline auto Tree::Node_class::sibling_order() const {
  I(tree_ptr != nullptr, "sibling_order: node is not attached to a tree");
  return tree_ptr->sibling_order(*this);
}

inline auto Tree::Node_class::pre_order_flat() const {
  I(tree_ptr != nullptr, "pre_order_flat: node is not attached to a tree");
  return tree_ptr->pre_order_flat(*this);
}

inline auto Tree::Node_class::pre_order_hier() const {
  I(tree_ptr != nullptr, "pre_order_hier: node is not attached to a tree");
  return tree_ptr->pre_order_hier(*this);
}

inline auto Tree::Node_class::post_order_flat() const {
  I(tree_ptr != nullptr, "post_order_flat: node is not attached to a tree");
  return tree_ptr->post_order_flat(*this);
}

inline auto Tree::Node_class::post_order_hier() const {
  I(tree_ptr != nullptr, "post_order_hier: node is not attached to a tree");
  return tree_ptr->post_order_hier(*this);
}

class Forest : public std::enable_shared_from_this<Forest> {
private:
  std::vector<std::shared_ptr<TreeIO>> tree_ios_;
  std::vector<std::shared_ptr<Tree>>   trees;
  std::vector<size_t>                  reference_counts;
  std::unordered_map<std::string, Tid> tree_name_to_tid_;
  std::unordered_map<std::string, Tid> deleted_name_to_tid_;

public:
  [[nodiscard]] static std::shared_ptr<Forest> create() { return std::shared_ptr<Forest>(new Forest()); }

  Forest(const Forest&)            = delete;
  Forest& operator=(const Forest&) = delete;
  Forest(Forest&&)                 = delete;
  Forest& operator=(Forest&&)      = delete;

  [[nodiscard]] std::shared_ptr<TreeIO> create_io(std::string_view name) {
    I(!name.empty(), "create_io: name is required");
    const auto it = deleted_name_to_tid_.find(std::string(name));
    if (it != deleted_name_to_tid_.end()) {
      const auto reused_tid = it->second;
      deleted_name_to_tid_.erase(it);
      return create_io_impl(reused_tid, name);
    }
    return create_io_impl(-static_cast<Tree_pos>(tree_ios_.size() + 1), name);
  }

  [[nodiscard]] std::shared_ptr<TreeIO> find_io(std::string_view name) {
    if (name.empty()) {
      return nullptr;
    }

    const auto it = tree_name_to_tid_.find(std::string(name));
    if (it == tree_name_to_tid_.end()) {
      return nullptr;
    }

    const auto tree_idx = static_cast<size_t>(-it->second - 1);
    if (tree_idx >= tree_ios_.size()) {
      return nullptr;
    }

    return tree_ios_[tree_idx];
  }

  [[nodiscard]] std::shared_ptr<const TreeIO> find_io(std::string_view name) const {
    if (name.empty()) {
      return nullptr;
    }

    const auto it = tree_name_to_tid_.find(std::string(name));
    if (it == tree_name_to_tid_.end()) {
      return nullptr;
    }

    const auto tree_idx = static_cast<size_t>(-it->second - 1);
    if (tree_idx >= tree_ios_.size()) {
      return nullptr;
    }

    return tree_ios_[tree_idx];
  }

  Tree& get_tree(Tree_pos tree_tid) {
    I(tree_tid < 0, "Invalid tree reference - must be negative");
    return *get_tree_ptr(tree_tid);
  }

  [[nodiscard]] std::shared_ptr<Tree> get_tree_ptr(Tree_pos tree_tid) {
    I(tree_tid < 0, "Invalid tree reference - must be negative");
    const auto tree_idx = static_cast<size_t>(-tree_tid - 1);
    I(tree_idx < trees.size(), "Tree index out of range");
    I(trees[tree_idx], "Attempting to access deleted tree");
    return trees[tree_idx];
  }

  [[nodiscard]] std::shared_ptr<const Tree> get_tree_ptr(Tree_pos tree_tid) const {
    I(tree_tid < 0, "Invalid tree reference - must be negative");
    const auto tree_idx = static_cast<size_t>(-tree_tid - 1);
    I(tree_idx < trees.size(), "Tree index out of range");
    I(trees[tree_idx], "Attempting to access deleted tree");
    return trees[tree_idx];
  }

  [[nodiscard]] std::shared_ptr<Tree> find_tree(std::string_view name) {
    auto tio = find_io(name);
    if (!tio) {
      return nullptr;
    }
    return tio->get_tree();
  }

  [[nodiscard]] std::shared_ptr<const Tree> find_tree(std::string_view name) const {
    auto tio = find_io(name);
    if (!tio) {
      return nullptr;
    }
    return tio->get_tree();
  }

  // Forest-wide reverse lookup for an opaque Tree_flat_index key. Returns a
  // Flat-context Node_class attached to the tree body named by the index's
  // tid. Returns an empty Node_class if the tid is deleted or the pos is out
  // of range. See Tree::get_node(Tree_class_index) for the per-body variant.
  [[nodiscard]] Tree::Node_class get_node(Tree_flat_index idx) {
    const auto tid      = idx.tid;
    const auto tree_idx = static_cast<size_t>(-tid - 1);
    if (tid >= 0 || tree_idx >= trees.size() || !trees[tree_idx]) {
      return Tree::Node_class();
    }
    auto& tree = *trees[tree_idx];
    if (!tree._check_idx_exists(idx.value) || !tree._contains_data(idx.value)) {
      return Tree::Node_class();
    }
    return Tree::Node_class(&tree, idx.value, tid);
  }

  void add_reference(Tree_pos tree_tid) {
    const auto tree_idx = static_cast<size_t>(-tree_tid - 1);
    reference_counts[tree_idx]++;
  }

  void remove_reference(Tree_pos tree_tid) {
    const auto tree_idx = static_cast<size_t>(-tree_tid - 1);
    I(reference_counts[tree_idx] > 0, "Reference count already zero");
    reference_counts[tree_idx]--;
  }

  bool delete_tree(Tree_pos tree_tid) {
    const auto tree_idx = static_cast<size_t>(-tree_tid - 1);
    I(tree_idx < trees.size(), "Tree index out of range");

    if (reference_counts[tree_idx] > 0) {
      return false;
    }

    if (tree_idx < tree_ios_.size() && tree_ios_[tree_idx]) {
      const auto name = std::string(tree_ios_[tree_idx]->get_name());
      auto       it   = tree_name_to_tid_.find(name);
      if (it != tree_name_to_tid_.end() && it->second == tree_tid) {
        tree_name_to_tid_.erase(it);
      }
      if (!name.empty()) {
        deleted_name_to_tid_[name] = tree_tid;
      }
      tree_ios_[tree_idx]->invalidate_from_forest();
      tree_ios_[tree_idx].reset();
    }

    if (trees[tree_idx]) {
      trees[tree_idx].reset();
      return true;
    }
    return false;
  }

  void delete_treeio(const std::shared_ptr<TreeIO>& tio) {
    I(tio != nullptr, "delete_treeio: null TreeIO");
    const auto tree_tid = tio->get_tid();
    const auto tree_idx = static_cast<size_t>(-tree_tid - 1);
    I(tree_idx < tree_ios_.size(), "delete_treeio: TreeIO index out of range");

    if (tree_idx < tree_ios_.size() && tree_ios_[tree_idx]) {
      const auto name = std::string(tree_ios_[tree_idx]->get_name());
      auto       it   = tree_name_to_tid_.find(name);
      if (it != tree_name_to_tid_.end() && it->second == tree_tid) {
        tree_name_to_tid_.erase(it);
      }
      if (!name.empty()) {
        deleted_name_to_tid_[name] = tree_tid;
      }
      tree_ios_[tree_idx]->invalidate_from_forest();
      tree_ios_[tree_idx].reset();
    }
    if (tree_idx < trees.size()) {
      if (trees[tree_idx]) {
        trees[tree_idx]->clear();
      }
      trees[tree_idx].reset();
    }
    if (tree_idx < reference_counts.size()) {
      reference_counts[tree_idx] = 0;
    }
  }

  void delete_treeio(std::string_view name) {
    auto tio = find_io(name);
    if (!tio) {
      return;
    }
    delete_treeio(tio);
  }

  [[nodiscard]] ForestCursor create_cursor(Tid tree_tid, Tree_pos start = ROOT);

  // Persistence — saves all declarations (text) and bodies (binary).
  void save(const std::string& db_path) const;
  void load(const std::string& db_path);

private:
  [[nodiscard]] std::shared_ptr<TreeIO> create_io_impl(Tid tree_tid, std::string_view name) {
    I(tree_tid < 0, "create_tree: tree id must be negative");
    I(!name.empty(), "create_io: name is required");
    assert_name_available(name);

    const auto tree_idx = static_cast<size_t>(-tree_tid - 1);
    if (tree_idx < tree_ios_.size()) {
      I(!tree_ios_[tree_idx], "create_io: explicit id already exists or is reserved");
    } else {
      tree_ios_.resize(tree_idx + 1);
      trees.resize(tree_idx + 1);
      reference_counts.resize(tree_idx + 1, 0);
    }

    auto tio            = std::shared_ptr<TreeIO>(new TreeIO(this->weak_from_this(), tree_tid, std::string(name)));
    tree_ios_[tree_idx] = tio;
    tree_name_to_tid_.emplace(std::string(name), tree_tid);
    reference_counts[tree_idx] = 0;
    return tio;
  }

  [[nodiscard]] std::shared_ptr<Tree> create_tree_body(const std::shared_ptr<TreeIO>& tio) {
    I(tio != nullptr, "create_tree_body: null TreeIO");

    const auto tree_idx = static_cast<size_t>(-tio->get_tid() - 1);
    I(tree_idx < tree_ios_.size(), "create_tree_body: tree declaration index out of range");
    I(tree_ios_[tree_idx] == tio, "create_tree_body: TreeIO is not owned by this forest");

    if (trees[tree_idx]) {
      return trees[tree_idx];
    }

    auto tree = Tree::create(this);
    tree->bind_forest_owner(this->weak_from_this());
    tree->bind_treeio_owner(tio, tio->get_tid(), tio->get_name());

    trees[tree_idx] = tree;
    return tree;
  }

  void assert_name_available(std::string_view name, Tid self_tid = INVALID) const {
    if (name.empty()) {
      return;
    }

    const auto it = tree_name_to_tid_.find(std::string(name));
    if (it == tree_name_to_tid_.end() || it->second == self_tid) {
      return;
    }

    I(false, "create_tree: tree name already exists");
  }

  Forest() = default;
  friend class Tree;
  friend class TreeIO;
};

inline std::shared_ptr<Tree> TreeIO::get_tree() {
  auto forest = forest_owner_.lock();
  if (!forest) {
    return nullptr;
  }
  const auto tree_idx = static_cast<size_t>(-tid_ - 1);
  if (tree_idx >= forest->trees.size()) {
    return nullptr;
  }
  return forest->trees[tree_idx];
}

inline std::shared_ptr<const Tree> TreeIO::get_tree() const {
  auto forest = forest_owner_.lock();
  if (!forest) {
    return nullptr;
  }
  const auto tree_idx = static_cast<size_t>(-tid_ - 1);
  if (tree_idx >= forest->trees.size()) {
    return nullptr;
  }
  return forest->trees[tree_idx];
}

inline std::shared_ptr<Tree> TreeIO::create_tree() {
  auto forest = forest_owner_.lock();
  I(forest != nullptr, "create_tree: TreeIO is no longer attached to a forest");
  return forest->create_tree_body(shared_from_this());
}

inline bool TreeIO::has_tree() const {
  auto forest = forest_owner_.lock();
  if (!forest) {
    return false;
  }
  const auto tree_idx = static_cast<size_t>(-tid_ - 1);
  return tree_idx < forest->trees.size() && forest->trees[tree_idx] != nullptr;
}

inline void TreeIO::clear() {
  auto forest = forest_owner_.lock();
  I(forest != nullptr, "clear: TreeIO is no longer attached to a forest");
  forest->delete_treeio(shared_from_this());
}

inline void Tree::set_name(std::string_view n) {
  auto tio = treeio_owner_.lock();
  if (tio) {
    I(name_ == n, "set_name: tree names are immutable once attached to a TreeIO");
    return;
  }
  name_ = std::string(n);
}

class TreeCursor {
private:
  std::shared_ptr<Tree> tree_;
  Tree_pos              root_pos_    = INVALID;
  Tree_pos              current_pos_ = INVALID;
  size_t                depth_       = 0;

public:
  TreeCursor() = default;
  TreeCursor(std::shared_ptr<Tree> tree, Tree_pos start) : tree_(std::move(tree)), root_pos_(start), current_pos_(start) {}

  [[nodiscard]] bool goto_parent() {
    if (!tree_ || current_pos_ == root_pos_) {
      return false;
    }
    const auto parent_pos = tree_->get_parent(current_pos_);
    if (parent_pos == INVALID) {
      return false;
    }
    current_pos_ = parent_pos;
    --depth_;
    return true;
  }

  [[nodiscard]] bool goto_first_child() {
    if (!tree_) {
      return false;
    }
    const auto child_pos = tree_->get_first_child(current_pos_);
    if (child_pos == INVALID) {
      return false;
    }
    current_pos_ = child_pos;
    ++depth_;
    return true;
  }

  [[nodiscard]] bool goto_last_child() {
    if (!tree_) {
      return false;
    }
    const auto child_pos = tree_->get_last_child(current_pos_);
    if (child_pos == INVALID) {
      return false;
    }
    current_pos_ = child_pos;
    ++depth_;
    return true;
  }

  [[nodiscard]] bool goto_next_sibling() {
    if (!tree_ || current_pos_ == root_pos_) {
      return false;
    }
    const auto sibling_pos = tree_->get_sibling_next(current_pos_);
    if (sibling_pos == INVALID) {
      return false;
    }
    current_pos_ = sibling_pos;
    return true;
  }

  [[nodiscard]] bool goto_prev_sibling() {
    if (!tree_ || current_pos_ == root_pos_) {
      return false;
    }
    const auto sibling_pos = tree_->get_sibling_prev(current_pos_);
    if (sibling_pos == INVALID) {
      return false;
    }
    current_pos_ = sibling_pos;
    return true;
  }

  [[nodiscard]] Tree_class_index get_current_index() const noexcept { return Tree_class_index{current_pos_}; }
  [[nodiscard]] Tree_class_index get_root_index() const noexcept { return Tree_class_index{root_pos_}; }
  [[nodiscard]] Tree::Node_class get_current_node() const { return tree_ ? tree_->as_class(current_pos_) : Tree::Node_class(); }
  [[nodiscard]] Tree::Node_class get_root_node() const { return tree_ ? tree_->as_class(root_pos_) : Tree::Node_class(); }
  [[nodiscard]] bool             is_root() const noexcept { return current_pos_ == root_pos_; }
  [[nodiscard]] bool             is_leaf() const { return tree_ && tree_->is_leaf(current_pos_); }
  [[nodiscard]] int              depth() const noexcept { return static_cast<int>(depth_); }
};

class ForestCursor {
private:
  struct ReturnFrame {
    Tid                   parent_tid = INVALID;
    std::shared_ptr<Tree> parent_tree;
    Tree_pos              caller_pos   = INVALID;
    size_t                caller_depth = 0;
  };

  std::shared_ptr<Forest>  forest_;
  std::shared_ptr<Tree>    current_tree_;
  Tid                      root_tid_    = INVALID;
  Tree_pos                 root_pos_    = INVALID;
  Tid                      current_tid_ = INVALID;
  Tree_pos                 current_pos_ = INVALID;
  size_t                   depth_       = 0;
  std::vector<ReturnFrame> return_stack_;

public:
  ForestCursor() = default;
  ForestCursor(std::shared_ptr<Forest> forest, std::shared_ptr<Tree> tree, Tid root_tid, Tree_pos root_pos)
      : forest_(std::move(forest))
      , current_tree_(std::move(tree))
      , root_tid_(root_tid)
      , root_pos_(root_pos)
      , current_tid_(root_tid)
      , current_pos_(root_pos) {}

  [[nodiscard]] bool goto_parent() {
    if (!current_tree_) {
      return false;
    }
    if (current_pos_ == current_tree_->get_root() && !return_stack_.empty()) {
      const auto frame = return_stack_.back();
      return_stack_.pop_back();
      current_tree_ = frame.parent_tree;
      current_tid_  = frame.parent_tid;
      current_pos_  = frame.caller_pos;
      depth_        = frame.caller_depth;
      return true;
    }
    if (current_tid_ == root_tid_ && current_pos_ == root_pos_) {
      return false;
    }
    const auto parent_pos = current_tree_->get_parent(current_pos_);
    if (parent_pos == INVALID) {
      return false;
    }
    current_pos_ = parent_pos;
    --depth_;
    return true;
  }

  [[nodiscard]] bool goto_first_child() {
    if (!current_tree_) {
      return false;
    }
    const auto subnode_tid = current_tree_->get_subnode(current_pos_);
    if (subnode_tid != INVALID) {
      auto subtree = forest_->get_tree_ptr(subnode_tid);
      return_stack_.push_back(ReturnFrame{current_tid_, current_tree_, current_pos_, depth_});
      current_tree_ = std::move(subtree);
      current_tid_  = subnode_tid;
      current_pos_  = current_tree_->get_root();
      ++depth_;
      return true;
    }

    const auto child_pos = current_tree_->get_first_child(current_pos_);
    if (child_pos == INVALID) {
      return false;
    }
    current_pos_ = child_pos;
    ++depth_;
    return true;
  }

  [[nodiscard]] bool goto_last_child() {
    if (!current_tree_) {
      return false;
    }
    const auto subnode_tid = current_tree_->get_subnode(current_pos_);
    if (subnode_tid != INVALID) {
      auto subtree = forest_->get_tree_ptr(subnode_tid);
      return_stack_.push_back(ReturnFrame{current_tid_, current_tree_, current_pos_, depth_});
      current_tree_ = std::move(subtree);
      current_tid_  = subnode_tid;
      current_pos_  = current_tree_->get_root();
      ++depth_;
      return true;
    }

    const auto child_pos = current_tree_->get_last_child(current_pos_);
    if (child_pos == INVALID) {
      return false;
    }
    current_pos_ = child_pos;
    ++depth_;
    return true;
  }

  [[nodiscard]] bool goto_next_sibling() {
    if (!current_tree_ || current_pos_ == current_tree_->get_root()) {
      return false;
    }
    const auto sibling_pos = current_tree_->get_sibling_next(current_pos_);
    if (sibling_pos == INVALID) {
      return false;
    }
    current_pos_ = sibling_pos;
    return true;
  }

  [[nodiscard]] bool goto_prev_sibling() {
    if (!current_tree_ || current_pos_ == current_tree_->get_root()) {
      return false;
    }
    const auto sibling_pos = current_tree_->get_sibling_prev(current_pos_);
    if (sibling_pos == INVALID) {
      return false;
    }
    current_pos_ = sibling_pos;
    return true;
  }

  [[nodiscard]] Tid              get_current_tid() const noexcept { return current_tid_; }
  [[nodiscard]] Tid              get_root_tid() const noexcept { return root_tid_; }
  [[nodiscard]] Tree_flat_index  get_current_index() const noexcept { return Tree_flat_index{current_tid_, current_pos_}; }
  [[nodiscard]] Tree_flat_index  get_root_index() const noexcept { return Tree_flat_index{root_tid_, root_pos_}; }
  [[nodiscard]] Tree::Node_class get_current_node() const {
    return current_tree_ ? Tree::Node_class(current_tree_.get(), current_pos_, current_tid_) : Tree::Node_class();
  }
  [[nodiscard]] Tree::Node_class get_root_node() const {
    if (!forest_) {
      return Tree::Node_class();
    }
    auto root_tree = forest_->get_tree_ptr(root_tid_);
    return root_tree ? Tree::Node_class(root_tree.get(), root_pos_, root_tid_) : Tree::Node_class();
  }
  [[nodiscard]] bool is_root() const noexcept {
    return current_tid_ == root_tid_ && current_pos_ == root_pos_ && return_stack_.empty();
  }
  [[nodiscard]] bool is_leaf() const {
    if (!current_tree_) {
      return false;
    }
    if (current_tree_->get_subnode(current_pos_) != INVALID) {
      return false;
    }
    return current_tree_->is_leaf(current_pos_);
  }
  [[nodiscard]] int depth() const noexcept { return static_cast<int>(depth_); }
};

inline Tree* Tree::_get_forest_tree(Tree_pos subtree_tid) {
  I(forest_ptr != nullptr, "Tree is not attached to a forest");
  return &forest_ptr->get_tree(subtree_tid);
}

inline TreeCursor Tree::create_cursor(Tree_pos start) {
  I(_check_idx_exists(start), "create_cursor: Node index out of range");
  I(_contains_data(start), "create_cursor: Node does not exist");
  auto self = this->weak_from_this().lock();
  I(static_cast<bool>(self), "create_cursor: Tree must be managed by std::shared_ptr");
  return TreeCursor(std::move(self), start);
}

inline TreeCursor Tree::create_cursor(Node_class start) { return create_cursor(start.get_debug_nid()); }
inline TreeCursor Tree::create_cursor() { return create_cursor(static_cast<Tree_pos>(ROOT)); }

inline ForestCursor Forest::create_cursor(Tid tree_tid, Tree_pos start) {
  auto tree = get_tree_ptr(tree_tid);
  I(tree->_check_idx_exists(start), "create_cursor: Node index out of range");
  I(tree->_contains_data(start), "create_cursor: Node does not exist");
  auto self = this->weak_from_this().lock();
  I(static_cast<bool>(self), "create_cursor: Forest must be managed by std::shared_ptr");
  return ForestCursor(std::move(self), std::move(tree), tree_tid, start);
}

// ---------------------------------- STRUCTURAL TREE IMPLEMENTATION ---------------------------------- //

inline Tree_pos Tree::get_last_child(const Tree_pos& parent_pos) const {
  I(_check_idx_exists(parent_pos), "get_last_child: Parent index out of range");

  const auto chunk_id     = (parent_pos >> CHUNK_SHIFT);
  const auto chunk_offset = (parent_pos & CHUNK_MASK);
  return pointers_stack[chunk_id].get_last_child_at(static_cast<int16_t>(chunk_offset));
}

inline Tree_pos Tree::get_first_child(const Tree_pos& parent_pos) const {
  I(_check_idx_exists(parent_pos), "get_first_child: Parent index out of range");

  const auto chunk_id     = (parent_pos >> CHUNK_SHIFT);
  const auto chunk_offset = (parent_pos & CHUNK_MASK);
  return pointers_stack[chunk_id].get_first_child_at(static_cast<int16_t>(chunk_offset));
}

inline bool Tree::is_last_child(const Tree_pos& node_pos) const {
  I(_check_idx_exists(node_pos), "is_last_child: Index out of range");

  const auto node_chunk_id     = (node_pos >> CHUNK_SHIFT);
  const auto node_chunk_offset = (node_pos & CHUNK_MASK);

  if (pointers_stack[node_chunk_id].get_next_sibling() != INVALID) {
    return false;
  }
  return pointers_stack[node_chunk_id].get_num_short_del_occ() == node_chunk_offset;
}

inline bool Tree::is_first_child(const Tree_pos& node_pos) const {
  I(_check_idx_exists(node_pos), "is_first_child: Index out of range");

  const auto node_chunk_id     = (node_pos >> CHUNK_SHIFT);
  const auto node_chunk_offset = (node_pos & CHUNK_MASK);

  if (pointers_stack[node_chunk_id].get_prev_sibling() != INVALID) {
    return false;
  }

  return !node_chunk_offset;
}

inline Tree_pos Tree::get_sibling_next(const Tree_pos& sibling_id) const {
  I(_check_idx_exists(sibling_id), "get_sibling_next: Sibling index out of range");

  const auto curr_chunk_id     = (sibling_id >> CHUNK_SHIFT);
  const auto curr_chunk_offset = (sibling_id & CHUNK_MASK);
  const auto last_occupied     = pointers_stack[curr_chunk_id].get_num_short_del_occ();

  for (auto offset = curr_chunk_offset + 1; offset <= last_occupied; ++offset) {
    const auto candidate = static_cast<Tree_pos>((curr_chunk_id << CHUNK_SHIFT) + offset);
    if (_contains_data(candidate)) {
      return candidate;
    }
  }

  const auto next_chunk = pointers_stack[curr_chunk_id].get_next_sibling();
  return (next_chunk != INVALID) ? static_cast<Tree_pos>(next_chunk << CHUNK_SHIFT) : INVALID;
}

inline Tree_pos Tree::get_sibling_prev(const Tree_pos& sibling_id) const {
  I(_check_idx_exists(sibling_id), "get_sibling_prev: Sibling index out of range");

  const auto curr_chunk_id     = (sibling_id >> CHUNK_SHIFT);
  const auto curr_chunk_offset = (sibling_id & CHUNK_MASK);

  for (auto offset = curr_chunk_offset; offset > 0; --offset) {
    const auto candidate = static_cast<Tree_pos>((curr_chunk_id << CHUNK_SHIFT) + offset - 1);
    if (_contains_data(candidate)) {
      return candidate;
    }
  }

  const auto prev_chunk = pointers_stack[curr_chunk_id].get_prev_sibling();
  return (prev_chunk != INVALID)
             ? static_cast<Tree_pos>((prev_chunk << CHUNK_SHIFT) + pointers_stack[prev_chunk].get_num_short_del_occ())
             : INVALID;
}

inline Tree_pos Tree::append_sibling(const Tree_pos& sibling_id) {
  I(_check_idx_exists(sibling_id), "append_sibling: Sibling index out of range");

  const auto parent_pos       = pointers_stack[sibling_id >> CHUNK_SHIFT].get_parent();
  const auto last_sibling_pos = get_last_child(parent_pos);
  return _append_after_last_sibling(last_sibling_pos, parent_pos);
}

inline Tree_pos Tree::insert_next_sibling(const Tree_pos& sibling_id) {
  I(_check_idx_exists(sibling_id), "insert_next_sibling: Sibling index out of range");

  if (is_last_child(sibling_id)) {
    return append_sibling(sibling_id);
  }

  auto new_sib = sibling_id;
  if ((new_sib & CHUNK_MASK) != CHUNK_MASK && !_contains_data(new_sib + 1)) {
    new_sib++;
    _set_data_valid(new_sib);
  } else {
    new_sib = _insert_chunk_after(new_sib >> CHUNK_SHIFT) << CHUNK_SHIFT;
  }

  return new_sib;
}

inline Tree_pos Tree::add_root() {
  I(pointers_stack.empty(), "add_root: Tree is not empty");
  dirty_ = true;

  pointers_stack.emplace_back();
  const auto root_pos = _create_space() << CHUNK_SHIFT;
  pointers_stack[ROOT >> CHUNK_SHIFT].set_num_short_del_occ(0);

  return root_pos;
}

inline Tree_pos Tree::add_child(const Tree_pos& parent_index) {
  I(_check_idx_exists(parent_index), "add_child: Parent index out of range");
  dirty_ = true;

  const auto last_child_id = get_last_child(parent_index);
  if (last_child_id != INVALID) {
    return _append_after_last_sibling(last_child_id, parent_index);
  }

  const auto child_chunk_id = _create_space();
  const auto new_child_id   = child_chunk_id << CHUNK_SHIFT;
  const auto new_parent_id  = _try_fit_child_ptr(parent_index, new_child_id);
  pointers_stack[child_chunk_id].set_parent(new_parent_id);
  pointers_stack[child_chunk_id].set_num_short_del_occ(0);

  const auto parent_chunk_id = parent_index >> CHUNK_SHIFT;
  const auto parent_offset   = static_cast<int16_t>(parent_index & CHUNK_MASK);
  pointers_stack[parent_chunk_id].set_is_leaf(false);
  pointers_stack[parent_chunk_id].set_last_child_at(parent_offset, new_child_id);

  return new_child_id;
}

inline void Tree::delete_leaf(const Tree_pos& leaf_index) {
  I(_check_idx_exists(leaf_index), "delete_leaf: Leaf index out of range");
  I(_contains_data(leaf_index), "delete_leaf: Leaf does not exist");
  I(get_first_child(leaf_index) == INVALID, "delete_leaf: Index is not a leaf");

  const auto subnode_ref = get_subnode(leaf_index);
  if (subnode_ref != INVALID && forest_ptr) {
    forest_ptr->remove_reference(subnode_ref);
  }

  const auto leaf_chunk_id     = leaf_index >> CHUNK_SHIFT;
  const auto leaf_chunk_offset = leaf_index & CHUNK_MASK;
  const auto prev_sibling_id   = get_sibling_prev(leaf_index);
  const auto next_sibling_id   = get_sibling_next(leaf_index);
  const auto parent_index      = pointers_stack[leaf_chunk_id].get_parent();

  erase_attr_object(make_node_attr_key(static_cast<uint64_t>(leaf_index)));
  _set_data_invalid(leaf_index);
  subs_cache_valid_ = false;
  if (leaf_index < static_cast<Tree_pos>(subnode_refs.size())) {
    subnode_refs[leaf_index] = INVALID;
  }
  pointers_stack[leaf_chunk_id].set_first_child_at(static_cast<int16_t>(leaf_chunk_offset), INVALID);
  pointers_stack[leaf_chunk_id].set_last_child_at(static_cast<int16_t>(leaf_chunk_offset), INVALID);
  pointers_stack[leaf_chunk_id].set_type_at(static_cast<int16_t>(leaf_chunk_offset), 0);

  if (parent_index != INVALID) {
    const auto parent_chunk  = parent_index >> CHUNK_SHIFT;
    const auto parent_offset = static_cast<int16_t>(parent_index & CHUNK_MASK);
    auto&      parent_meta   = pointers_stack[parent_chunk];
    if (parent_meta.get_first_child_at(parent_offset) == leaf_index) {
      parent_meta.set_first_child_at(parent_offset, next_sibling_id != INVALID ? next_sibling_id : prev_sibling_id);
    }
    if (parent_meta.get_last_child_at(parent_offset) == leaf_index) {
      parent_meta.set_last_child_at(parent_offset, prev_sibling_id != INVALID ? prev_sibling_id : next_sibling_id);
    }
    if (parent_meta.get_first_child_at(parent_offset) == INVALID) {
      parent_meta.set_is_leaf(true);
    }
  }

  bool       chunk_has_data = false;
  const auto last_occupied  = pointers_stack[leaf_chunk_id].get_num_short_del_occ();
  for (Tree_pos offset = 0; offset <= last_occupied; ++offset) {
    if (_contains_data((leaf_chunk_id << CHUNK_SHIFT) + offset)) {
      chunk_has_data = true;
      break;
    }
  }

  if (!chunk_has_data) {
    const auto prev_sib_chunk = pointers_stack[leaf_chunk_id].get_prev_sibling();
    const auto next_sib_chunk = pointers_stack[leaf_chunk_id].get_next_sibling();
    if (prev_sib_chunk != INVALID) {
      pointers_stack[prev_sib_chunk].set_next_sibling(next_sib_chunk);
    }
    if (next_sib_chunk != INVALID) {
      pointers_stack[next_sib_chunk].set_prev_sibling(prev_sib_chunk);
    }
  }
}

inline void Tree::delete_subtree(const Tree_pos& subtree_root) {
  I(_check_idx_exists(subtree_root), "delete_subtree: Subtree root index out of range");
  I(_contains_data(subtree_root), "delete_subtree: Subtree root does not exist");
  dirty_            = true;
  subs_cache_valid_ = false;

  std::vector<Tree_pos> nodes_to_delete;
  std::queue<Tree_pos>  q;
  q.push(subtree_root);

  while (!q.empty()) {
    Tree_pos node = q.front();
    q.pop();
    nodes_to_delete.push_back(node);

    for (auto child = get_first_child(node); child != INVALID; child = get_sibling_next(child)) {
      q.push(child);
    }
  }

  // delete_leaf handles subnode-reference decrement per node; no separate pass here.
  for (auto it = nodes_to_delete.rbegin(); it != nodes_to_delete.rend(); ++it) {
    if (_contains_data(*it) && get_first_child(*it) == INVALID) {
      delete_leaf(*it);
    }
  }
}

inline void Tree::clear() {
  dirty_ = true;
  ++generation_;
  if (forest_ptr != nullptr) {
    for (Tree_pos node_pos = ROOT; node_pos < static_cast<Tree_pos>(pointers_stack.size() << CHUNK_SHIFT); ++node_pos) {
      if (_contains_data(node_pos)) {
        const auto subnode_ref = get_subnode(node_pos);
        if (subnode_ref != INVALID) {
          forest_ptr->remove_reference(subnode_ref);
        }
      }
    }
  }
  pointers_stack.clear();
  validity_stack.clear();
  subnode_refs.clear();
  discard_attr_stores();
  subs_cache_.clear();
  subs_cache_valid_ = false;
}

inline void Tree::set_subnode(const Tree_pos& node_pos, Tree_pos subnode_ref) {
  dirty_ = true;
  I(subnode_ref < 0, "Subnode reference must be negative");
  I(_check_idx_exists(node_pos), "set_subnode: Node index out of range");
  _ensure_subnode_ref_capacity(node_pos);
  subnode_refs[node_pos] = subnode_ref;
  subs_cache_valid_      = false;
  if (forest_ptr) {
    forest_ptr->add_reference(subnode_ref);
  }
}

}  // namespace hhds

namespace std {

template <>
struct hash<hhds::Tree_class_index> {
  [[nodiscard]] size_t operator()(const hhds::Tree_class_index& x) const noexcept { return std::hash<hhds::Tree_pos>{}(x.value); }
};

template <>
struct hash<hhds::Tree_flat_index> {
  [[nodiscard]] size_t operator()(const hhds::Tree_flat_index& x) const noexcept {
    const size_t h1 = std::hash<hhds::Tid>{}(x.tid);
    const size_t h2 = std::hash<hhds::Tree_pos>{}(x.value);
    return h1 ^ (h2 + 0x9e3779b97f4a7c15ULL + (h1 << 6U) + (h1 >> 2U));
  }
};

template <>
struct hash<hhds::Tree_hier_index> {
  [[nodiscard]] size_t operator()(const hhds::Tree_hier_index& x) const noexcept {
    const size_t h1 = std::hash<hhds::Tree_pos>{}(x.hier_pos);
    const size_t h2 = std::hash<hhds::Tree_pos>{}(x.value);
    return h1 ^ (h2 + 0x9e3779b97f4a7c15ULL + (h1 << 6U) + (h1 >> 2U));
  }
};

}  // namespace std
