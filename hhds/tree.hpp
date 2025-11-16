// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// tree.hpp
#include <sys/stat.h>
#include <sys/types.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <iostream>
#include <iterator>
#include <memory>
#include <ostream>
#include <queue>
#include <set>
#include <stdexcept>
#include <vector>
#include <bitset>

#include "iassert.hpp"

#ifdef TESTING
  #undef I                    
  #define I(cond, msg) \
      do { if (!(cond)) throw std::runtime_error(msg); } while (0)
#endif

// Logging control
#define ENABLE_LOGGING          0
#define ENABLE_CREATION_LOGGING 0

// Logging macros
#if ENABLE_CREATION_LOGGING
#define LOG_CREAT_DEBUG(...)                            \
  do {                                                  \
    std::printf("[DEBUG] %s:%d - ", __FILE__, __LINE__); \
    std::printf(__VA_ARGS__);                           \
    std::printf("\n");                                  \
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

static constexpr int16_t CHUNK_SHIFT   = 3;                 // The number of bits in a chunk offset
static constexpr int16_t CHUNK_SIZE    = 1 << CHUNK_SHIFT;  // Size of a chunk in bits
static constexpr int16_t CHUNK_MASK    = CHUNK_SIZE - 1;    // Mask for chunk offset
static constexpr Tree_pos INVALID = 0;                 // This is invalid for all pointers other than parent
static constexpr Tree_pos ROOT    = 1 << CHUNK_SHIFT;  // ROOT ID

// Now using full 64-bit addressing - much simpler!
static constexpr uint64_t MAX_TREE_SIZE = UINT64_MAX;  // Maximum number of chunks in the tree

template <typename X>
class Forest;

class __attribute__((aligned(64))) Tree_pointers {  // NOLINT(readability-magic-numbers)
private:
  // Full 64-bit pointers - no more bit packing
  Tree_pos parent;
  Tree_pos next_sibling;
  Tree_pos prev_sibling;

  // Direct child pointers per slot within the chunk
  std::array<Tree_pos, CHUNK_SIZE> first_child_ptrs;
  std::array<Tree_pos, CHUNK_SIZE> last_child_ptrs;

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
      , num_short_del_occ(0)
      , is_leaf(true) {
    first_child_ptrs.fill(INVALID);
    last_child_ptrs.fill(INVALID);
  }

  /* PARAM CONSTRUCTOR */
  Tree_pointers(Tree_pos p)
      : parent(p)
      , next_sibling(INVALID)
      , prev_sibling(INVALID)
      , first_child_ptrs{}
      , last_child_ptrs{}
      , num_short_del_occ(0)
      , is_leaf(true) {
    first_child_ptrs.fill(INVALID);
    last_child_ptrs.fill(INVALID);
  }

  // Getters
  [[nodiscard]] Tree_pos get_parent() const { return parent; }
  [[nodiscard]] Tree_pos get_next_sibling() const { return next_sibling; }
  [[nodiscard]] Tree_pos get_prev_sibling() const { return prev_sibling; }
  [[nodiscard]] Tree_pos get_first_child_at(int16_t index) const { return first_child_ptrs[static_cast<size_t>(index)]; }
  [[nodiscard]] Tree_pos get_last_child_at(int16_t index) const { return last_child_ptrs[static_cast<size_t>(index)]; }

  // Getter for num_occ
  [[nodiscard]] uint16_t get_num_short_del_occ() const { return num_short_del_occ; }

  // getter for is_leaf
  [[nodiscard]] bool get_is_leaf() const { return is_leaf; }

  // Operators
  constexpr bool operator==(const Tree_pointers& other) const {
    return parent == other.parent && next_sibling == other.next_sibling && prev_sibling == other.prev_sibling
           && first_child_ptrs == other.first_child_ptrs && last_child_ptrs == other.last_child_ptrs && num_short_del_occ == other.num_short_del_occ && is_leaf == other.is_leaf;
  }

  constexpr bool operator!=(const Tree_pointers& other) const { return !(*this == other); }
  void           invalidate() { parent = INVALID; }

  // Add setters
  void set_parent(Tree_pos p) { parent = p; }
  void set_next_sibling(Tree_pos n) { next_sibling = n; }
  void set_prev_sibling(Tree_pos p) { prev_sibling = p; }
  void set_first_child_at(int16_t index, Tree_pos value) { first_child_ptrs[static_cast<size_t>(index)] = value; }
  void set_last_child_at(int16_t index, Tree_pos value) { last_child_ptrs[static_cast<size_t>(index)] = value; }
  void set_num_short_del_occ(uint16_t n) { num_short_del_occ = n; }
  void set_is_leaf(bool l) { this->is_leaf = l; }

  // Helper methods for subtree references
  [[nodiscard]] bool     has_subtree_ref() const { return parent < 0; }
  [[nodiscard]] Tree_pos get_subtree_ref() const { return parent; }
  void                   set_subtree_ref(Tree_pos ref) { parent = ref; }
};  // Tree_pointers class

template <typename X>
class Forest;

template <typename X>
class tree {
private:
  /* The tree pointers and data stored separately */
  std::vector<Tree_pointers>    pointers_stack;
  std::vector<X>                data_stack;
  std::vector<std::bitset<64>>  validity_stack;
  Forest<X>*                    forest_ptr;

  /* Special functions for sanity */
  [[nodiscard]] inline bool _check_idx_exists(const Tree_pos& idx) const noexcept {
    // idx >= 0 not needed for unsigned int
    return idx < static_cast<Tree_pos>(pointers_stack.size() << CHUNK_SHIFT);
  }

  [[nodiscard]] inline bool _contains_data(const Tree_pos& idx) const noexcept {
    const auto bitset_idx = idx >> 6;  // Divide by 64 (bits per bitset)
    const auto bit_pos = idx & 63;     // Modulo 64
    return bitset_idx < static_cast<Tree_pos>(validity_stack.size()) && validity_stack[bitset_idx][bit_pos];
  }

  inline void _set_data_valid(const Tree_pos& idx) noexcept {
    const auto bitset_idx = idx >> 6;
    const auto bit_pos = idx & 63;
    if (bitset_idx >= static_cast<Tree_pos>(validity_stack.size())) {
      validity_stack.resize(bitset_idx + 1);
    }
    validity_stack[bitset_idx][bit_pos] = true;
  }

  inline void _set_data_invalid(const Tree_pos& idx) noexcept {
    const auto bitset_idx = idx >> 6;
    const auto bit_pos = idx & 63;
    if (bitset_idx < static_cast<Tree_pos>(validity_stack.size())) {
      validity_stack[bitset_idx][bit_pos] = false;
    }
  }

  // SIMD-friendly bulk validity check for a range
  [[nodiscard]] inline bool _has_any_valid_in_range(Tree_pos start, Tree_pos end) const noexcept {
    const auto start_bitset = start >> 6;
    const auto end_bitset = end >> 6;

    if (start_bitset >= static_cast<Tree_pos>(validity_stack.size())) return false;

    // Single bitset case
    if (start_bitset == end_bitset) {
      const auto start_bit = start & 63;
      const auto end_bit = end & 63;
      const auto mask = ((1ULL << (end_bit - start_bit + 1)) - 1) << start_bit;
      return (validity_stack[start_bitset].to_ullong() & mask) != 0;
    }

    // Multi-bitset case - check first partial, middle full, last partial
    const auto start_bit = start & 63;
    const auto end_bit = end & 63;

    // Check first partial bitset
    const auto first_mask = ~((1ULL << start_bit) - 1);
    if ((validity_stack[start_bitset].to_ullong() & first_mask) != 0) return true;

    // Check middle full bitsets
    for (auto i = start_bitset + 1; i < end_bitset && i < static_cast<Tree_pos>(validity_stack.size()); ++i) {
      if (validity_stack[i].any()) return true;
    }

    // Check last partial bitset
    if (end_bitset < static_cast<Tree_pos>(validity_stack.size())) {
      const auto last_mask = (1ULL << (end_bit + 1)) - 1;
      if ((validity_stack[end_bitset].to_ullong() & last_mask) != 0) return true;
    }

    return false;
  }

  // SIMD-friendly function to find the next valid index in a range
  [[nodiscard]] inline Tree_pos _find_next_valid_in_range(Tree_pos start, Tree_pos end) const noexcept {
    const auto start_bitset = start >> 6;
    const auto end_bitset = end >> 6;

    if (start_bitset >= static_cast<Tree_pos>(validity_stack.size())) return INVALID;

    // Single bitset case
    if (start_bitset == end_bitset) {
      const auto start_bit = start & 63;
      const auto end_bit = std::min(static_cast<Tree_pos>(end & 63), static_cast<Tree_pos>(63));

      auto bits = validity_stack[start_bitset].to_ullong();
      bits &= ~((1ULL << start_bit) - 1);  // Clear bits before start
      bits &= (1ULL << (end_bit + 1)) - 1; // Clear bits after end

      if (bits != 0) {
        return static_cast<Tree_pos>((start_bitset << 6) + __builtin_ctzll(bits));
      }
      return INVALID;
    }

    // Multi-bitset case
    const auto start_bit = start & 63;

    // Check first partial bitset
    auto bits = validity_stack[start_bitset].to_ullong();
    bits &= ~((1ULL << start_bit) - 1);  // Clear bits before start
    if (bits != 0) {
      return static_cast<Tree_pos>((start_bitset << 6) + __builtin_ctzll(bits));
    }

    // Check middle full bitsets
    for (auto i = start_bitset + 1; i < end_bitset && i < static_cast<Tree_pos>(validity_stack.size()); ++i) {
      bits = validity_stack[i].to_ullong();
      if (bits != 0) {
        return static_cast<Tree_pos>((i << 6) + __builtin_ctzll(bits));
      }
    }

    // Check last partial bitset
    if (end_bitset < static_cast<Tree_pos>(validity_stack.size())) {
      const auto end_bit = end & 63;
      bits = validity_stack[end_bitset].to_ullong();
      bits &= (1ULL << (end_bit + 1)) - 1;  // Clear bits after end
      if (bits != 0) {
        return static_cast<Tree_pos>((end_bitset << 6) + __builtin_ctzll(bits));
      }
    }

    return INVALID;
  }

  // Bulk validation check for chunk boundaries - returns count of valid entries
  [[nodiscard]] inline size_t _count_valid_in_chunk(Tree_pos chunk_start) const noexcept {
    const auto bitset_start = chunk_start >> 6;
    const auto bit_start = chunk_start & 63;

    // Handle case where chunk spans bitset boundary
    if ((bit_start + CHUNK_SIZE) > 64) {
      size_t count = 0;
      for (int i = 0; i < CHUNK_SIZE; ++i) {
        if (_contains_data(chunk_start + i)) count++;
      }
      return count;
    }

    // Fast path: entire chunk within single bitset
    if (bitset_start < static_cast<Tree_pos>(validity_stack.size())) {
      const auto mask = ((1ULL << CHUNK_SIZE) - 1) << bit_start;
      const auto masked_bits = validity_stack[bitset_start].to_ullong() & mask;
      return __builtin_popcountll(masked_bits);
    }

    return 0;
  }

  /* Function to add an entry to the pointers and data stack (typically for add/append)*/
  Tree_pos _create_space(const X& data) {
    // Make space for CHUNK_SIZE number of entries at the end
    const auto start_pos = data_stack.size();
    data_stack.emplace_back(data);
    data_stack.resize(data_stack.size() + CHUNK_MASK);

    // Mark the first entry as valid, others as invalid
    _set_data_valid(start_pos);

    // Add the single pointer node for all CHUNK_SIZE entries
    pointers_stack.emplace_back();
    return pointers_stack.size() - 1;
  }

  /* Function to insert a new chunk in between (typically for handling add/append corner cases)*/
  Tree_pos _insert_chunk_after(const Tree_pos& curr) {
    // Allot new chunk at the end
    const auto new_chunk_id = _create_space(X());

    // Update bookkeeping -> This is basically inserting inside of a doubly linked list
    pointers_stack[new_chunk_id].set_prev_sibling(curr);
    pointers_stack[new_chunk_id].set_next_sibling(pointers_stack[curr].get_next_sibling());
    pointers_stack[curr].set_next_sibling(new_chunk_id);

    if (pointers_stack[new_chunk_id].get_next_sibling() != INVALID) {
      pointers_stack[pointers_stack[new_chunk_id].get_next_sibling()].set_prev_sibling(new_chunk_id);
    }

    pointers_stack[new_chunk_id].set_parent(pointers_stack[curr].get_parent());

    return new_chunk_id;
  }

  /* Helper function to update the parent pointer of all sibling chunks*/
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
  // :private

public:
  /**
   *  Query based API (no updates)
   */
  [[nodiscard]] inline Tree_pos get_parent(const Tree_pos& curr_index) const {
    return pointers_stack[curr_index >> CHUNK_SHIFT].get_parent();
  }
  [[nodiscard]] Tree_pos get_last_child(const Tree_pos& parent_index) const;
  [[nodiscard]] Tree_pos get_first_child(const Tree_pos& parent_index) const;
  [[nodiscard]] bool     is_last_child(const Tree_pos& self_index) const;
  [[nodiscard]] bool     is_first_child(const Tree_pos& self_index) const;
  [[nodiscard]] Tree_pos get_sibling_next(const Tree_pos& sibling_id) const;
  [[nodiscard]] Tree_pos get_sibling_prev(const Tree_pos& sibling_id) const;
  [[nodiscard]] inline bool     is_leaf(const Tree_pos& leaf_index) const {
    return pointers_stack[leaf_index >> CHUNK_SHIFT].get_is_leaf();
  }
  [[nodiscard]] Tree_pos get_root() const { return ROOT; }

  /**
   *  Update based API (Adds and Deletes from the tree)
   */
  // FREQUENT UPDATES
  Tree_pos append_sibling(const Tree_pos& sibling_id, const X& data);
  Tree_pos add_child(const Tree_pos& parent_index, const X& data);
  Tree_pos add_root(const X& data);

  void delete_leaf(const Tree_pos& leaf_index);
  void delete_subtree(const Tree_pos& subtree_root);
  void add_subtree_ref(const Tree_pos& node_pos, Tree_pos subtree_ref);

  // INFREQUENT UPDATES
  Tree_pos insert_next_sibling(const Tree_pos& sibling_id, const X& data);

  /**
   * Data access API
   */
  X& get_data(const Tree_pos& idx) {
    GI(_check_idx_exists(idx), _contains_data(idx), "Index out of range or no data at the index");

    return data_stack[idx];
  }

  const X& get_data(const Tree_pos& idx) const {
    GI(_check_idx_exists(idx), _contains_data(idx), "Index out of range or no data at the index");

    return data_stack[idx];
  }

  void set_data(const Tree_pos& idx, const X& data) {
    I(_check_idx_exists(idx), "Index out of range");

    data_stack[idx] = data;
    _set_data_valid(idx);
  }

  // Use "X operator[](const Tree_pos& idx) const { return get_data(idx); }" to pass data as a const reference
  X operator[](const Tree_pos& idx) { return data_stack[idx]; }

  /**
   *  Debug API (Temp)
   */
  void print_tree(int deep = 0) {
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

    std::cout << std::endl;

    if (deep) {
      for (size_t i = 0; i < data_stack.size(); i++) {
        if (_contains_data(i)) {
          std::cout << "Index: " << i << " Data: " << data_stack[i] << std::endl;
          std::cout << "PAR : " << get_parent(i) << std::endl;
          std::cout << "FC  : " << get_first_child(i) << std::endl;
          std::cout << "LC  : " << get_last_child(i) << std::endl;
          std::cout << "NS  : " << get_sibling_next(i) << std::endl;
          std::cout << "PS  : " << get_sibling_prev(i) << std::endl;
          std::cout << std::endl;
        }
      }
    }
  }

  explicit tree(Forest<X>* forest = nullptr) : forest_ptr(forest) {}

  /**
   * ITERATORS
   * - SIBLING-ORDER(start)
   * - POSTORDER (subtree_parent)
   * - PREORDER (subtree_parent)
   */
  template <typename Derived>
  class traversal_iterator_base {
  protected:
    Tree_pos       current;
    const tree<X>* tree_ptr;
    bool           m_follow_subtrees;

  public:
    using iterator_category = std::forward_iterator_tag;
    using value_type        = Tree_pos;
    using difference_type   = std::ptrdiff_t;
    using pointer           = Tree_pos*;
    using reference         = Tree_pos&;

    traversal_iterator_base(Tree_pos start, const tree<X>* tree, bool follow_refs)
        : current(start), tree_ptr(tree), m_follow_subtrees(follow_refs) {}

    bool     operator==(const traversal_iterator_base& other) const { return current == other.current; }
    bool     operator!=(const traversal_iterator_base& other) const { return current != other.current; }
    Tree_pos operator*() const { return current; }

  protected:
    // Helper method to handle subtree references
    Tree_pos handle_subtree_ref(Tree_pos pos) {
      if (m_follow_subtrees && tree_ptr->forest_ptr) {  // only follow if flag is true
        auto& node = tree_ptr->pointers_stack[pos >> CHUNK_SHIFT];
        if (node.has_subtree_ref()) {
          Tree_pos ref = node.get_subtree_ref();
          if (ref < 0) {
            return ROOT;
          }
        }
      }
      return INVALID;
    }
  };

  // SIBLING ORDER TRAVERSAL
  class sibling_order_iterator : public traversal_iterator_base<sibling_order_iterator> {
  private:
    using base = traversal_iterator_base<sibling_order_iterator>;
    using base::current;
    using base::m_follow_subtrees;
    using base::tree_ptr;

  public:
    using iterator_category = std::forward_iterator_tag;
    using value_type        = Tree_pos;
    using difference_type   = std::ptrdiff_t;
    using pointer           = Tree_pos*;
    using reference         = Tree_pos&;

    sibling_order_iterator(Tree_pos start, tree<X>* tree, bool follow_refs) : base(start, tree, follow_refs) {}

    sibling_order_iterator& operator++() {
      Tree_pos subtree_root = this->handle_subtree_ref(this->current);
      if (subtree_root != INVALID) {
        this->current = subtree_root;
        return *this;
      }
      if (tree_ptr->get_sibling_next(current) != INVALID) {
        current = tree_ptr->get_sibling_next(current);
      } else {
        current = INVALID;
      }
      return *this;
    }

    sibling_order_iterator operator++(int) {
      sibling_order_iterator temp = *this;
      ++(*this);
      return temp;
    }
  };

  class sibling_order_range {
  private:
    Tree_pos m_start;
    tree<X>* m_tree_ptr;
    bool     m_follow_subtrees;

  public:
    sibling_order_range(Tree_pos start, tree<X>* tree, bool follow_subtrees = false)
        : m_start(start), m_tree_ptr(tree), m_follow_subtrees(follow_subtrees) {}

    sibling_order_iterator begin() { return sibling_order_iterator(m_start, m_tree_ptr, m_follow_subtrees); }

    sibling_order_iterator end() { return sibling_order_iterator(INVALID, m_tree_ptr, m_follow_subtrees); }
  };

  sibling_order_range sibling_order(Tree_pos start, bool follow_subtrees = false) {
    return sibling_order_range(start, this, follow_subtrees);
  }

  class const_sibling_order_iterator : public traversal_iterator_base<const_sibling_order_iterator> {
  private:
    using base = traversal_iterator_base<const_sibling_order_iterator>;
    using base::current;
    using base::m_follow_subtrees;
    using base::tree_ptr;

  public:
    using iterator_category = std::forward_iterator_tag;
    using value_type        = Tree_pos;
    using difference_type   = std::ptrdiff_t;
    using pointer           = const Tree_pos*;
    using reference         = const Tree_pos&;

    const_sibling_order_iterator(Tree_pos start, const tree<X>* tree, bool follow_refs) : base(start, tree, follow_refs) {}

    const_sibling_order_iterator& operator++() {
      Tree_pos subtree_root = this->handle_subtree_ref(this->current);
      if (subtree_root != INVALID) {
        this->current = subtree_root;
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
    Tree_pos       m_start;
    const tree<X>* m_tree_ptr;
    bool           m_follow_subtrees;

  public:
    const_sibling_order_range(Tree_pos start, const tree<X>* tree, bool follow_subtrees = false)
        : m_start(start), m_tree_ptr(tree), m_follow_subtrees(follow_subtrees) {}

    const_sibling_order_iterator begin() const { return const_sibling_order_iterator(m_start, m_tree_ptr, m_follow_subtrees); }

    const_sibling_order_iterator end() const { return const_sibling_order_iterator(INVALID, m_tree_ptr, m_follow_subtrees); }
  };

  const_sibling_order_range sibling_order(Tree_pos start) const { return const_sibling_order_range(start, this); }

  // PRE-ORDER TRAVERSAL (Optimized for performance)
  class pre_order_iterator {
  private:
    Tree_pos current;
    const tree<X>* tree_ptr;

    // Fast inline helpers to avoid function call overhead
    inline Tree_pos fast_get_first_child(Tree_pos parent_index) const {
      const auto chunk_id     = (parent_index >> CHUNK_SHIFT);
      const auto chunk_offset = (parent_index & CHUNK_MASK);
      return tree_ptr->pointers_stack[chunk_id].get_first_child_at(static_cast<int16_t>(chunk_offset));
    }

    inline Tree_pos fast_get_sibling_next(Tree_pos sibling_id) const {
      // Fast path: check same chunk first
      const auto curr_chunk_id = (sibling_id >> CHUNK_SHIFT);
      const auto curr_chunk_offset = (sibling_id & CHUNK_MASK);

      // Check if next slot in same chunk has data
      if (curr_chunk_offset < CHUNK_MASK) {
        const auto next_chunk_occ = tree_ptr->pointers_stack[curr_chunk_id].get_num_short_del_occ();
        if ((curr_chunk_offset + 1) <= next_chunk_occ) {
          return (curr_chunk_id << CHUNK_SHIFT) + curr_chunk_offset + 1;
        }
      }

      // Check next sibling chunk
      const auto next_sibling_chunk = tree_ptr->pointers_stack[curr_chunk_id].get_next_sibling();
      return static_cast<Tree_pos>(next_sibling_chunk << CHUNK_SHIFT);
    }

    inline Tree_pos fast_get_parent(Tree_pos index) const {
      return tree_ptr->pointers_stack[index >> CHUNK_SHIFT].get_parent();
    }

    inline bool fast_is_leaf(Tree_pos index) const {
      return tree_ptr->pointers_stack[index >> CHUNK_SHIFT].get_is_leaf();
    }

  public:
    using iterator_category = std::forward_iterator_tag;
    using value_type = Tree_pos;
    using difference_type = std::ptrdiff_t;
    using pointer = Tree_pos*;
    using reference = Tree_pos&;

    pre_order_iterator(Tree_pos start, const tree<X>* tree, bool /* follow_subtrees */ = false)
        : current(start), tree_ptr(tree) {}

    X get_data() const { return tree_ptr->get_data(current); }

    pre_order_iterator& operator++() {
      // Optimized preorder traversal - no subtree handling for performance

      // 1. Try to go to first child
      if (!fast_is_leaf(current)) {
        auto child = fast_get_first_child(current);
        if (child != INVALID) {
          current = child;
          return *this;
        }
      }

      // 2. Try to go to next sibling
      auto sibling = fast_get_sibling_next(current);
      if (sibling != INVALID) {
        current = sibling;
        return *this;
      }

      // 3. Go up to parent and find their next sibling
      auto parent = fast_get_parent(current);
      while (parent != ROOT && parent != INVALID) {
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

    bool operator==(const pre_order_iterator& other) const {
      return current == other.current;
    }

    bool operator!=(const pre_order_iterator& other) const {
      return current != other.current;
    }

    Tree_pos operator*() const { return current; }
  };

  // Keep the full-featured iterator for subtree traversal when needed
  class pre_order_iterator_with_subtrees : public traversal_iterator_base<pre_order_iterator_with_subtrees> {
    // private:

  public:
    std::set<Tree_pos>    visited_subtrees;
    tree<X>*              current_tree;  // Track which tree we're currently traversing
    tree<X>*              main_tree;     // Keep reference to main tree (NOTE: This won't work for multiple layers of trees)
    std::vector<Tree_pos> prev_trees;
    Tree_pos              return_to_node;  // Node to return to after subtree traversal
    std::vector<Tree_pos> return_to_nodes;
    using base = traversal_iterator_base<pre_order_iterator_with_subtrees>;
    using base::current;
    using base::m_follow_subtrees;
    using iterator_category = std::forward_iterator_tag;
    using value_type        = Tree_pos;
    using difference_type   = std::ptrdiff_t;
    using pointer           = Tree_pos*;
    using reference         = Tree_pos&;

    pre_order_iterator_with_subtrees(Tree_pos start, tree<X>* tree, bool follow_refs)
        : base(start, tree, follow_refs), current_tree(tree), main_tree(tree), return_to_node(INVALID) {}

    X get_data() const { return current_tree->get_data(current); }

    pre_order_iterator_with_subtrees& operator++() {
      if (m_follow_subtrees && current_tree->forest_ptr) {
        auto& node = this->current_tree->pointers_stack[this->current >> CHUNK_SHIFT];
        if (node.has_subtree_ref()) {
          Tree_pos ref = node.get_subtree_ref();
          if (ref < 0 && visited_subtrees.find(ref) == visited_subtrees.end()) {
            visited_subtrees.insert(ref);
            return_to_node = current;
            prev_trees.push_back(ref);
            return_to_nodes.push_back(current);
            current_tree = &(current_tree->forest_ptr->get_tree(ref));
            current      = ROOT;
            return *this;
          }
        }
      }

      // first try to go to first child
      if (!current_tree->is_leaf(this->current)) {
        auto new_cur = current_tree->get_first_child(this->current);
        if (new_cur != INVALID) {
          this->current = new_cur;
          return *this;
        }
      }

      // if no children, try to go to next sibling
      auto nxt = current_tree->get_sibling_next(this->current);
      if (nxt != INVALID) {
        this->current = nxt;
        return *this;
      }

      auto parent = current_tree->get_parent(this->current);
      while (parent != ROOT && parent != INVALID) {
        if (m_follow_subtrees && parent <= 0) {
          this->current = INVALID;
          return *this;
        }
        if (!m_follow_subtrees && parent <= 0) {
          this->current = INVALID;
          return *this;
        }
        auto parent_sibling = current_tree->get_sibling_next(parent);
        if (parent_sibling != INVALID) {
          this->current = parent_sibling;
          return *this;
        }
        parent = current_tree->get_parent(parent);
      }

      // if no next sibling and we're in a subtree, return to main tree -- let's instead check if nested subtrees
      if (current_tree != main_tree) {
        Tree_pos r_node;
        if (this->prev_trees.size() <= 1) {
          this->current_tree = this->main_tree;
          r_node             = this->return_to_nodes.back();
          this->return_to_nodes.pop_back();
        } else {
          this->prev_trees.pop_back();
          this->current_tree = &(this->current_tree->forest_ptr->get_tree(this->prev_trees.back()));
          r_node             = this->return_to_nodes.back();
          this->return_to_nodes.pop_back();
        }
        if (!current_tree->is_leaf(r_node)) {
          auto new_cur = current_tree->get_first_child(r_node);
          if (new_cur != INVALID) {
            this->current        = new_cur;
            this->return_to_node = INVALID;
            return *this;
          }
        }

        this->current = current_tree->get_sibling_next(r_node);
        if (this->current != INVALID) {
          this->return_to_node = INVALID;
          return *this;
        }
      }

      current = INVALID;
      return *this;
    }

    bool operator==(const pre_order_iterator_with_subtrees& other) const {
      return current == other.current && current_tree == other.current_tree;
    }

    bool operator!=(const pre_order_iterator_with_subtrees& other) const { return !(*this == other); }

    Tree_pos operator*() const { return current; }
  };

  class pre_order_range {
  private:
    Tree_pos m_start;
    const tree<X>* m_tree_ptr;
    bool     m_follow_subtrees;

  public:
    pre_order_range(Tree_pos start, const tree<X>* tree, bool follow_subtrees = false)
        : m_start(start), m_tree_ptr(tree), m_follow_subtrees(follow_subtrees) {}

    pre_order_iterator begin() const { return pre_order_iterator(m_start, m_tree_ptr); }

    pre_order_iterator end() const { return pre_order_iterator(INVALID, m_tree_ptr); }
  };

  // Optimized default traversal (no subtree handling)
  pre_order_range pre_order(Tree_pos start = ROOT) const {
    return pre_order_range(start, this, false);
  }

  // For cases where subtree traversal is needed
  class pre_order_range_with_subtrees {
  private:
    Tree_pos m_start;
    tree<X>* m_tree_ptr;
    bool     m_follow_subtrees;

  public:
    pre_order_range_with_subtrees(Tree_pos start, tree<X>* tree, bool follow_subtrees = false)
        : m_start(start), m_tree_ptr(tree), m_follow_subtrees(follow_subtrees) {}

    pre_order_iterator_with_subtrees begin() { return pre_order_iterator_with_subtrees(m_start, m_tree_ptr, m_follow_subtrees); }

    pre_order_iterator_with_subtrees end() { return pre_order_iterator_with_subtrees(INVALID, m_tree_ptr, m_follow_subtrees); }
  };

  pre_order_range_with_subtrees pre_order_with_subtrees(Tree_pos start = ROOT, bool follow_subtrees = false) {
    return pre_order_range_with_subtrees(start, this, follow_subtrees);
  }

  class const_pre_order_iterator : public traversal_iterator_base<const_pre_order_iterator> {
  private:
    using base = traversal_iterator_base<const_pre_order_iterator>;
    using base::current;
    using base::m_follow_subtrees;
    using base::tree_ptr;

    std::set<Tree_pos> visited_subtrees;
    const tree<X>*     current_tree;
    const tree<X>*     main_tree;
    Tree_pos           return_to_node;

  public:
    using iterator_category = std::forward_iterator_tag;
    using value_type        = Tree_pos;
    using difference_type   = std::ptrdiff_t;
    using pointer           = const Tree_pos*;
    using reference         = const Tree_pos&;

    const_pre_order_iterator(Tree_pos start, const tree<X>* tree, bool follow_refs)
        : base(start, tree, follow_refs), current_tree(tree), main_tree(tree), return_to_node(INVALID) {}

    const X get_data() const { return current_tree->get_data(current); }

    const_pre_order_iterator& operator++() {
      if (m_follow_subtrees && current_tree->forest_ptr) {
        auto& node = current_tree->pointers_stack[current >> CHUNK_SHIFT];
        if (node.has_subtree_ref()) {
          Tree_pos ref = node.get_subtree_ref();
          if (ref < 0 && visited_subtrees.find(ref) == visited_subtrees.end()) {
            visited_subtrees.insert(ref);
            return_to_node = current;
            current_tree   = &(current_tree->forest_ptr->get_tree(ref));
            this->current  = ROOT;
            return *this;
          }
        }
      }

      // first try to go to first child
      if (!current_tree->is_leaf(this->current)) {
        this->current = current_tree->get_first_child(this->current);
        return *this;
      }

      // if no children, try to go to next sibling
      auto nxt = current_tree->get_sibling_next(this->current);
      if (nxt != INVALID) {
        this->current = nxt;
        return *this;
      }

      // if no next sibling and we're in a subtree, return to main tree
      if (current_tree != main_tree) {
        current_tree   = main_tree;
        current        = current_tree->get_sibling_next(return_to_node);
        return_to_node = INVALID;
        if (this->current != INVALID) {
          return *this;
        }
      }

      // if no next sibling, go up to parent's next sibling
      auto parent = current_tree->get_parent(this->current);
      while (parent != ROOT && parent != INVALID) {
        auto parent_sibling = current_tree->get_sibling_next(parent);
        if (parent_sibling != INVALID) {
          this->current = parent_sibling;
          return *this;
        }
        parent = current_tree->get_parent(parent);
      }

      // if we'e gone through all possibilities, mark as end
      this->current = INVALID;
      return *this;
    }

    bool operator==(const const_pre_order_iterator& other) const {
      return current == other.current && current_tree == other.current_tree;
    }

    bool operator!=(const const_pre_order_iterator& other) const { return !(*this == other); }

    Tree_pos operator*() const { return current; }
  };

  class const_pre_order_range {
  private:
    Tree_pos       m_start;
    const tree<X>* m_tree_ptr;
    bool           m_follow_subtrees;

  public:
    const_pre_order_range(Tree_pos start, const tree<X>* tree, bool follow_subtrees = false)
        : m_start(start), m_tree_ptr(tree), m_follow_subtrees(follow_subtrees) {}

    const_pre_order_iterator begin() const { return const_pre_order_iterator(m_start, m_tree_ptr, m_follow_subtrees); }

    const_pre_order_iterator end() const { return const_pre_order_iterator(INVALID, m_tree_ptr, m_follow_subtrees); }
  };

  const_pre_order_range const_pre_order(Tree_pos start = ROOT) const { return const_pre_order_range(start, this); }

  // POST-ORDER TRAVERSAL
  class post_order_iterator : public traversal_iterator_base<post_order_iterator> {
  private:
    using base = traversal_iterator_base<post_order_iterator>;
    using base::current;
    using base::m_follow_subtrees;
    using base::tree_ptr;

  public:
    using iterator_category = std::forward_iterator_tag;
    using value_type        = Tree_pos;
    using difference_type   = std::ptrdiff_t;
    using pointer           = Tree_pos*;
    using reference         = Tree_pos&;

    post_order_iterator(Tree_pos start, tree<X>* tree, bool follow_refs) : base(start, tree, follow_refs) {}

    X get_data() const { return tree_ptr->get_data(this->current); }

    post_order_iterator& operator++() {
      Tree_pos subtree_root = this->handle_subtree_ref(this->current);
      if (subtree_root != INVALID) {
        this->current = subtree_root;
        return *this;
      }

      if (tree_ptr->get_sibling_next(current) != INVALID) {
        auto next = tree_ptr->get_sibling_next(current);
        while (tree_ptr->get_sibling_next(next) != INVALID) {
          next = tree_ptr->get_first_child(next);
        }

        current = next;
      } else {
        current = tree_ptr->get_parent(current);
      }
      return *this;
    }

    post_order_iterator operator++(int) {
      post_order_iterator temp = *this;
      ++(*this);
      return temp;
    }
  };

  class post_order_range {
  private:
    Tree_pos m_start;
    tree<X>* m_tree_ptr;
    bool     m_follow_subtrees;

  public:
    post_order_range(Tree_pos start, tree<X>* tree, bool follow_subtrees = false)
        : m_start(start), m_tree_ptr(tree), m_follow_subtrees(follow_subtrees) {}

    post_order_iterator begin() { return post_order_iterator(m_start, m_tree_ptr, m_follow_subtrees); }

    post_order_iterator end() { return post_order_iterator(INVALID, m_tree_ptr, m_follow_subtrees); }
  };

  post_order_range post_order(Tree_pos start = ROOT, bool follow_subtrees = false) {
    return post_order_range(start, this, follow_subtrees);
  }

  class const_post_order_iterator : public traversal_iterator_base<const_post_order_iterator> {
  private:
    using base = traversal_iterator_base<const_post_order_iterator>;
    using base::current;
    using base::m_follow_subtrees;
    using base::tree_ptr;

  public:
    using iterator_category = std::forward_iterator_tag;
    using value_type        = Tree_pos;
    using difference_type   = std::ptrdiff_t;
    using pointer           = const Tree_pos*;
    using reference         = const Tree_pos&;

    const_post_order_iterator(Tree_pos start, const tree<X>* tree, bool follow_refs) : base(start, tree, follow_refs) {}

    const_post_order_iterator& operator++() {
      Tree_pos subtree_root = this->handle_subtree_ref(this->current);
      if (subtree_root != INVALID) {
        this->current = subtree_root;
        return *this;
      }

      if (tree_ptr->get_sibling_next(current) != INVALID) {
        auto next = tree_ptr->get_sibling_next(current);
        while (tree_ptr->get_sibling_next(next) != INVALID) {
          next = tree_ptr->get_first_child(next);
        }
        current = next;
      } else {
        current = tree_ptr->get_parent(current);
      }
      return *this;
    }

    bool operator==(const const_post_order_iterator& other) const { return current == other.current; }

    bool operator!=(const const_post_order_iterator& other) const { return current != other.current; }

    Tree_pos operator*() const { return current; }
  };

  class const_post_order_range {
  private:
    Tree_pos       m_start;
    const tree<X>* m_tree_ptr;
    bool           m_follow_subtrees;

  public:
    const_post_order_range(Tree_pos start, const tree<X>* tree, bool follow_subtrees = false)
        : m_start(start), m_tree_ptr(tree), m_follow_subtrees(follow_subtrees) {}

    const_post_order_iterator begin() const { return const_post_order_iterator(m_start, m_tree_ptr, m_follow_subtrees); }

    const_post_order_iterator end() const { return const_post_order_iterator(INVALID, m_tree_ptr, m_follow_subtrees); }
  };

  const_post_order_range post_order(Tree_pos start = ROOT) const { return const_post_order_range(start, this); }

  // move helper methods inside tree class
  [[nodiscard]] bool is_subtree_ref(Tree_pos pos) const { return pos < 0; }

  [[nodiscard]] size_t get_subtree_index(Tree_pos pos) const { return static_cast<size_t>(-pos - 1); }

  [[nodiscard]] Tree_pos make_subtree_ref(size_t subtree_index) const { return static_cast<Tree_pos>(-(subtree_index + 1)); }

  [[nodiscard]] Tree_pos get_subtree_ref(Tree_pos pos) const { return pointers_stack[pos >> CHUNK_SHIFT].get_subtree_ref(); }

  // :public

};  // tree class

template <typename X>
class Forest {
private:
  std::vector<std::unique_ptr<tree<X>>> trees;
  std::vector<size_t>                   reference_counts;

public:
  Tree_pos create_tree(const X& root_data) {
    trees.push_back(std::make_unique<tree<X>>(this));
    reference_counts.push_back(0);
    const auto tree_idx = trees.size() - 1;
    trees[tree_idx]->add_root(root_data);
    return -static_cast<Tree_pos>(tree_idx) - 1;
  }

  tree<X>& get_tree(Tree_pos tree_ref) {
    I(tree_ref < 0, "Invalid tree reference - must be negative");
    const auto tree_idx = static_cast<size_t>(-tree_ref - 1);
    I(tree_idx < trees.size(), "Tree index out of range");
    I(trees[tree_idx], "Attempting to access deleted tree");
    return *trees[tree_idx];
  }

  void add_reference(Tree_pos tree_ref) {
    const auto tree_idx = static_cast<size_t>(-tree_ref - 1);
    reference_counts[tree_idx]++;
  }

  void remove_reference(Tree_pos tree_ref) {
    const auto tree_idx = static_cast<size_t>(-tree_ref - 1);
    I(reference_counts[tree_idx] > 0, "Reference count already zero");
    reference_counts[tree_idx]--;
  }

  bool delete_tree(Tree_pos tree_ref) {
    const auto tree_idx = static_cast<size_t>(-tree_ref - 1);
    I(tree_idx < trees.size(), "Tree index out of range");

    if (reference_counts[tree_idx] > 0) {
      return false;
    }

    if (trees[tree_idx]) {
      trees[tree_idx].reset();
      return true;
    }
    return false;
  }
};

// ---------------------------------- TEMPLATE IMPLEMENTATION ---------------------------------- //


/**
 * @brief Get absolute ID of the last child of a node.
 *
 * @param parent_index The absolute ID of the parent node.
 * @return Tree_pos The absolute ID of the last child, INVALID if none
 *
 * @throws std::out_of_range If the parent index is out of range
 */
template <typename X>
inline Tree_pos tree<X>::get_last_child(const Tree_pos& parent_index) const {
  I(_check_idx_exists(parent_index), "get_last_child: Parent index out of range");

  const auto chunk_id     = (parent_index >> CHUNK_SHIFT);
  const auto chunk_offset = (parent_index & CHUNK_MASK);
  return pointers_stack[chunk_id].get_last_child_at(static_cast<int16_t>(chunk_offset));
}

/**
 * @brief Get absolute ID of the first child of a node.
 *
 * @param parent_index The absolute ID of the parent node.
 * @return Tree_pos The absolute ID of the first child, INVALID if none
 *
 * @throws std::out_of_range If the parent index is out of range
 */
template <typename X>
inline Tree_pos tree<X>::get_first_child(const Tree_pos& parent_index) const {
  I(_check_idx_exists(parent_index), "get_first_child: Parent index out of range");

  const auto chunk_id     = (parent_index >> CHUNK_SHIFT);
  const auto chunk_offset = (parent_index & CHUNK_MASK);
  return pointers_stack[chunk_id].get_first_child_at(static_cast<int16_t>(chunk_offset));
}

/**
 * @brief Check if a node is the last child of its parent.
 *
 * @param self_index The absolute ID of the node.
 * @return true If the node is the last child of its parent.
 *
 * @throws std::out_of_range If the query index is out of range
 */
template <typename X>
inline bool tree<X>::is_last_child(const Tree_pos& self_index) const {
  I(_check_idx_exists(self_index), "is_last_child: Index out of range");

  const auto self_chunk_id     = (self_index >> CHUNK_SHIFT);
  const auto self_chunk_offset = (self_index & CHUNK_MASK);

  // If this chunk has a next_sibling pointer, certainly not the last child
  if (pointers_stack[self_chunk_id].get_next_sibling() != INVALID) {
    return false;
  }
  return pointers_stack[self_chunk_id].get_num_short_del_occ() == self_chunk_offset;

  // Now, to be the last child, all entries after this should be invalid
  // for (int16_t offset = self_chunk_offset; offset < CHUNK_SIZE; offset++) {
  //     if (_contains_data((self_chunk_id << CHUNK_SHIFT) + offset)) {
  //         return false;
  //     }
  // }
}

/**
 * @brief Check if a node is the first child of its parent.
 *
 * @param self_index The absolute ID of the node.
 * @return true If the node is the first child of its parent.
 *
 * @throws std::out_of_range If the query index is out of range
 */
template <typename X>
inline bool tree<X>::is_first_child(const Tree_pos& self_index) const {
  I(_check_idx_exists(self_index), "is_first_child: Index out of range");

  const auto self_chunk_id     = (self_index >> CHUNK_SHIFT);
  const auto self_chunk_offset = (self_index & CHUNK_MASK);

  // If this chunk has a prev_sibling pointer, certainly not the last child
  if (pointers_stack[self_chunk_id].get_prev_sibling() != INVALID) {
    return false;
  }

  // Now, to be the first child, it must have no offset
  return !self_chunk_offset;
}

/**
 * @brief Get the next sibling of a node.
 *
 * @param sibling_id The absolute ID of the sibling node.
 * @return Tree_pos The absolute ID of the next sibling, INVALID if none
 *
 * @throws std::out_of_range If the sibling index is out of range
 */
template <typename X>
inline Tree_pos tree<X>::get_sibling_next(const Tree_pos& sibling_id) const {
  I(_check_idx_exists(sibling_id), "get_sibling_next: Sibling index out of range");

  const auto curr_chunk_id     = (sibling_id >> CHUNK_SHIFT);
  const auto curr_chunk_offset = (sibling_id & CHUNK_MASK);
  const auto last_occupied     = pointers_stack[curr_chunk_id].get_num_short_del_occ();

  // Fast path: next sibling lives in the same chunk
  if (curr_chunk_offset < last_occupied) {
    return static_cast<Tree_pos>((curr_chunk_id << CHUNK_SHIFT) + curr_chunk_offset + 1);
  }

  // Fall back to the next sibling chunk if it exists
  const auto next_chunk = pointers_stack[curr_chunk_id].get_next_sibling();
  return (next_chunk != INVALID) ? static_cast<Tree_pos>(next_chunk << CHUNK_SHIFT) : INVALID;
}

/**
 * @brief Get the prev sibling of a node.
 *
 * @param sibling_id The absolute ID of the sibling node.
 * @return Tree_pos The absolute ID of the prev sibling, INVALID if none
 *
 * @throws std::out_of_range If the sibling index is out of range
 * @todo handle when the prev or next could be within same chunk but more than 1 offset away
 *       happens if someone is deleted from the middle
 */
template <typename X>
inline Tree_pos tree<X>::get_sibling_prev(const Tree_pos& sibling_id) const {
  I(_check_idx_exists(sibling_id), "get_sibling_prev: Sibling index out of range");

  const auto curr_chunk_id     = (sibling_id >> CHUNK_SHIFT);
  const auto curr_chunk_offset = (sibling_id & CHUNK_MASK);

  // Fast path: previous sibling shares the chunk
  if (curr_chunk_offset > 0) {
    return static_cast<Tree_pos>((curr_chunk_id << CHUNK_SHIFT) + curr_chunk_offset - 1);
  }

  // Otherwise consult the previous sibling chunk
  const auto prev_chunk = pointers_stack[curr_chunk_id].get_prev_sibling();
  return (prev_chunk != INVALID)
             ? static_cast<Tree_pos>((prev_chunk << CHUNK_SHIFT) + pointers_stack[prev_chunk].get_num_short_del_occ())
             : INVALID;
}

/**
 * @brief Append a sibling to a node.
 *
 * @param sibling_id The absolute ID of the sibling node.
 * @param data The data to be stored in the new sibling.
 *
 * @return Tree_pos The absolute ID of the new sibling.
 * @throws std::out_of_range If the sibling index is out of range
 */
template <typename X>
Tree_pos tree<X>::append_sibling(const Tree_pos& sibling_id, const X& data) {
  I(_check_idx_exists(sibling_id), "append_sibling: Sibling index out of range");

  // Directly go to the last sibling of the sibling_id
  const auto parent_id = pointers_stack[sibling_id >> CHUNK_SHIFT].get_parent();
  auto       new_sib   = get_last_child(parent_id);

  // If this chunk does not have more space, just add a new chunk
  // and treat that as the last child
  if ((new_sib & CHUNK_MASK) == CHUNK_MASK) {
    // Make a new chunk after this, and put the data there
    new_sib             = _insert_chunk_after(new_sib >> CHUNK_SHIFT) << CHUNK_SHIFT;
    data_stack[new_sib] = data;
    _set_data_valid(new_sib);
  } else {
    // Just put the data in the next offset
    new_sib++;
    data_stack[new_sib] = data;
    _set_data_valid(new_sib);
  }
  // Increment the number of occupied slots in the sibling chunk
  pointers_stack[new_sib >> CHUNK_SHIFT].set_num_short_del_occ(new_sib & CHUNK_MASK);
  pointers_stack[parent_id >> CHUNK_SHIFT].set_is_leaf(false);

  // Update the parent's last_child pointer since this is now the new last child
  const auto parent_offset = static_cast<int16_t>(parent_id & CHUNK_MASK);
  pointers_stack[parent_id >> CHUNK_SHIFT].set_last_child_at(parent_offset, new_sib);

  return new_sib;
}

/**
 * @brief Insert a sibling after a node.
 *
 * @param sibling_id The absolute ID of the sibling node.
 * @param data The data to be stored in the new sibling.
 *
 * @return Tree_pos The absolute ID of the new sibling.
 * @throws std::out_of_range If the sibling index is out of range
 */
template <typename X>
Tree_pos tree<X>::insert_next_sibling(const Tree_pos& sibling_id, const X& data) {
  I(_check_idx_exists(sibling_id), "insert_next_sibling: Sibling index out of range");

  // If this is the last child, just append a sibling
  if (is_last_child(sibling_id)) {
    return append_sibling(sibling_id, data);
  }

  // Directly go to the next sibling of the sibling_id
  // TODO: const auto parent_id = pointers_stack[sibling_id >> CHUNK_SHIFT].get_parent();
  auto new_sib = sibling_id;

  // Try to fir the sibling right after this, if the chunk has some space
  if ((new_sib & CHUNK_MASK) != CHUNK_MASK && !_contains_data(new_sib + 1)) {
    new_sib++;
    data_stack[new_sib] = data;
    _set_data_valid(new_sib);
  } else {
    new_sib             = _insert_chunk_after(new_sib >> CHUNK_SHIFT) << CHUNK_SHIFT;
    data_stack[new_sib] = data;
    _set_data_valid(new_sib);
  }

  return new_sib;
}

/**
 * @brief Add a root node to the tree.
 *
 * @param data The data to be stored in the root node.
 *
 * @return Tree_pos The absolute ID of the root node.
 * @throws std::logic_error If the tree is not empty
 */
template <typename X>
Tree_pos tree<X>::add_root(const X& data) {
  I(pointers_stack.empty(), "add_root: Tree is not empty");

  // Add empty nodes to make the tree 1-indexed
  data_stack.resize(CHUNK_SIZE);
  pointers_stack.emplace_back();

  // Make space for CHUNK_SIZE number of entries at the end
  const auto root_pos = data_stack.size();
  data_stack.emplace_back(data);
  data_stack.resize(data_stack.size() + CHUNK_MASK);

  // Mark the root as valid
  _set_data_valid(root_pos);

  // Add the single pointer node for all CHUNK_SIZE entries
  pointers_stack.emplace_back();

  // set num occupied to 0
  pointers_stack[ROOT >> CHUNK_SHIFT].set_num_short_del_occ(0);

  return ROOT;
}

/**
 * @brief Add a child to a node at the end of all it's children.
 *
 * @param parent_index The absolute ID of the parent node.
 * @param data The data to be stored in the new sibling.
 *
 * @throws std::out_of_range If the parent index is out of range
 */
template <typename X>
Tree_pos tree<X>::add_child(const Tree_pos& parent_index, const X& data) {
  I(_check_idx_exists(parent_index), "add_child: Parent index out of range");
  // This is not the first child being added
  const auto last_child_id = get_last_child(parent_index);
  if (last_child_id != INVALID) {
    return append_sibling(last_child_id, data);
  } else {
  }

  // Try to fit this child pointer
  const auto child_chunk_id = _create_space(data);
  const auto new_child_id = child_chunk_id << CHUNK_SHIFT;
  const auto new_parent_id = _try_fit_child_ptr(parent_index, new_child_id);
  pointers_stack[child_chunk_id].set_parent(new_parent_id);

  // Set num occupied to 0
  pointers_stack[child_chunk_id].set_num_short_del_occ(0);

  // update is_leaf flag and set both first and last child pointers since this is the only child
  const auto parent_chunk_id = parent_index >> CHUNK_SHIFT;
  const auto parent_offset = static_cast<int16_t>(parent_index & CHUNK_MASK);
  pointers_stack[parent_chunk_id].set_is_leaf(false);
  pointers_stack[parent_chunk_id].set_last_child_at(parent_offset, new_child_id);

  return new_child_id;
}

/**
 * @brief Delete a leaf, given
 *
 * @param leaf_index The absolute ID of the leaf node.
 *
 * @return Tree_pos The absolute ID of the leaf to be deleted.
 * @throws std::out_of_range If the leaf index is out of range
 * @throws std::logic_error If the leaf index is not actually a leaf
 */
template <typename X>
void tree<X>::delete_leaf(const Tree_pos& leaf_index) {
  I(_check_idx_exists(leaf_index), "delete_leaf: Leaf index out of range");
  I(get_first_child(leaf_index) == INVALID, "delete_leaf: Index is not a leaf");

  auto& node = pointers_stack[leaf_index >> CHUNK_SHIFT];
  if (node.has_subtree_ref() && forest_ptr) {
    forest_ptr->remove_reference(node.get_subtree_ref());
  }

  const auto leaf_chunk_id     = leaf_index >> CHUNK_SHIFT;
  const auto leaf_chunk_offset = leaf_index & CHUNK_MASK;
  const auto prev_sibling_id   = get_sibling_prev(leaf_index);
  const auto next_sibling_id   = get_sibling_next(leaf_index);

  // Empty this spot in the data array
  _set_data_invalid(leaf_index);
  pointers_stack[leaf_chunk_id].set_first_child_at(static_cast<int16_t>(leaf_chunk_offset), INVALID);
  int16_t remaining_data = 0;

  // Swap this forward
  for (int16_t offset = leaf_chunk_offset; offset < CHUNK_SIZE - 1; offset++) {
    if (_contains_data((leaf_chunk_id << CHUNK_SHIFT) + offset + 1)) {
      remaining_data++;

      data_stack[(leaf_chunk_id << CHUNK_SHIFT) + offset]     = data_stack[(leaf_chunk_id << CHUNK_SHIFT) + offset + 1];
      _set_data_invalid((leaf_chunk_id << CHUNK_SHIFT) + offset + 1);

      // Move any child pointer metadata along with the node
      auto& chunk_meta = pointers_stack[leaf_chunk_id];
      chunk_meta.set_first_child_at(offset, chunk_meta.get_first_child_at(offset + 1));
      chunk_meta.set_first_child_at(offset + 1, INVALID);

      // Update the parent pointer of the moved node
      const auto moved_node = (leaf_chunk_id << CHUNK_SHIFT) + offset;
      const auto fc = get_first_child((leaf_chunk_id << CHUNK_SHIFT) + offset + 1);
      if (fc != INVALID) {
        _update_parent_pointer(fc, moved_node);
      }
    } else {
      break;
    }
  }

  // Recompute the last occupied slot for this chunk if it still has data
  int16_t new_last = -1;
  for (int16_t idx = CHUNK_SIZE - 1; idx >= 0; --idx) {
    if (_contains_data((leaf_chunk_id << CHUNK_SHIFT) + idx)) {
      new_last = idx;
      break;
    }
  }
  if (new_last >= 0) {
    pointers_stack[leaf_chunk_id].set_num_short_del_occ(static_cast<uint16_t>(new_last));
  } else {
    pointers_stack[leaf_chunk_id].set_num_short_del_occ(0);
  }

  // int16_t circuit prev and next sibling chunk if this chunk is empty
  if (remaining_data == 0) {
    const auto prev_sib_chunk = pointers_stack[leaf_chunk_id].get_prev_sibling();
    const auto next_sib_chunk = pointers_stack[leaf_chunk_id].get_next_sibling();

    if (prev_sib_chunk != INVALID) {
      pointers_stack[prev_sib_chunk].set_next_sibling(next_sib_chunk);
    }

    if (next_sib_chunk != INVALID) {
      pointers_stack[next_sib_chunk].set_prev_sibling(prev_sib_chunk);
    }

    // I will have to adjust the parent's last and first child pointers
    // If the leaf that I just deleted was a last/first child
    // Get the parent of the leaf
    const auto parent_index  = pointers_stack[leaf_chunk_id].get_parent();
    const auto parent_chunk  = parent_index >> CHUNK_SHIFT;
    const auto parent_offset = static_cast<int16_t>(parent_index & CHUNK_MASK);
    auto&      parent_meta   = pointers_stack[parent_chunk];

    if (parent_meta.get_first_child_at(parent_offset) == leaf_index) {
      Tree_pos replacement = INVALID;
      if (next_sibling_id != INVALID) {
        replacement = next_sibling_id;
      } else if (prev_sibling_id != INVALID) {
        replacement = prev_sibling_id;
      }
      parent_meta.set_first_child_at(parent_offset, replacement);
    }

    if (parent_meta.get_first_child_at(parent_offset) == INVALID) {
      parent_meta.set_is_leaf(true);
      // Clear the last_child pointer too
      parent_meta.set_last_child_at(parent_offset, INVALID);
    } else {
      // If we deleted the last child, update the parent's last_child pointer
      if (next_sibling_id == INVALID && prev_sibling_id != INVALID) {
        parent_meta.set_last_child_at(parent_offset, prev_sibling_id);
      }
    }
  }
}

/**
 * @brief Delete a subtree rooted at a node.
 *
 * @param subtree_root The absolute ID of the root of the subtree.
 *
 * @throws std::out_of_range If the subtree root index is out of range
 */
template <typename X>
void tree<X>::delete_subtree(const Tree_pos& subtree_root) {
  I(_check_idx_exists(subtree_root), "delete_subtree: Subtree root index out of range");

  // Vector to store the nodes in reverse level order
  std::vector<Tree_pos> nodes_to_delete;

  // Queue for level order traversal
  std::queue<Tree_pos> q;
  q.push(subtree_root);

  // Perform level order traversal to collect nodes
  while (!q.empty()) {
    Tree_pos node = q.front();
    q.pop();
    nodes_to_delete.push_back(node);

    for (auto child = get_first_child(node); child != INVALID; child = get_sibling_next(child)) {
      q.push(child);
    }
  }

  // Remove subtree references first
  for (auto node : nodes_to_delete) {
    auto& node_ptr = pointers_stack[node >> CHUNK_SHIFT];
    if (node_ptr.has_subtree_ref() && forest_ptr) {
      forest_ptr->remove_reference(node_ptr.get_subtree_ref());
    }
  }

  // Delete nodes in reverse order to ensure leaves are deleted first
  for (auto it = nodes_to_delete.rbegin(); it != nodes_to_delete.rend(); ++it) {
    if (is_leaf(*it)) {
      delete_leaf(*it);
    }
  }
}

template <typename X>
void tree<X>::add_subtree_ref(const Tree_pos& node_pos, Tree_pos subtree_ref) {
  I(subtree_ref < 0, "Subtree reference must be negative");
  this->pointers_stack[node_pos >> CHUNK_SHIFT].set_subtree_ref(subtree_ref);
  if (forest_ptr) {
    forest_ptr->add_reference(subtree_ref);
  }
}

}  // namespace hhds
