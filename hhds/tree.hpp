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
#include <functional>
#include <iostream>
#include <iterator>
#include <optional>
#include <queue>
#include <set>
#include <stdexcept>
#include <vector>

#include "fmt/format.h"
#include "iassert.hpp"

namespace hhds {
/** NOTES for future contributors:
 * Realize that the total number of bits for the
 * each entry of Tree_pointers is 3 + 5*CHUNK_BITS
 * + 2*7*SHORT_DELTA = 3 + 5*43 + 2*7*21 = 512 bits.
 *
 * The number of bits for each entry of Tree_pointers is
 * exactly one cache line. If at any point there is bookkeeping
 * to be added, please add it to the Tree_pointers class after
 * adjusting the values of CHUNK_BITS and SHORT_DELTA.
 *
 * Other values for reference (CHUNK_BITS, SHORT_DELTA, TOTAL_BITS)
 *  40 22 -> 511
    43 21 -> 512
    45 20 -> 508
    48 19 -> 509
 *
 * NEVER let it exceed 512 bits.
*/

using Tree_pos    = int64_t;
using Short_delta = int32_t;

static constexpr int16_t CHUNK_SHIFT   = 3;                 // The number of bits in a chunk offset
static constexpr int16_t CHUNK_SIZE    = 1 << CHUNK_SHIFT;  // Size of a chunk in bits
static constexpr int16_t CHUNK_MASK    = CHUNK_SIZE - 1;    // Mask for chunk offset
static constexpr int16_t NUM_SHORT_DEL = CHUNK_MASK;        // Number of int16_t delta children in eachc tree_ptr

static constexpr Tree_pos INVALID = 0;                 // This is invalid for all pointers other than parent
static constexpr Tree_pos ROOT    = 1 << CHUNK_SHIFT;  // ROOT ID

static constexpr int CHUNK_BITS  = 49;
static constexpr int SHORT_DELTA = 18;

static constexpr uint64_t MAX_TREE_SIZE = 1LL << CHUNK_BITS;  // Maximum number of chunks in the tree

static constexpr Short_delta MIN_SHORT_DELTA = -(1 << (SHORT_DELTA - 1));     // Minimum value for int16_t delta
static constexpr Short_delta MAX_SHORT_DELTA = (1 << (SHORT_DELTA - 1)) - 1;  // Maximum value for int16_t delta

template <typename X>
class Forest;

class __attribute__((packed, aligned(64))) Tree_pointers {  // NOLINT(readability-magic-numbers)
private:
  // We only store the exact ID of parent
  Tree_pos parent : CHUNK_BITS + CHUNK_SHIFT;
  Tree_pos next_sibling : CHUNK_BITS;
  Tree_pos prev_sibling : CHUNK_BITS;

  // Long child pointers
  Tree_pos first_child_l : CHUNK_BITS;
  Tree_pos last_child_l : CHUNK_BITS;

  // Track number of occupied int16_t delta pointers
  uint16_t num_short_del_occ : CHUNK_SHIFT;

  // leaf flag
  bool is_leaf : 1;

  // int16_t (delta) child pointers
  __int128 first_child_s;
  __int128 last_child_s;

  // Helper functions to get and set first child pointers by index
  [[nodiscard]] inline Short_delta _get_first_child_s(int16_t index) const {
    return (first_child_s >> (index * SHORT_DELTA)) & ((1 << SHORT_DELTA) - 1);
  }

  inline void _set_first_child_s(int16_t index, Short_delta value) {
    first_child_s &= ~((__int128)((static_cast<__int128>(1) << SHORT_DELTA) - 1) << (index * SHORT_DELTA));
    first_child_s |= ((__int128)value << (index * SHORT_DELTA));
  }

  // Helper functions to get and set last child pointers by index
  inline Short_delta _get_last_child_s(int16_t index) const {
    return (last_child_s >> (index * SHORT_DELTA)) & ((static_cast<__int128>(1) << SHORT_DELTA) - 1);
  }

  inline void _set_last_child_s(int16_t index, Short_delta value) {
    last_child_s &= ~((__int128)((static_cast<__int128>(1) << SHORT_DELTA) - 1) << (index * SHORT_DELTA));
    last_child_s |= ((__int128)value << (index * SHORT_DELTA));
  }

  // :private

public:
  /* DEFAULT CONSTRUCTOR */
  Tree_pointers()
      : parent(INVALID)
      , next_sibling(INVALID)
      , prev_sibling(INVALID)
      , first_child_l(INVALID)
      , last_child_l(INVALID)
      , num_short_del_occ(0)
      , is_leaf(true)
      , first_child_s(INVALID)
      , last_child_s(INVALID) {}

  /* PARAM CONSTRUCTOR */
  Tree_pointers(Tree_pos p)
      : parent(p)
      , next_sibling(INVALID)
      , prev_sibling(INVALID)
      , first_child_l(INVALID)
      , last_child_l(INVALID)
      , num_short_del_occ(0)
      , is_leaf(true)
      , first_child_s(INVALID)
      , last_child_s(INVALID) {}

  // Getters
  [[nodiscard]] Tree_pos get_parent() const { return parent; }
  [[nodiscard]] Tree_pos get_next_sibling() const { return next_sibling; }
  [[nodiscard]] Tree_pos get_prev_sibling() const { return prev_sibling; }
  [[nodiscard]] Tree_pos get_first_child_l() const { return first_child_l; }
  [[nodiscard]] Tree_pos get_last_child_l() const { return last_child_l; }

  // Getters for int16_t child pointers
  [[nodiscard]] Short_delta get_first_child_s_at(int16_t index) const { return _get_first_child_s(index); }
  [[nodiscard]] Short_delta get_last_child_s_at(int16_t index) const { return _get_last_child_s(index); }

  // Getter for num_occ
  [[nodiscard]] uint16_t get_num_short_del_occ() const { return num_short_del_occ; }

  // getter for is_leaf
  [[nodiscard]] bool get_is_leaf() const { return is_leaf; }

  // Operators
  constexpr bool operator==(const Tree_pointers& other) const {
    return parent == other.parent && next_sibling == other.next_sibling && prev_sibling == other.prev_sibling
           && first_child_l == other.first_child_l && last_child_l == other.last_child_l && first_child_s == other.first_child_s
           && last_child_s == other.last_child_s && is_leaf == other.is_leaf;
  }

  constexpr bool operator!=(const Tree_pointers& other) const { return !(*this == other); }
  void           invalidate() { parent = INVALID; }

  // Add setters
  void set_parent(Tree_pos p) { parent = p; }
  void set_next_sibling(Tree_pos n) { next_sibling = n; }
  void set_prev_sibling(Tree_pos p) { prev_sibling = p; }
  void set_first_child_l(Tree_pos f) { first_child_l = f; }
  void set_last_child_l(Tree_pos l) { last_child_l = l; }
  void set_num_short_del_occ(uint16_t n) { num_short_del_occ = n; }
  void set_is_leaf(bool l) { is_leaf = l; }
  void set_first_child_s_at(int16_t index, Short_delta value) { _set_first_child_s(index, value); }
  void set_last_child_s_at(int16_t index, Short_delta value) { _set_last_child_s(index, value); }

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
  std::vector<std::optional<X>> data_stack;
  Forest<X>*                    forest_ptr;

  /* Special functions for sanity */
  [[nodiscard]] inline bool _check_idx_exists(const Tree_pos& idx) const noexcept {
    // idx >= 0 not needed for unsigned int
    return idx < static_cast<Tree_pos>(pointers_stack.size() << CHUNK_SHIFT);
  }

  [[nodiscard]] inline bool _contains_data(const Tree_pos& idx) const noexcept {
    return (pointers_stack[idx >> CHUNK_SHIFT].get_num_short_del_occ() >= (idx & CHUNK_MASK));
    // return (idx < data_stack.size() && data_stack[idx].has_value());
  }

  /* Function to add an entry to the pointers and data stack (typically for add/append)*/
  Tree_pos _create_space(const X& data) {
    // Make space for CHUNK_SIZE number of entries at the end
    data_stack.emplace_back(data);
    data_stack.resize(data_stack.size() + CHUNK_MASK);

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

  /* Helper function to check if we can fit something in the int16_t delta*/
  inline bool _fits_in_short_del(const Tree_pos& parent_chunk_id, const Tree_pos& child_chunk_id) {
    const int64_t delta = child_chunk_id - parent_chunk_id;
    return (delta >= MIN_SHORT_DELTA) && (delta <= MAX_SHORT_DELTA);
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

    /* BASE CASE OF THE RECURSION */
    // If parent has long ptr access, this is easy
    if ((parent_id & CHUNK_MASK) == 0) {
      pointers_stack[parent_id >> CHUNK_SHIFT].set_last_child_l(child_id >> CHUNK_SHIFT);
      if (pointers_stack[parent_id >> CHUNK_SHIFT].get_first_child_l() == INVALID) {
        pointers_stack[parent_id >> CHUNK_SHIFT].set_first_child_l(child_id >> CHUNK_SHIFT);
      }

      // update is_leaf flag
      pointers_stack[parent_id >> CHUNK_SHIFT].set_is_leaf(false);

      return parent_id;
    }

    // Now, try to fit the child in the int16_t delta
    const auto parent_chunk_id     = (parent_id >> CHUNK_SHIFT);
    const auto parent_chunk_offset = (parent_id & CHUNK_MASK);
    if (_fits_in_short_del(parent_chunk_id, child_id >> CHUNK_SHIFT)) {
      // Adjust the child pointers
      pointers_stack[parent_chunk_id].set_last_child_s_at(parent_chunk_offset - 1, (child_id >> CHUNK_SHIFT) - parent_chunk_id);

      if (pointers_stack[parent_chunk_id].get_first_child_s_at(parent_chunk_offset - 1) == INVALID) {
        pointers_stack[parent_chunk_id].set_first_child_s_at(parent_chunk_offset - 1, (child_id >> CHUNK_SHIFT) - parent_chunk_id);
      }

      // update is_leaf flag
      pointers_stack[parent_chunk_id].set_is_leaf(false);

      return parent_id;
    }

    /* RECURSION */
    const auto            grandparent_id = pointers_stack[parent_chunk_id].get_parent();
    std::vector<Tree_pos> new_chunks;

    // Break the chunk fully -> Every node in the chunk is moved to a separate chunk
    for (int16_t offset = parent_chunk_offset - 1; offset < NUM_SHORT_DEL; offset++) {
      if (_contains_data((parent_chunk_id << CHUNK_SHIFT) + offset + 1)) {
        const auto curr_id = (parent_chunk_id << CHUNK_SHIFT) + offset + 1;

        // Create a new chunk, put this one over there
        const auto new_chunk_id = _insert_chunk_after(new_chunks.empty() ? parent_chunk_id : new_chunks.back());

        // Store the new chunk id for updates later
        new_chunks.push_back(new_chunk_id);

        // Remove data from old, and put it here
        data_stack[new_chunk_id << CHUNK_SHIFT] = data_stack[curr_id];
        data_stack[curr_id]                     = std::nullopt;

        // Convert the int16_t pointers here to long pointers there
        const auto fc = pointers_stack[parent_chunk_id].get_first_child_s_at(offset);
        const auto lc = pointers_stack[parent_chunk_id].get_last_child_s_at(offset);
        if (fc != INVALID) {
          pointers_stack[new_chunk_id].set_first_child_l(fc + parent_chunk_id);
          pointers_stack[new_chunk_id].set_last_child_l(lc + parent_chunk_id);
          pointers_stack[new_chunk_id].set_is_leaf(false);
        }

        // Update the parent pointer of all children of this guy
        // THIS LOOKS LIKE A BOTTLENECK -> Will iterate over all ~ (children / 8) chunks
        if (fc != INVALID) {
          _update_parent_pointer((fc + parent_chunk_id) << CHUNK_SHIFT, new_chunk_id << CHUNK_SHIFT);
        }

        // Remove the int16_t pointers in the old chunk
        pointers_stack[parent_chunk_id].set_first_child_s_at(offset, INVALID);
        pointers_stack[parent_chunk_id].set_last_child_s_at(offset, INVALID);
      }
    }
    // Decrement the number of occupied slots in the chunk
    pointers_stack[parent_chunk_id].set_num_short_del_occ(parent_chunk_offset - 1);

    // Try fitting the last chunk here in the grandparent. Recurse.
    const auto my_new_parent = _try_fit_child_ptr(grandparent_id, new_chunks.back() << CHUNK_SHIFT);

    // Update the parent pointer of the new chunks
    if (my_new_parent != grandparent_id) {
      for (const auto& new_chunk : new_chunks) {
        pointers_stack[new_chunk].set_parent(my_new_parent);
      }
    }

    // update is_leaf flag
    pointers_stack[parent_chunk_id].set_is_leaf(false);

    return new_chunks.front() << CHUNK_SHIFT;  // The first one was where the parent was sent
  }
  // :private

public:
  /**
   *  Query based API (no updates)
   */
  [[nodiscard]] Tree_pos get_parent(const Tree_pos& curr_index) const;
  [[nodiscard]] Tree_pos get_last_child(const Tree_pos& parent_index) const;
  [[nodiscard]] Tree_pos get_first_child(const Tree_pos& parent_index) const;
  [[nodiscard]] bool     is_last_child(const Tree_pos& self_index) const;
  [[nodiscard]] bool     is_first_child(const Tree_pos& self_index) const;
  [[nodiscard]] Tree_pos get_sibling_next(const Tree_pos& sibling_id) const;
  [[nodiscard]] Tree_pos get_sibling_prev(const Tree_pos& sibling_id) const;
  [[nodiscard]] bool     is_leaf(const Tree_pos& leaf_index) const;
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
    GI(_check_idx_exists(idx), data_stack[idx].has_value(), "Index out of range or no data at the index");

    return *data_stack[idx];
  }

  const X& get_data(const Tree_pos& idx) const {
    GI(_check_idx_exists(idx), data_stack[idx].has_value(), "Index out of range or no data at the index");

    return *data_stack[idx];
  }

  void set_data(const Tree_pos& idx, const X& data) {
    I(_check_idx_exists(idx), "Index out of range");

    data_stack[idx] = data;
  }

  // Use "X operator[](const Tree_pos& idx) const { return get_data(idx); }" to pass data as a const reference
  X operator[](const Tree_pos& idx) { return *data_stack[idx]; }

  /**
   *  Debug API (Temp)
   */
  void print_tree(int deep = 0) {
    for (size_t i = 0; i < pointers_stack.size(); i++) {
      std::cout << "Index: " << (i << CHUNK_SHIFT) << " Parent: " << pointers_stack[i].get_parent()
                << " Data: " << data_stack[i << CHUNK_SHIFT].value_or(-1) << std::endl;
      std::cout << "First Child: " << pointers_stack[i].get_first_child_l() << " ";
      std::cout << "Last Child: " << pointers_stack[i].get_last_child_l() << " ";
      std::cout << "Next Sibling: " << pointers_stack[i].get_next_sibling() << " ";
      std::cout << "Prev Sibling: " << pointers_stack[i].get_prev_sibling() << " ";
      std::cout << "Num Occ: " << pointers_stack[i].get_num_short_del_occ() << std::endl;
      std::cout << "Is Leaf: " << pointers_stack[i].get_is_leaf() << std::endl;
      std::cout << std::endl;
    }

    std::cout << std::endl;

    if (deep) {
      for (size_t i = 0; i < data_stack.size(); i++) {
        if (data_stack[i].has_value()) {
          std::cout << "Index: " << i << " Data: " << data_stack[i].value() << std::endl;
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

  // PRE-ORDER TRAVERSAL
  class pre_order_iterator : public traversal_iterator_base<pre_order_iterator> {
  private:
    using base = traversal_iterator_base<pre_order_iterator>;
    using base::current;
    using base::m_follow_subtrees;
    using base::tree_ptr;

    std::set<Tree_pos> visited_subtrees;
    tree<X>*           current_tree;    // Track which tree we're currently traversing
    tree<X>*           main_tree;       // Keep reference to main tree
    Tree_pos           return_to_node;  // Node to return to after subtree traversal

  public:
    using iterator_category = std::forward_iterator_tag;
    using value_type        = Tree_pos;
    using difference_type   = std::ptrdiff_t;
    using pointer           = Tree_pos*;
    using reference         = Tree_pos&;

    pre_order_iterator(Tree_pos start, tree<X>* tree, bool follow_refs)
        : base(start, tree, follow_refs), current_tree(tree), main_tree(tree), return_to_node(INVALID) {}

    X get_data() const { return current_tree->get_data(current); }

    pre_order_iterator& operator++() {
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
      if (!current_tree->is_leaf(current)) {
        current = current_tree->get_first_child(current);
        return *this;
      }

      // if no children, try to go to next sibling
      auto nxt = current_tree->get_sibling_next(current);
      if (nxt != INVALID) {
        current = nxt;
        return *this;
      }

      // if no next sibling and we're in a subtree, return to main tree
      if (current_tree != main_tree) {
        current_tree   = main_tree;
        current        = current_tree->get_sibling_next(return_to_node);
        return_to_node = INVALID;
        if (current != INVALID) {
          return *this;
        }
      }

      // if no next sibling, go up to parent's next sibling
      auto parent = current_tree->get_parent(current);
      while (parent != ROOT && parent != INVALID) {
        auto parent_sibling = current_tree->get_sibling_next(parent);
        if (parent_sibling != INVALID) {
          current = parent_sibling;
          return *this;
        }
        parent = current_tree->get_parent(parent);
      }

      // if we'e gone through all possibilities, mark as end
      current = INVALID;
      return *this;
    }

    bool operator==(const pre_order_iterator& other) const {
      return current == other.current && current_tree == other.current_tree;
    }

    bool operator!=(const pre_order_iterator& other) const { return !(*this == other); }

    Tree_pos operator*() const { return current; }
  };

  class pre_order_range {
  private:
    Tree_pos m_start;
    tree<X>* m_tree_ptr;
    bool     m_follow_subtrees;

  public:
    pre_order_range(Tree_pos start, tree<X>* tree, bool follow_subtrees = false)
        : m_start(start), m_tree_ptr(tree), m_follow_subtrees(follow_subtrees) {}

    pre_order_iterator begin() { return pre_order_iterator(m_start, m_tree_ptr, m_follow_subtrees); }

    pre_order_iterator end() { return pre_order_iterator(INVALID, m_tree_ptr, m_follow_subtrees); }
  };

  pre_order_range pre_order(Tree_pos start = ROOT, bool follow_subtrees = false) {
    return pre_order_range(start, this, follow_subtrees);
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
      if (!current_tree->is_leaf(current)) {
        current = current_tree->get_first_child(current);
        return *this;
      }

      // if no children, try to go to next sibling
      auto nxt = current_tree->get_sibling_next(current);
      if (nxt != INVALID) {
        current = nxt;
        return *this;
      }

      // if no next sibling and we're in a subtree, return to main tree
      if (current_tree != main_tree) {
        current_tree   = main_tree;
        current        = current_tree->get_sibling_next(return_to_node);
        return_to_node = INVALID;
        if (current != INVALID) {
          return *this;
        }
      }

      // if no next sibling, go up to parent's next sibling
      auto parent = current_tree->get_parent(current);
      while (parent != ROOT && parent != INVALID) {
        auto parent_sibling = current_tree->get_sibling_next(parent);
        if (parent_sibling != INVALID) {
          current = parent_sibling;
          return *this;
        }
        parent = current_tree->get_parent(parent);
      }

      // if we'e gone through all possibilities, mark as end
      current = INVALID;
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

  const_pre_order_range pre_order(Tree_pos start = ROOT) const { return const_pre_order_range(start, this); }

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
    if (!trees[tree_idx]) {
      throw std::runtime_error("Attempting to access deleted tree");
    }
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
 * @brief Get absolute ID of the parent of a node.
 *
 * @param curr_index The absolute ID of the current node.
 * @return Tree_pos The absolute ID of the parent, INVALID if none
 *
 * @assert If the index is out of range
 */
template <typename X>
inline Tree_pos tree<X>::get_parent(const Tree_pos& curr_index) const {
  I(_check_idx_exists(curr_index), "get_parent: Index out of range");

  return pointers_stack[curr_index >> CHUNK_SHIFT].get_parent();
}

/**
 * @brief Check if a node is a leaf node.
 *
 * @param leaf_index The absolute ID of the node.
 * @return true If the node is a leaf node.
 *
 * @assert If the index is out of range
 */
template <typename X>
inline bool tree<X>::is_leaf(const Tree_pos& leaf_index) const {
  I(_check_idx_exists(leaf_index), "is_leaf: Index out of range");

  return pointers_stack[leaf_index >> CHUNK_SHIFT].get_is_leaf();
}

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

  const auto chunk_id       = (parent_index >> CHUNK_SHIFT);
  const auto chunk_offset   = (parent_index & CHUNK_MASK);
  const auto last_child_s_i = chunk_offset ? pointers_stack[chunk_id].get_last_child_s_at(chunk_offset - 1) : INVALID;

  Tree_pos child_chunk_id = INVALID;
  if (last_child_s_i != INVALID) {
    // If the int16_t delta contains a value, go to this nearby chunk
    child_chunk_id = chunk_id + last_child_s_i;
  } else if (chunk_offset == 0) {
    // The first entry will always have the chunk id of the child
    child_chunk_id = pointers_stack[chunk_id].get_last_child_l();
  }

  return (child_chunk_id == INVALID)
             ? (static_cast<Tree_pos>(INVALID))
             : (static_cast<Tree_pos>((child_chunk_id << CHUNK_SHIFT) + pointers_stack[child_chunk_id].get_num_short_del_occ()));
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
  // if (!_check_idx_exists(parent_index)) {
  //     throw std::out_of_range("get_first_child: Parent index out of range");
  // }

  const auto chunk_id     = (parent_index >> CHUNK_SHIFT);
  const auto chunk_offset = (parent_index & CHUNK_MASK);

  Tree_pos child_chunk_id = INVALID;
  if (!chunk_offset) {
    // If the int16_t delta contains a value, go to this nearby chunk
    child_chunk_id = pointers_stack[chunk_id].get_first_child_l();
  } else {
    const auto first_child_s_i = pointers_stack[chunk_id].get_first_child_s_at(chunk_offset - 1);
    if (first_child_s_i != INVALID) {
      // The first entry will always have the chunk id of the child
      child_chunk_id = chunk_id + first_child_s_i;
    }
  }

  // The beginning of the chunk IS the first child (always)
  return (child_chunk_id == INVALID) ? INVALID : static_cast<Tree_pos>(child_chunk_id << CHUNK_SHIFT);
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
  // for (int16_t offset = self_chunk_offset; offset < NUM_SHORT_DEL; offset++) {
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

  // If this is the last child, no next sibling
  if (is_last_child(sibling_id)) {
    return INVALID;
  }

  // Check if the next sibling is within the same chunk, at idx + 1
  const auto curr_chunk_id     = (sibling_id >> CHUNK_SHIFT);
  const auto curr_chunk_offset = (sibling_id & CHUNK_MASK);
  if (curr_chunk_offset < CHUNK_MASK and _contains_data((curr_chunk_id << CHUNK_SHIFT) + curr_chunk_offset + 1)) {
    return static_cast<Tree_pos>((curr_chunk_id << CHUNK_SHIFT) + curr_chunk_offset + 1);
  }

  // Just jump to the next sibling chunk, or returns invalid
  return pointers_stack[curr_chunk_id].get_next_sibling() == INVALID
             ? INVALID
             : static_cast<Tree_pos>(pointers_stack[curr_chunk_id].get_next_sibling() << CHUNK_SHIFT);
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

  // If this is the first child, no prev sibling
  if (is_first_child(sibling_id)) {
    return INVALID;
  }

  // Check if the prev sibling is within the same chunk, at idx - 1
  const auto curr_chunk_id     = (sibling_id >> CHUNK_SHIFT);
  const auto curr_chunk_offset = (sibling_id & CHUNK_MASK);
  if (curr_chunk_offset > 0 and _contains_data((curr_chunk_id << CHUNK_SHIFT) + curr_chunk_offset - 1)) {
    return static_cast<Tree_pos>((curr_chunk_id << CHUNK_SHIFT) + curr_chunk_offset - 1);
  }

  // Just jump to the next sibling chunk, or returns invalid
  const auto prev_sibling = pointers_stack[curr_chunk_id].get_prev_sibling();

  // Find the last occupied in the prev sibling chunk
  return (prev_sibling == INVALID)
             ? INVALID
             : static_cast<Tree_pos>((prev_sibling << CHUNK_SHIFT) + pointers_stack[prev_sibling].get_num_short_del_occ());
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
  /* POSSIBLE IMPROVEMENT -> PERFECTLY FORWARD THE DATA AND SIBLING ID*/
  // if (!_check_idx_exists(sibling_id)) {
  //     throw std::out_of_range("append_sibling: Sibling index out of range");
  // }

  // Directly go to the last sibling of the sibling_id
  const auto parent_id = pointers_stack[sibling_id >> CHUNK_SHIFT].get_parent();
  auto       new_sib   = get_last_child(parent_id);

  // If this chunk does not have more space, just add a new chunk
  // and treat that as the last child
  if ((new_sib & CHUNK_MASK) == CHUNK_MASK) {
    // Make a new chunk after this, and put the data there
    new_sib             = _insert_chunk_after(new_sib >> CHUNK_SHIFT) << CHUNK_SHIFT;
    data_stack[new_sib] = data;
  } else {
    // Just put the data in the next offset
    new_sib++;
    data_stack[new_sib] = data;
  }

  const auto first_sib     = get_first_child(parent_id);
  const auto new_parent_id = _try_fit_child_ptr(parent_id, new_sib);
  if (new_parent_id != parent_id) {
    _update_parent_pointer(first_sib, new_parent_id);
  }

  // Increment the number of occupied slots in the sibling chunk
  pointers_stack[new_sib >> CHUNK_SHIFT].set_num_short_del_occ(((unsigned)new_sib & CHUNK_MASK));

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
  // if (!_check_idx_exists(sibling_id)) {
  //     throw std::out_of_range("insert_next_sibling: Sibling index out of range");
  // }

  // If this is the last child, just append a sibling
  if (is_last_child(sibling_id)) {
    return append_sibling(sibling_id, data);
  }

  // Directly go to the next sibling of the sibling_id
  const auto parent_id = pointers_stack[sibling_id >> CHUNK_SHIFT].get_parent();
  auto       new_sib   = sibling_id;

  // Try to fir the sibling right after this, if the chunk has some space
  if ((new_sib & CHUNK_MASK) != CHUNK_MASK && !_contains_data(new_sib + 1)) {
    new_sib++;
    data_stack[new_sib] = data;
  } else {
    new_sib             = _insert_chunk_after(new_sib >> CHUNK_SHIFT) << CHUNK_SHIFT;
    data_stack[new_sib] = data;
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
  // if (!pointers_stack.empty()) {
  //     throw std::logic_error("add_root: Tree is not empty");
  // }

  // Add empty nodes to make the tree 1-indexed
  for (int i = 0; i < CHUNK_SIZE; i++) {
    data_stack.emplace_back();
  }
  pointers_stack.emplace_back();

  // Make space for CHUNK_SIZE number of entries at the end
  data_stack.emplace_back(data);
  for (int i = 0; i < CHUNK_MASK; i++) {
    data_stack.emplace_back();
  }

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
 * @return Tree_pos The absolute ID of the new child.
 * @throws std::out_of_range If the parent index is out of range
 */
template <typename X>
Tree_pos tree<X>::add_child(const Tree_pos& parent_index, const X& data) {
  // if (!_check_idx_exists(parent_index)) {
  //     throw std::out_of_range("add_child: Parent index out of range: " + std::to_string(parent_index));
  // }

  // This is not the first child being added
  const auto last_child_id = get_last_child(parent_index);
  if (last_child_id != INVALID) {
    return append_sibling(last_child_id, data);
  }

  // Try to fit this child pointer
  const auto child_chunk_id = _create_space(data);
  const auto new_parent_id  = _try_fit_child_ptr(parent_index, child_chunk_id << CHUNK_SHIFT);

  pointers_stack[child_chunk_id].set_parent(new_parent_id);

  // Set num occupied to 0
  pointers_stack[child_chunk_id].set_num_short_del_occ(0);

  // update is_leaf flag
  pointers_stack[parent_index >> CHUNK_SHIFT].set_is_leaf(false);

  return child_chunk_id << CHUNK_SHIFT;
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
  // if (!_check_idx_exists(leaf_index)) {
  //     throw std::out_of_range("delete_leaf: Leaf index out of range");
  // }

  // // Check if the leaf actually is a leaf
  // if (get_first_child(leaf_index) != INVALID) {
  //     throw std::logic_error("delete_leaf: Index is not a leaf");
  // }

  auto& node = pointers_stack[leaf_index >> CHUNK_SHIFT];
  if (node.has_subtree_ref() && forest_ptr) {
    forest_ptr->remove_reference(node.get_subtree_ref());
  }

  const auto leaf_chunk_id     = leaf_index >> CHUNK_SHIFT;
  const auto leaf_chunk_offset = leaf_index & CHUNK_MASK;
  const auto prev_sibling_id   = get_sibling_prev(leaf_index);

  // Empty this spot in the data array
  data_stack[leaf_index] = std::nullopt;
  int16_t remaining_data = 0;

  // Swap this forward
  for (int16_t offset = leaf_chunk_offset; offset < CHUNK_SIZE - 1; offset++) {
    if (_contains_data((leaf_chunk_id << CHUNK_SHIFT) + offset + 1)) {
      remaining_data++;

      data_stack[(leaf_chunk_id << CHUNK_SHIFT) + offset]     = data_stack[(leaf_chunk_id << CHUNK_SHIFT) + offset + 1];
      data_stack[(leaf_chunk_id << CHUNK_SHIFT) + offset + 1] = std::nullopt;

      // Update the parent pointer of the moved node
      const auto fc = get_first_child((leaf_chunk_id << CHUNK_SHIFT) + offset + 1);
      if (fc != INVALID) {
        pointers_stack[fc >> CHUNK_SHIFT].set_parent((leaf_chunk_id << CHUNK_SHIFT) + offset);
        _update_parent_pointer(fc, (leaf_chunk_id << CHUNK_SHIFT) + offset);
      }

      // If this was moved to the first place in the chunk, convert int16_t to long
      if (offset == 0) {
        const auto fc_del = pointers_stack[leaf_chunk_id].get_first_child_s_at(0);
        if (fc_del != INVALID) {
          pointers_stack[leaf_chunk_id].set_first_child_l(fc_del + leaf_chunk_id);
          pointers_stack[leaf_chunk_id].set_first_child_s_at(0, INVALID);
        }

        const auto lc_del = pointers_stack[leaf_chunk_id].get_last_child_s_at(0);
        if (lc_del != INVALID) {
          pointers_stack[leaf_chunk_id].set_last_child_l(lc_del + leaf_chunk_id);
          pointers_stack[leaf_chunk_id].set_last_child_s_at(0, INVALID);
        }
      }
    } else {
      break;
    }
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
    const auto parent_index = pointers_stack[leaf_chunk_id].get_parent();
    const auto new_par_id   = _try_fit_child_ptr(parent_index, prev_sibling_id);
    pointers_stack[leaf_chunk_id].set_parent(new_par_id);

    // check if parent has no more children
    if (get_first_child(parent_index) == INVALID) {
      pointers_stack[parent_index >> CHUNK_SHIFT].set_is_leaf(true);
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
  // if (!_check_idx_exists(subtree_root)) {
  //     throw std::out_of_range("delete_subtree: Subtree root index out of range");
  // }

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

  // Delete nodes in reverse order to ensure leaves are deleted first
  for (auto it = nodes_to_delete.rbegin(); it != nodes_to_delete.rend(); ++it) {
    if (is_leaf(*it)) {
      delete_leaf(*it);
    }
  }
  for (auto node : pre_order(subtree_root)) {
    auto& node_ptr = pointers_stack[node >> CHUNK_SHIFT];
    if (node_ptr.has_subtree_ref() && forest_ptr) {
      forest_ptr->remove_reference(node_ptr.get_subtree_ref());
    }
  }
}

template <typename X>
void tree<X>::add_subtree_ref(const Tree_pos& node_pos, Tree_pos subtree_ref) {
  I(subtree_ref < 0, "Subtree reference must be negative");
  pointers_stack[node_pos >> CHUNK_SHIFT].set_subtree_ref(subtree_ref);
  if (forest_ptr) {
    forest_ptr->add_reference(subtree_ref);
  }
}

}  // namespace hhds
