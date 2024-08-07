//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
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
#include <stdexcept>
#include <vector>

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

using Tree_pos    = uint64_t;
using Short_delta = int32_t;

static constexpr short CHUNK_SHIFT   = 3;                 // The number of bits in a chunk offset
static constexpr short CHUNK_SIZE    = 1 << CHUNK_SHIFT;  // Size of a chunk in bits
static constexpr short CHUNK_MASK    = CHUNK_SIZE - 1;    // Mask for chunk offset
static constexpr short NUM_SHORT_DEL = CHUNK_MASK;        // Number of short delta children in eachc tree_ptr

static constexpr Tree_pos INVALID = 0;                 // This is invalid for all pointers other than parent
static constexpr Tree_pos ROOT    = 1 << CHUNK_SHIFT;  // ROOT ID

static constexpr short CHUNK_BITS  = 43;  // Number of chunks in a tree node
static constexpr short SHORT_DELTA = 21;  // Amount of short delta allowed

static constexpr uint64_t MAX_TREE_SIZE = 1LL << CHUNK_BITS;  // Maximum number of nodes in the tree

static constexpr Short_delta MIN_SHORT_DELTA = -(1 << (SHORT_DELTA - 1));     // Minimum value for short delta
static constexpr Short_delta MAX_SHORT_DELTA = (1 << (SHORT_DELTA - 1)) - 1;  // Maximum value for short delta

class __attribute__((packed, aligned(64))) Tree_pointers {
private:
  // We only store the exact ID of parent
  Tree_pos parent : CHUNK_BITS + CHUNK_SHIFT;
  Tree_pos next_sibling : CHUNK_BITS;
  Tree_pos prev_sibling : CHUNK_BITS;

  // Long child pointers
  Tree_pos first_child_l : CHUNK_BITS;
  Tree_pos last_child_l : CHUNK_BITS;

  // Short (delta) child pointers
  Short_delta first_child_s_0 : SHORT_DELTA;
  Short_delta first_child_s_1 : SHORT_DELTA;
  Short_delta first_child_s_2 : SHORT_DELTA;
  Short_delta first_child_s_3 : SHORT_DELTA;
  Short_delta first_child_s_4 : SHORT_DELTA;
  Short_delta first_child_s_5 : SHORT_DELTA;
  Short_delta first_child_s_6 : SHORT_DELTA;

  Short_delta last_child_s_0 : SHORT_DELTA;
  Short_delta last_child_s_1 : SHORT_DELTA;
  Short_delta last_child_s_2 : SHORT_DELTA;
  Short_delta last_child_s_3 : SHORT_DELTA;
  Short_delta last_child_s_4 : SHORT_DELTA;
  Short_delta last_child_s_5 : SHORT_DELTA;
  Short_delta last_child_s_6 : SHORT_DELTA;

  // Helper functions to get and set first child pointers by index
  Short_delta _get_first_child_s(short index) const {
    switch (index) {
      case 0: return first_child_s_0;
      case 1: return first_child_s_1;
      case 2: return first_child_s_2;
      case 3: return first_child_s_3;
      case 4: return first_child_s_4;
      case 5: return first_child_s_5;
      case 6: return first_child_s_6;

      default: throw std::out_of_range("_get_first_child_s: Invalid index for first_child_s");
    }
  }

  void _set_first_child_s(short index, Short_delta value) {
    switch (index) {
      case 0: first_child_s_0 = value; break;
      case 1: first_child_s_1 = value; break;
      case 2: first_child_s_2 = value; break;
      case 3: first_child_s_3 = value; break;
      case 4: first_child_s_4 = value; break;
      case 5: first_child_s_5 = value; break;
      case 6: first_child_s_6 = value; break;

      default: throw std::out_of_range("_set_first_child_s: Invalid index for first_child_s");
    }
  }

  // Helper functions to get and set last child pointers by index
  Short_delta _get_last_child_s(short index) const {
    switch (index) {
      case 0: return last_child_s_0;
      case 1: return last_child_s_1;
      case 2: return last_child_s_2;
      case 3: return last_child_s_3;
      case 4: return last_child_s_4;
      case 5: return last_child_s_5;
      case 6: return last_child_s_6;

      default: throw std::out_of_range("_get_last_child_s: Invalid index for last_child_s");
    }
  }

  void _set_last_child_s(short index, Short_delta value) {
    switch (index) {
      case 0: last_child_s_0 = value; break;
      case 1: last_child_s_1 = value; break;
      case 2: last_child_s_2 = value; break;
      case 3: last_child_s_3 = value; break;
      case 4: last_child_s_4 = value; break;
      case 5: last_child_s_5 = value; break;
      case 6: last_child_s_6 = value; break;

      default: throw std::out_of_range("_get_last_child_s: Invalid index for last_child_s");
    }
  }

  // :private

public:
  /* DEFAULT CONSTRUCTOR */
  // Parent can be = 0 for someone, but it can never be MAX_TREE_SIZE
  // That is why the best way to invalidate is to set it to MAX_TREE_SIZE
  // for every other entry INVALID is the best choice
  Tree_pointers() : parent(INVALID), next_sibling(INVALID), prev_sibling(INVALID), first_child_l(INVALID), last_child_l(INVALID) {
    for (short i = 0; i < NUM_SHORT_DEL; i++) {
      _set_first_child_s(i, INVALID);
      _set_last_child_s(i, INVALID);
    }
  }

  /* PARAM CONSTRUCTOR */
  Tree_pointers(Tree_pos p)
      : parent(p), next_sibling(INVALID), prev_sibling(INVALID), first_child_l(INVALID), last_child_l(INVALID) {
    for (short i = 0; i < NUM_SHORT_DEL; i++) {
      _set_first_child_s(i, INVALID);
      _set_last_child_s(i, INVALID);
    }
  }

  // Getters
  Tree_pos get_parent() const { return parent; }
  Tree_pos get_next_sibling() const { return next_sibling; }
  Tree_pos get_prev_sibling() const { return prev_sibling; }
  Tree_pos get_first_child_l() const { return first_child_l; }
  Tree_pos get_last_child_l() const { return last_child_l; }

  // Getters for short child pointers
  Short_delta get_first_child_s_at(short index) const { return _get_first_child_s(index); }
  Short_delta get_last_child_s_at(short index) const { return _get_last_child_s(index); }

  // Setters
  void set_parent(Tree_pos p) { parent = p; }
  void set_next_sibling(Tree_pos ns) { next_sibling = ns; }
  void set_prev_sibling(Tree_pos ps) { prev_sibling = ps; }
  void set_first_child_l(Tree_pos fcl) { first_child_l = fcl; }
  void set_last_child_l(Tree_pos lcl) { last_child_l = lcl; }

  // Setters for short child pointers
  void set_first_child_s_at(short index, Short_delta fcs) { _set_first_child_s(index, fcs); }
  void set_last_child_s_at(short index, Short_delta lcs) { _set_last_child_s(index, lcs); }

  // Operators
  constexpr bool operator==(const Tree_pointers& other) const {
    for (short i = 0; i < 7; i++) {
      if (_get_first_child_s(i) != other._get_first_child_s(i) || _get_last_child_s(i) != other._get_last_child_s(i)) {
        return false;
      }
    }

    return parent == other.parent && next_sibling == other.next_sibling && prev_sibling == other.prev_sibling
           && first_child_l == other.first_child_l && last_child_l == other.last_child_l;
  }

  constexpr bool operator!=(const Tree_pointers& other) const { return !(*this == other); }
  void           invalidate() { parent = INVALID; }
  // :public

};  // Tree_pointers class

template <typename X>
class tree {
private:
  /* The tree pointers and data stored separately */
  std::vector<Tree_pointers>    pointers_stack;
  std::vector<std::optional<X>> data_stack;

  /* Special functions for sanity */
  [[nodiscard]] bool _check_idx_exists(const Tree_pos& idx) const noexcept {
    // idx >= 0 not needed for unsigned int
    return idx < static_cast<Tree_pos>(data_stack.size());
  }
  [[nodiscard]] bool _contains_data(const Tree_pos& idx) const noexcept {
    return (idx < data_stack.size() && data_stack[idx].has_value());
  }

  /* Function to add an entry to the pointers and data stack (typically for add/append)*/
  [[nodiscard]] Tree_pos _create_space(const Tree_pos& parent_index, const X& data) {
    if (pointers_stack.size() >= MAX_TREE_SIZE) {
      throw std::out_of_range("_create_space: Tree size exceeded");
    } else if (!_check_idx_exists(parent_index)) {
      throw std::out_of_range("_create_space: Parent index out of range");
    }

    // Make space for CHUNK_SIZE number of entries at the end
    data_stack.emplace_back(data);
    for (int i = 0; i < CHUNK_MASK; i++) {
      data_stack.emplace_back();
    }

    // Add the single pointer node for all CHUNK_SIZE entries
    pointers_stack.emplace_back();

    return (data_stack.size() - CHUNK_SIZE) >> CHUNK_SHIFT;
  }

  /* Function to insert a new chunk in between (typically for handling add/append corner cases)*/
  [[nodiscard]] Tree_pos _insert_chunk_after(const Tree_pos curr) {
    if (pointers_stack.size() >= MAX_TREE_SIZE) {
      throw std::out_of_range("_insert_chunk_after: Tree size exceeded");
    } else if (!_check_idx_exists(curr)) {
      throw std::out_of_range("_insert_chunk_after: Current index out of range");
    }

    // Allot new chunk at the end
    const auto new_chunk_id = _create_space(pointers_stack[curr].get_parent(), X());

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

  /* Helper function to check if we can fit something in the short delta*/
  [[nodiscard]] bool _fits_in_short_del(const Tree_pos parent_chunk_id, const Tree_pos child_chunk_id) {
    const int delta = child_chunk_id - parent_chunk_id;
    return abs(delta) <= MAX_SHORT_DELTA;
  }

  /* Helper function to update the parent pointer of all sibling chunks*/
  void _update_parent_pointer(const Tree_pos first_child, const Tree_pos new_parent_id) {
    auto curr_chunk_id = (first_child >> CHUNK_SHIFT);

    while (curr_chunk_id != INVALID) {
      pointers_stack[curr_chunk_id].set_parent(new_parent_id);
      curr_chunk_id = pointers_stack[curr_chunk_id].get_next_sibling();
    }
  }

  /* Helper function to break the chunk starting at a a given offset inside it*/
  [[nodiscard]] Tree_pos _break_chunk_from(const Tree_pos abs_id) {
    /* FOR NOW THIS HAS BEEN DEPRECATED */
    const auto old_chunk_id     = (abs_id >> CHUNK_SHIFT);
    const auto old_chunk_offset = (abs_id & CHUNK_MASK);

    // Insert a blank chunk after the current chunk
    bool     requires_new_chunk = true;
    bool     retval_set         = false;
    short    new_chunk_offset   = 0;
    Tree_pos new_chunk_id, retval; /* To store the chunk that is being populated */

    for (short offset = old_chunk_offset; offset < NUM_SHORT_DEL; offset++) {
      const auto curr_id = (old_chunk_id << CHUNK_SHIFT) + offset;

      // Get first and last child abs id
      const auto fc = get_first_child(curr_id);
      const auto lc = get_last_child(curr_id);

      if (!requires_new_chunk) {
        // Try fitting first and last child in the short delta
        if (_fits_in_short_del(old_chunk_id, fc >> CHUNK_SHIFT) and _fits_in_short_del(old_chunk_id, lc >> CHUNK_SHIFT)) {
          pointers_stack[old_chunk_id].set_first_child_s_at(new_chunk_offset, (fc >> CHUNK_SHIFT) - old_chunk_id);
          pointers_stack[old_chunk_id].set_last_child_s_at(new_chunk_offset, (lc >> CHUNK_SHIFT) - old_chunk_id);
          new_chunk_offset++;

          // Copy the data to the new chunk
          data_stack[(new_chunk_id << CHUNK_SHIFT) + new_chunk_offset] = data_stack[curr_id];

          // Update the parent pointer of the children
          _update_parent_pointer(fc, (new_chunk_id << CHUNK_SHIFT) + new_chunk_offset);

          continue;
        }

        requires_new_chunk = true;
      }

      // Make new chunk since required
      new_chunk_id       = _insert_chunk_after(old_chunk_id);
      requires_new_chunk = false;

      // Copy the data to the new chunk
      data_stack[new_chunk_id << CHUNK_SHIFT] = data_stack[curr_id];

      // Set long pointers
      pointers_stack[new_chunk_id].set_first_child_l(fc >> CHUNK_SHIFT);
      pointers_stack[new_chunk_id].set_last_child_l(lc >> CHUNK_SHIFT);

      // Update all the children with the new parent id
      _update_parent_pointer(fc, new_chunk_id);
      new_chunk_offset = 1;

      if (!retval_set) {
        retval     = new_chunk_id;
        retval_set = true;
      }
    }

    // Clear old chunk if all children are moved
    for (short offset = old_chunk_offset; offset < NUM_SHORT_DEL; offset++) {
      data_stack[(old_chunk_id << CHUNK_SHIFT) + offset] = X();
    }
    for (short offset = old_chunk_offset; offset < NUM_SHORT_DEL; offset++) {
      pointers_stack[old_chunk_id].set_first_child_s_at(offset, INVALID);
      pointers_stack[old_chunk_id].set_last_child_s_at(offset, INVALID);
    }

    return retval;
  }

  void _try_fit_child_ptr(const Tree_pos parent_id, const Tree_pos child_id) {
    // Check and throw accordingly
    if (!_check_idx_exists(parent_id) || !_check_idx_exists(child_id)) {
      throw std::out_of_range("_try_fit_child_ptr: Index out of range");
    }

    /* BASE CASE OF THE RECURSION */
    // If parent has long ptr access, this is easy
    if ((parent_id & CHUNK_MASK) == 0) {
      pointers_stack[parent_id >> CHUNK_SHIFT].set_last_child_l(child_id >> CHUNK_SHIFT);
      if (pointers_stack[parent_id >> CHUNK_SHIFT].get_first_child_l() == INVALID) {
        pointers_stack[parent_id >> CHUNK_SHIFT].set_first_child_l(child_id >> CHUNK_SHIFT);
      }
      return;
    }

    // Now, try to fit the child in the short delta
    const auto parent_chunk_id     = (parent_id >> CHUNK_SHIFT);
    const auto parent_chunk_offset = (parent_id & CHUNK_MASK);
    if (_fits_in_short_del(parent_chunk_id, child_id)) {
      // Adjust the child pointers
      pointers_stack[parent_chunk_id].set_last_child_s_at(parent_chunk_offset - 1, (child_id >> CHUNK_SHIFT) - parent_chunk_id);

      if (pointers_stack[parent_chunk_id].get_first_child_s_at(parent_chunk_offset - 1) == INVALID) {
        pointers_stack[parent_chunk_id].set_first_child_s_at(parent_chunk_offset - 1, (child_id >> CHUNK_SHIFT) - parent_chunk_id);
      }

      return;
    }

    /* RECURSION */
    auto last_new_chunk = INVALID;
    // Step 1: Break the chunk fully
    for (short offset = parent_chunk_offset; offset < NUM_SHORT_DEL; offset++) {
      if (_contains_data((parent_chunk_id << CHUNK_SHIFT) + offset)) {
        const auto curr_id = (parent_chunk_id << CHUNK_SHIFT) + offset;

        // Create a new chunk, put this one over there
        const auto new_chunk_id = _insert_chunk_after(curr_id);

        // Remove data from old, and put it here
        data_stack[new_chunk_id << CHUNK_SHIFT] = data_stack[curr_id];
        data_stack[curr_id]                     = X();

        // Convert the short pointers here to long pointers there
        const auto fc = pointers_stack[curr_id].get_first_child_s_at(offset);
        const auto lc = pointers_stack[curr_id].get_last_child_s_at(offset);
        pointers_stack[new_chunk_id].set_first_child_l(fc + parent_chunk_id);
        pointers_stack[new_chunk_id].set_last_child_l(lc + parent_chunk_id);

        // Update the parent pointer of all children of this guy
        _update_parent_pointer((fc + parent_chunk_id) << CHUNK_SHIFT, new_chunk_id << CHUNK_SHIFT);

        // Remove the short pointers
        pointers_stack[curr_id].set_first_child_s_at(offset, INVALID);
        pointers_stack[curr_id].set_last_child_s_at(offset, INVALID);

        // Set the last new chunk
        last_new_chunk = new_chunk_id;
      }
    }

    // Step 2: Try fitting the last chunk here in the parent. Recurse.
    _try_fit_child_ptr(last_new_chunk, child_id);
  }
  // :private

public:
  /**
   *  Query based API (no updates)
   */
  Tree_pos get_parent(const Tree_pos& curr_index);
  Tree_pos get_last_child(const Tree_pos& parent_index);
  Tree_pos get_first_child(const Tree_pos& parent_index);
  bool     is_last_child(const Tree_pos& self_index);
  bool     is_first_child(const Tree_pos& self_index);
  Tree_pos get_sibling_next(const Tree_pos& sibling_id);
  Tree_pos get_sibling_prev(const Tree_pos& sibling_id);
  int      get_tree_width(const int& level);

  /**
   *  Update based API (Adds and Deletes from the tree)
   */
  Tree_pos append_sibling(const Tree_pos& sibling_id, const X& data);
  Tree_pos add_child(const Tree_pos& parent_index, const X& data);
  Tree_pos add_root(const X& data);

  void delete_leaf(const Tree_pos& leaf_index);

  /**
   * Data access API
   */
  X& get_data(const Tree_pos& idx) {
    if (!_check_idx_exists(idx) || !data_stack[idx].has_value()) {
      throw std::out_of_range("get_data: Index out of range or no data at index");
    }

    return data_stack[idx].value();
  }

  const X& get_data(const Tree_pos& idx) const {
    if (!_check_idx_exists(idx) || !data_stack[idx].has_value()) {
      throw std::out_of_range("get_data: Index out of range or no data at index");
    }

    return data_stack[idx].value();
  }

  void set_data(const Tree_pos& idx, const X& data) {
    if (!_check_idx_exists(idx)) {
      throw std::out_of_range("set_data: Index out of range");
    }

    data_stack[idx] = data;
  }

  X operator[](const Tree_pos& idx) { return get_data(idx); }

  /**
   *  Debug API (Temp)
   */
  void print_tree(int deep = 0) {
    for (size_t i = 0; i < pointers_stack.size(); i++) {
      std::cout << "Index: " << i << " Parent: " << pointers_stack[i].get_parent()
                << " Data: " << data_stack[i << CHUNK_SHIFT].value_or(-1) << std::endl;
      std::cout << "First Child: " << pointers_stack[i].get_first_child_l() << " ";
      std::cout << "Last Child: " << pointers_stack[i].get_last_child_l() << " ";
      std::cout << "Next Sibling: " << pointers_stack[i].get_next_sibling() << " ";
      std::cout << "Prev Sibling: " << pointers_stack[i].get_prev_sibling() << std::endl;
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

  /**
   * ITERATORS
   * - SIBLING-ORDER(start)
   * - POSTORDER (subtree_parent)
   * - PREORDER (subtree_parent)
   */

  // SIBLING ORDER TRAVERSAL
  class sibling_order_iterator {
  private:
    Tree_pos current;
    tree<X>* tree_ptr;

  public:
    using iterator_category = std::forward_iterator_tag;
    using value_type        = Tree_pos;
    using difference_type   = std::ptrdiff_t;
    using pointer           = Tree_pos*;
    using reference         = Tree_pos&;

    sibling_order_iterator(Tree_pos start, tree<X>* tree) : current(start), tree_ptr(tree) {}

    sibling_order_iterator& operator++() {
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

    bool operator==(const sibling_order_iterator& other) const { return current == other.current; }

    bool operator!=(const sibling_order_iterator& other) const { return current != other.current; }

    Tree_pos  operator*() const { return current; }
    Tree_pos* operator->() const { return &current; }
  };

  class sibling_order_range {
  private:
    Tree_pos m_start;
    tree<X>* m_tree_ptr;

  public:
    sibling_order_range(Tree_pos start, tree<X>* tree) : m_start(start), m_tree_ptr(tree) {}

    sibling_order_iterator begin() { return sibling_order_iterator(m_start, m_tree_ptr); }

    sibling_order_iterator end() { return sibling_order_iterator(INVALID, m_tree_ptr); }
  };

  sibling_order_range sibling_order(Tree_pos start) { return sibling_order_range(start, this); }

  class const_sibling_order_iterator {
  private:
    Tree_pos       current;
    const tree<X>* tree_ptr;

  public:
    using iterator_category = std::forward_iterator_tag;
    using value_type        = Tree_pos;
    using difference_type   = std::ptrdiff_t;
    using pointer           = const Tree_pos*;
    using reference         = const Tree_pos&;

    const_sibling_order_iterator(Tree_pos start, const tree<X>* tree) : current(start), tree_ptr(tree) {}

    const_sibling_order_iterator& operator++() {
      if (tree_ptr->get_sibling_next(current) != INVALID) {
        current = tree_ptr->get_sibling_next(current);
      } else {
        current = INVALID;
      }
      return *this;
    }

    const_sibling_order_iterator operator++(int) {
      const_sibling_order_iterator temp = *this;
      ++(*this);
      return temp;
    }

    bool operator==(const const_sibling_order_iterator& other) const { return current == other.current; }

    bool operator!=(const const_sibling_order_iterator& other) const { return current != other.current; }

    Tree_pos        operator*() const { return current; }
    const Tree_pos* operator->() const { return &current; }
  };

  class const_sibling_order_range {
  private:
    Tree_pos       m_start;
    const tree<X>* m_tree_ptr;

  public:
    const_sibling_order_range(Tree_pos start, const tree<X>* tree) : m_start(start), m_tree_ptr(tree) {}

    const_sibling_order_iterator begin() const { return const_sibling_order_iterator(m_start, m_tree_ptr); }

    const_sibling_order_iterator end() const { return const_sibling_order_iterator(INVALID, m_tree_ptr); }
  };

  const_sibling_order_range sibling_order(Tree_pos start) const { return const_sibling_order_range(start, this); }

  // PRE-ORDER TRAVERSAL
  class pre_order_iterator {
  private:
    Tree_pos current;
    tree<X>* tree_ptr;

  public:
    using iterator_category = std::forward_iterator_tag;
    using value_type        = Tree_pos;
    using difference_type   = std::ptrdiff_t;
    using pointer           = Tree_pos*;
    using reference         = Tree_pos&;

    pre_order_iterator(Tree_pos start, tree<X>* tree) : current(start), tree_ptr(tree) {}

    pre_order_iterator& operator++() {
      if (tree_ptr->get_first_child(current) != INVALID) {
        current = tree_ptr->get_first_child(current);
      } else {
        // See if there is a sibling we can move to
        const auto nxt = tree_ptr->get_sibling_next(current);
        if (nxt != INVALID) {
          current = nxt;
        } else {
          // No more siblings, let's move to an ancestor's sibling
          bool found  = false;
          auto parent = tree_ptr->get_parent(current);
          while (parent != ROOT) {
            const auto parent_sibling = tree_ptr->get_sibling_next(parent);
            if (parent_sibling != INVALID) {
              current = parent_sibling;
              found   = true;
              break;
            }
            parent = tree_ptr->get_parent(parent);
          }

          if (!found) {
            current = INVALID;
          }
        }
      }
      return *this;
    }

    pre_order_iterator operator++(int) {
      pre_order_iterator temp = *this;
      ++(*this);
      return temp;
    }

    bool operator==(const pre_order_iterator& other) const { return current == other.current; }

    bool operator!=(const pre_order_iterator& other) const { return current != other.current; }

    Tree_pos  operator*() const { return current; }
    Tree_pos* operator->() const { return &current; }
  };

  class pre_order_range {
  private:
    Tree_pos m_start;
    tree<X>* m_tree_ptr;

  public:
    pre_order_range(Tree_pos start, tree<X>* tree) : m_start(start), m_tree_ptr(tree) {}

    pre_order_iterator begin() { return pre_order_iterator(m_start, m_tree_ptr); }

    pre_order_iterator end() { return pre_order_iterator(INVALID, m_tree_ptr); }
  };

  pre_order_range pre_order(Tree_pos start = ROOT) { return pre_order_range(start, this); }

  class const_pre_order_iterator {
  private:
    Tree_pos       current;
    const tree<X>* tree_ptr;

  public:
    using iterator_category = std::forward_iterator_tag;
    using value_type        = Tree_pos;
    using difference_type   = std::ptrdiff_t;
    using pointer           = Tree_pos*;
    using reference         = Tree_pos&;

    const_pre_order_iterator(Tree_pos start, const tree<X>* tree) : current(start), tree_ptr(tree) {}

    const_pre_order_iterator& operator++() {
      if (tree_ptr->get_first_child(current) != INVALID) {
        current = tree_ptr->get_first_child(current);
      } else {
        while (tree_ptr->get_sibling_next(current) == INVALID) {
          current = tree_ptr->get_parent(current);
          if (current == INVALID) {
            break;
          }
        }
        if (current != INVALID) {
          current = tree_ptr->get_sibling_next(current);
        }
      }
      return *this;
    }

    const_pre_order_iterator operator++(int) {
      const_pre_order_iterator temp = *this;
      ++(*this);
      return temp;
    }

    bool operator==(const const_pre_order_iterator& other) const { return current == other.current; }

    bool operator!=(const const_pre_order_iterator& other) const { return current != other.current; }

    const X& operator*() const { return tree_ptr->get_data(current); }
    const X* operator->() const { return &tree_ptr->get_data(current); }
  };

  class const_pre_order_range {
  private:
    Tree_pos       m_start;
    const tree<X>* m_tree_ptr;

  public:
    const_pre_order_range(Tree_pos start, const tree<X>* tree) : m_start(start), m_tree_ptr(tree) {}

    const_pre_order_iterator begin() { return const_pre_order_iterator(m_start, m_tree_ptr); }

    const_pre_order_iterator end() { return const_pre_order_iterator(INVALID, m_tree_ptr); }
  };

  const_pre_order_range pre_order(Tree_pos start = ROOT) const { return const_pre_order_range(start, this); }

  // POST-ORDER TRAVERSAL
  class post_order_iterator {
  private:
    Tree_pos current;
    tree<X>* tree_ptr;

  public:
    using iterator_category = std::forward_iterator_tag;
    using value_type        = Tree_pos;
    using difference_type   = std::ptrdiff_t;
    using pointer           = Tree_pos*;
    using reference         = Tree_pos&;

    post_order_iterator(Tree_pos start, tree<X>* tree) : current(start), tree_ptr(tree) {}

    post_order_iterator& operator++() {
      post_order_iterator temp = *this;
      if (tree_ptr->get_sibling_next(current) != INVALID) {
        auto next = tree_ptr->get_sibling_next(current);
        while (tree_ptr->get_sibling_next(next) != INVALID) {
          next = tree_ptr->get_first_child(next);
        }

        current = next;
      } else {
        current = tree_ptr->get_parent(current);
      }
      return temp;
    }

    bool operator==(const post_order_iterator& other) const { return current == other.current; }

    bool operator!=(const post_order_iterator& other) const { return current != other.current; }

    Tree_pos  operator*() const { return current; }
    Tree_pos* operator->() const { return &current; }
  };

  class post_order_range {
  private:
    Tree_pos m_start;
    tree<X>* m_tree_ptr;

  public:
    post_order_range(Tree_pos start, tree<X>* tree) : m_start(start), m_tree_ptr(tree) {}

    post_order_iterator begin() { return post_order_iterator(m_start, m_tree_ptr); }

    post_order_iterator end() { return post_order_iterator(INVALID, m_tree_ptr); }
  };

  post_order_range post_order(Tree_pos start = ROOT) { return post_order_range(start, this); }

  class const_post_order_iterator {
  private:
    Tree_pos       current;
    const tree<X>* tree_ptr;

  public:
    using iterator_category = std::forward_iterator_tag;
    using value_type        = Tree_pos;
    using difference_type   = std::ptrdiff_t;
    using pointer           = Tree_pos*;
    using reference         = Tree_pos&;

    const_post_order_iterator(Tree_pos start, const tree<X>* tree) : current(start), tree_ptr(tree) {}

    const_post_order_iterator& operator++() {
      // post_order_iterator temp = *this;
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

    const X& operator*() const { return tree_ptr->get_data(current); }
    const X* operator->() const { return &tree_ptr->get_data(current); }
  };

  class const_post_order_range {
  private:
    Tree_pos       m_start;
    const tree<X>* m_tree_ptr;

  public:
    const_post_order_range(Tree_pos start, const tree<X>* tree) : m_start(start), m_tree_ptr(tree) {}

    const_post_order_iterator begin() { return const_post_order_iterator(m_start, m_tree_ptr); }

    const_post_order_iterator end() { return const_post_order_iterator(INVALID, m_tree_ptr); }
  };

  const_post_order_range post_order(Tree_pos start = ROOT) const { return const_post_order_range(start, this); }

  // :public

};  // tree class

// ---------------------------------- TEMPLATE IMPLEMENTATION ---------------------------------- //
/**
 * @brief Get absolute ID of the parent of a node.
 *
 * @param curr_index The absolute ID of the current node.
 * @return Tree_pos The absolute ID of the parent, INVALID if none
 *
 * @throws std::out_of_range If current index is out of range
 */
template <typename X>
Tree_pos tree<X>::get_parent(const Tree_pos& curr_index) {
  if (!_check_idx_exists(curr_index)) {
    throw std::out_of_range("get_parent: Index out of range");
  }

  return pointers_stack[curr_index >> CHUNK_SHIFT].get_parent();
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
Tree_pos tree<X>::get_last_child(const Tree_pos& parent_index) {
  if (!_check_idx_exists(parent_index)) {
    throw std::out_of_range("get_last_child: Parent index out of range");
  }

  const auto chunk_id       = (parent_index >> CHUNK_SHIFT);
  const auto chunk_offset   = (parent_index & CHUNK_MASK);
  const auto last_child_s_i = chunk_offset ? pointers_stack[chunk_id].get_last_child_s_at(chunk_offset - 1) : INVALID;

  Tree_pos child_chunk_id = INVALID;
  if (last_child_s_i != INVALID) {
    // If the short delta contains a value, go to this nearby chunk
    child_chunk_id = chunk_id + last_child_s_i;
  } else if (chunk_offset == 0) {
    // The first entry will always have the chunk id of the child
    child_chunk_id = pointers_stack[chunk_id].get_last_child_l();
  }

  // Iterate in reverse to find the last occupied in child chunk
  if (child_chunk_id != INVALID) {
    for (short offset = NUM_SHORT_DEL; offset >= 0; offset--) {
      if (_contains_data((child_chunk_id << CHUNK_SHIFT) + offset)) {
        return static_cast<Tree_pos>((child_chunk_id << CHUNK_SHIFT) + offset);
      }
    }
  }

  return static_cast<Tree_pos>(INVALID);
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
Tree_pos tree<X>::get_first_child(const Tree_pos& parent_index) {
  if (!_check_idx_exists(parent_index)) {
    throw std::out_of_range("get_first_child: Parent index out of range");
  }

  const auto chunk_id        = (parent_index >> CHUNK_SHIFT);
  const auto chunk_offset    = (parent_index & CHUNK_MASK);
  const auto first_child_s_i = chunk_offset ? pointers_stack[chunk_id].get_first_child_s_at(chunk_offset - 1) : INVALID;

  Tree_pos child_chunk_id = INVALID;
  if (chunk_offset and (first_child_s_i != INVALID)) {
    // If the short delta contains a value, go to this nearby chunk
    child_chunk_id = chunk_id + first_child_s_i;
  } else {
    // The first entry will always have the chunk id of the child
    child_chunk_id = pointers_stack[chunk_id].get_first_child_l();
  }

  // The beginning of the chunk IS the first child (always)
  return static_cast<Tree_pos>(child_chunk_id << CHUNK_SHIFT);
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
bool tree<X>::is_last_child(const Tree_pos& self_index) {
  if (!_check_idx_exists(self_index)) {
    throw std::out_of_range("is_last_child: Index out of range");
  }

  const auto self_chunk_id     = (self_index >> CHUNK_SHIFT);
  const auto self_chunk_offset = (self_index & CHUNK_MASK);

  // If this chunk has a next_sibling pointer, certainly not the last child
  if (pointers_stack[self_chunk_id].get_next_sibling() != INVALID) {
    return false;
  }

  // Now, to be the last child, all entries after this should be invalid
  for (short offset = self_chunk_offset; offset < NUM_SHORT_DEL; offset++) {
    /* POSSIBLE IMPROVEMENT, instead of checking the data pointers track,just check if the pointer is MIN_VAL
      BE CAREFUL HERE THOUGH, DONT SIMPLY CHECK IF THE SHORT DELTA = 0, BUT ASSIGN A SPECIAL VALUE WHICH IS
      -2 ** (SHORT_DELTA_BITS)*/
    // const auto last_child_s_i = pointers_stack[self_chunk_id].get_last_child_s_at(offset);
    if (_contains_data((self_chunk_id << CHUNK_SHIFT) + offset)) {
      return false;
    }
  }

  return true;
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
bool tree<X>::is_first_child(const Tree_pos& self_index) {
  if (!_check_idx_exists(self_index)) {
    throw std::out_of_range("is_first_child: Index out of range");
  }

  const auto self_chunk_id     = (self_index >> CHUNK_SHIFT);
  const auto self_chunk_offset = (self_index & CHUNK_MASK);

  // If this chunk has a prev_sibling pointer, certainly not the last child
  if (pointers_stack[self_chunk_id].get_prev_sibling() != INVALID) {
    return false;
  }

  // Now, to be the first child, it must have no offset
  if (self_chunk_offset) {
    return false;
  }

  return true;
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
Tree_pos tree<X>::get_sibling_next(const Tree_pos& sibling_id) {
  if (!_check_idx_exists(sibling_id)) {
    throw std::out_of_range("get_sibling_next: Sibling index out of range");
  }

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
  return pointers_stack[curr_chunk_id].get_next_sibling() << CHUNK_SHIFT;  // default is INVALID
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
Tree_pos tree<X>::get_sibling_prev(const Tree_pos& sibling_id) {
  if (!_check_idx_exists(sibling_id)) {
    throw std::out_of_range("get_sibling_prev: Sibling index out of range");
  }

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
  if (prev_sibling != INVALID) {
    for (short offset = NUM_SHORT_DEL - 1; offset >= 0; offset--) {
      if (_contains_data(prev_sibling + offset)) {
        return static_cast<Tree_pos>(prev_sibling + offset);
      }
    }
  }

  return INVALID;
}

template <typename X>
int tree<X>::get_tree_width(const int& level) {
  (void)level;
  // Placeholder: Implement the actual width calculation using BFS or additional bookkeeping
  return 0;
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
  if (!_check_idx_exists(sibling_id)) {
    throw std::out_of_range("append_sibling: Sibling index out of range");
  }

  // Directly go to the last sibling of the sibling_id
  const auto parent_id           = pointers_stack[sibling_id >> CHUNK_SHIFT].get_parent();
  const auto parent_chunk_id     = parent_id >> CHUNK_SHIFT;
  const auto parent_chunk_offset = parent_id & CHUNK_MASK;

  auto new_sib = get_last_child(parent_id);

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

  // _try_fit_child_ptr(parent_id, new_sib);

  // new_sib is the new sibling, split it into id and offset
  const auto chunk_id = new_sib >> CHUNK_SHIFT;

  // Update the parent pointer of the new sibling
  /* NOTE : MAKE SURE TO NOT LEAVE ANY GAPS IN THE DATA STACK WHEN DELETING */
  if (parent_chunk_offset) {
    if (_fits_in_short_del(parent_id, chunk_id)) {
      const auto delta = chunk_id - parent_chunk_id;
      pointers_stack[parent_chunk_id].set_last_child_s_at(parent_chunk_offset - 1, static_cast<Short_delta>(delta));
    } else {
      // Break the parent chunk from the parent id
      const auto new_parent_chunk_id = _break_chunk_from(parent_id);

      // Set the last child pointer of the parent
      pointers_stack[new_parent_chunk_id].set_last_child_l(chunk_id);

      // Set the first child pointer if needed
      if (pointers_stack[new_parent_chunk_id].get_first_child_l() == INVALID) {
        pointers_stack[new_parent_chunk_id].set_first_child_l(chunk_id);
      }

      // Setting the first child pointer should never be necessary
      // Realize that we have appended a sibling, so it a child of
      // the parent must have existed before this.
      // assert(pointers_stack[new_parent_chunk_id].get_first_child_l() != INVALID);
    }
  } else {
    // Can directly update the long pointer
    pointers_stack[parent_chunk_id].set_last_child_l(chunk_id);

    // Set the first child pointer if needed
    if (pointers_stack[parent_chunk_id].get_first_child_l() == INVALID) {
      pointers_stack[parent_chunk_id].set_first_child_l(chunk_id);
    }

    // Setting the first child pointer should never be necessary
    // Realize that we have appended a sibling, so it a child of
    // the parent must have existed before this.
    // assert(pointers_stack[parent_chunk_id].get_first_child_l() != INVALID);
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
  if (!pointers_stack.empty()) {
    throw std::logic_error("add_root: Tree is not empty");
  }

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

  return (data_stack.size() - CHUNK_SIZE);
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
  if (!_check_idx_exists(parent_index)) {
    throw std::out_of_range("add_child: Parent index out of range: " + std::to_string(parent_index));
  }

  const auto last_child_id = get_last_child(parent_index);

  // This is not the first child being added
  if (last_child_id != INVALID) {
    return append_sibling(last_child_id, data);
  }

  const auto child_chunk_id = _create_space(parent_index, data);
  pointers_stack[child_chunk_id].set_parent(parent_index);

  // // Try to fit this child pointer
  // _try_fit_child_ptr(parent_index,
  //                    child_chunk_id << CHUNK_SHIFT);

  const auto parent_chunk_id     = (parent_index >> CHUNK_SHIFT);
  const auto parent_chunk_offset = (parent_index & CHUNK_MASK);

  /* Update the parent's first and last child pointers to this new child */
  if (parent_chunk_offset == 0) {
    // The offset is 0, we can fit the chunk id of the child directly
    pointers_stack[parent_chunk_id].set_first_child_l(child_chunk_id);
    pointers_stack[parent_chunk_id].set_last_child_l(child_chunk_id);
  } else {
    // Try to fit the delta in the short delta pointers
    const auto delta = (child_chunk_id - parent_chunk_id);  // todo : CHECK DANGER
    if (_fits_in_short_del(parent_index, child_chunk_id)) {
      // I am resetting the first child s pointer, I don't need to do it
      pointers_stack[parent_chunk_id].set_first_child_s_at(parent_chunk_offset - 1, static_cast<Short_delta>(delta));

      pointers_stack[parent_chunk_id].set_last_child_s_at(parent_chunk_offset - 1, static_cast<Short_delta>(delta));
    } else {
      // Break the parent chunk from the parent id
      const auto new_parent_chunk_id = _break_chunk_from(parent_index);

      // Set the first and last child pointer of the parent to the new parent chunk
      pointers_stack[new_parent_chunk_id].set_first_child_l(child_chunk_id);
      pointers_stack[new_parent_chunk_id].set_last_child_l(child_chunk_id);
    }
  }

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
  if (!_check_idx_exists(leaf_index)) {
    throw std::out_of_range("delete_leaf: Leaf index out of range");
  }

  // Check if the leaf actually is a leaf
  if (get_first_child(leaf_index) != INVALID) {
    throw std::logic_error("delete_leaf: Index is not a leaf");
  }

  const auto leaf_chunk_id     = leaf_index >> CHUNK_SHIFT;
  const auto leaf_chunk_offset = leaf_index & CHUNK_MASK;
  const auto prev_sibling_id   = get_sibling_prev(leaf_index);

  // Let's maintain the invariant that there will be
  // no "gaps" in a chunk after deletion.
  // Empty this spot in the data array
  data_stack[leaf_index] = X();

  // Then swap forward within the chunk
  for (short offset = leaf_chunk_offset; offset < NUM_SHORT_DEL - 1; offset++) {
    if (!_contains_data((leaf_chunk_id << CHUNK_SHIFT) + offset + 1)) {
      break;
    }

    std::swap(data_stack[(leaf_chunk_id << CHUNK_SHIFT) + offset], data_stack[(leaf_chunk_id << CHUNK_SHIFT) + offset + 1]);

    // Update all pointers.
    /* MAYBE DON'T MAINTAIN THE INVARIANT -> MAKE CHANGES TO ALL ACCORIDNGLY */
  }

  // I will have to adjust the parent's last and first child pointers
  // If the leaf that I just deleted was a last/first child
  // Get the parent of the leaf
  const auto parent_index        = get_parent(leaf_index);
  const auto parent_chunk_id     = parent_index >> CHUNK_SHIFT;
  const auto parent_chunk_offset = parent_index & CHUNK_MASK;

  // If the parent has only one child, just clear the pointers
  if (get_first_child(parent_index) == leaf_index) {
    if (parent_chunk_offset == 0) {
      pointers_stack[parent_chunk_id].set_first_child_l(INVALID);
    } else {
      pointers_stack[parent_chunk_id].set_first_child_s_at(parent_chunk_offset - 1, INVALID);
    }
  } else {
    if (get_last_child(parent_index) == leaf_index) {
      if (parent_chunk_offset == 0) {
        // Set the last child long pointer to this chunk. Unless
        // This chunk is now vacant, in that case go to the prev sibling
        pointers_stack[parent_chunk_id].set_last_child_l(prev_sibling_id >> CHUNK_SHIFT);
      } else {
        // Set the last child short pointer to this chunk. Unless
        // This chunk is now vacant, in that case go to the prev sibling
        assert(_fits_in_short_del(parent_chunk_id, prev_sibling_id >> CHUNK_SHIFT));
        pointers_stack[parent_chunk_id].set_last_child_s_at(parent_chunk_offset - 1,
                                                            (prev_sibling_id >> CHUNK_SHIFT) - parent_chunk_id);
      }
    } else {
      // Do nothing!! This child was not contributing to any
      // bookkeeping the parent pointers chunk
    }
  }
}

}  // namespace hhds
