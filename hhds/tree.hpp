//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// tree.hpp
#include <sys/types.h>
#include <sys/stat.h>

#include <stdexcept>
#include <cassert>

#include <cstdint>
#include <functional>
#include <iostream>
#include <algorithm>
#include <array>
#include <cstdint>
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

using Tree_pos = uint64_t;
using Short_delta = int32_t;

static constexpr Tree_pos INVALID = 0;                                  // This is invalid for all pointers other than parent
static constexpr Tree_pos ROOT = 0;                                     // ROOT ID

static constexpr short CHUNK_BITS = 43;                                 // Number of chunks in a tree node
static constexpr short SHORT_DELTA = 21;                                // Amount of short delta allowed

static constexpr short CHUNK_SHIFT = 3;                                 // The number of bits in a chunk offset
static constexpr short CHUNK_SIZE = 1 << CHUNK_SHIFT;                   // Size of a chunk in bits
static constexpr short CHUNK_MASK = CHUNK_SIZE - 1;                     // Mask for chunk offset
static constexpr short NUM_SHORT_DEL = CHUNK_MASK;                      // Number of short delta children in eachc tree_ptr

static constexpr uint64_t MAX_TREE_SIZE = 1LL << CHUNK_BITS;            // Maximum number of nodes in the tree

static constexpr Short_delta MIN_SHORT_DELTA = -(1 << SHORT_DELTA);     // Minimum value for short delta
static constexpr Short_delta MAX_SHORT_DELTA = (1 << SHORT_DELTA) - 1;  // Maximum value for short delta

class __attribute__((packed, aligned(64))) Tree_pointers {
private:
    // We only store the exact ID of parent
    Tree_pos parent                     : CHUNK_BITS + CHUNK_SHIFT;
    Tree_pos next_sibling               : CHUNK_BITS;
    Tree_pos prev_sibling               : CHUNK_BITS;

    // Long child pointers
    Tree_pos first_child_l              : CHUNK_BITS;
    Tree_pos last_child_l               : CHUNK_BITS;

    // Short (delta) child pointers
    Short_delta first_child_s_0         : SHORT_DELTA;
    Short_delta first_child_s_1         : SHORT_DELTA;
    Short_delta first_child_s_2         : SHORT_DELTA;
    Short_delta first_child_s_3         : SHORT_DELTA;
    Short_delta first_child_s_4         : SHORT_DELTA;
    Short_delta first_child_s_5         : SHORT_DELTA;
    Short_delta first_child_s_6         : SHORT_DELTA;

    Short_delta last_child_s_0          : SHORT_DELTA;
    Short_delta last_child_s_1          : SHORT_DELTA;
    Short_delta last_child_s_2          : SHORT_DELTA;
    Short_delta last_child_s_3          : SHORT_DELTA;
    Short_delta last_child_s_4          : SHORT_DELTA;
    Short_delta last_child_s_5          : SHORT_DELTA;
    Short_delta last_child_s_6          : SHORT_DELTA;

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
            
            default:
            throw std::out_of_range("Invalid index for first_child_s");
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
            
            default:
            throw std::out_of_range("Invalid index for first_child_s");
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
            
            default:
            throw std::out_of_range("Invalid index for last_child_s");
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

            default: 
            throw std::out_of_range("Invalid index for last_child_s");
        }
    }

// :private

public:
    /* DEFAULT CONSTRUCTOR */
    // Parent can be = 0 for someone, but it can never be MAX_TREE_SIZE
    // That is why the best way to invalidate is to set it to MAX_TREE_SIZE
    // for every other entry INVALID is the best choice
    Tree_pointers()
        : parent(MAX_TREE_SIZE), next_sibling(INVALID), prev_sibling(INVALID),
          first_child_l(INVALID), last_child_l(INVALID) {
            for (short i = 0; i < NUM_SHORT_DEL; i++) {
                _set_first_child_s(i, INVALID);
                _set_last_child_s(i, INVALID);
            }
        }

    /* PARAM CONSTRUCTOR */
    Tree_pointers(Tree_pos p)
        : parent(p), 
            next_sibling(INVALID), prev_sibling(INVALID), 
            first_child_l(INVALID), last_child_l(INVALID) {
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
    Short_delta get_first_child_s_at(short index) const { 
        return _get_first_child_s(index); 
    }
    Short_delta get_last_child_s_at(short index) const { 
        return _get_last_child_s(index); 
    }

    // Setters
    void set_parent(Tree_pos p) { parent = p; }
    void set_next_sibling(Tree_pos ns) { next_sibling = ns; }
    void set_prev_sibling(Tree_pos ps) { prev_sibling = ps; }
    void set_first_child_l(Tree_pos fcl) { first_child_l = fcl; }
    void set_last_child_l(Tree_pos lcl) { last_child_l = lcl; }

    // Setters for short child pointers
    void set_first_child_s_at(short index, Short_delta fcs) { 
        _set_first_child_s(index, fcs); 
    }
    void set_last_child_s_at(short index, Short_delta lcs) { 
        _set_last_child_s(index, lcs); 
    }

    // Operators
    constexpr bool operator==(const Tree_pointers& other) const {
        for (short i = 0; i < 7; i++) {
            if (_get_first_child_s(i) != other._get_first_child_s(i) ||
                _get_last_child_s(i) != other._get_last_child_s(i)) {
                return false;
            }
        }
        return parent == other.parent && next_sibling == other.next_sibling &&
               prev_sibling == other.prev_sibling && first_child_l == other.first_child_l &&
               last_child_l == other.last_child_l;
    }

    constexpr bool operator!=(const Tree_pointers& other) const { return !(*this == other); }
    void invalidate() { parent = MAX_TREE_SIZE; }

    // Checkers
    [[nodiscard]] constexpr bool is_invalid() const { return parent == MAX_TREE_SIZE; }
    [[nodiscard]] constexpr bool is_valid() const { return parent != MAX_TREE_SIZE; }
// :public

}; // Tree_pointers class

template <typename X>
class tree {
private:
    /* The tree pointers and data stored separately */
    std::vector<Tree_pointers>  pointers_stack;
    std::vector<X>              data_stack;

    /* Special functions for sanity */
    [[nodiscard]] bool _check_idx_exists(const Tree_pos &idx) const noexcept {
        return idx >= 0 && idx < static_cast<Tree_pos>(pointers_stack.size());
    }
    [[nodiscard]] bool _contains_data(const Tree_pos &idx) const noexcept {
        /* CHANGE THE SECOND CONDITION
        CAN USE STD::OPTIONAL WRAPPING AROUND THE 
        TEMPLATE OF X */
        return (idx < data_stack.size() && data_stack[idx]);
    }

    /* Function to add an entry to the pointers and data stack (typically for add/append)*/
    [[nodiscard]] Tree_pos _create_space(const Tree_pos &parent_index, const X& data) {
        if (pointers_stack.size() >= MAX_TREE_SIZE) {
            throw std::out_of_range("Tree size exceeded");
        } else if (!_check_idx_exists(parent_index)) {
            throw std::out_of_range("Parent index out of range");
        }

        // Make space for CHUNK_SIZE number of entries at the end
        data_stack.emplace_back(data);
        for (int i = 0; i < CHUNK_OFFSET; i++) {
            data_stack.emplace_back();
        }

        // Add the single pointer node for all CHUNK_SIZE entries
        pointers_stack.emplace_back(parent_index);

        return (data_stack.size() - CHUNK_SIZE) >> CHUNK_SHIFT;
    }

    /* Function to insert a new chunk in between (typically for handling add/append corner cases)*/
    [[nodiscard]] Tree_pos _insert_chunk_after(const Tree_pos curr) {
        if (pointers_stack.size() >= MAX_TREE_SIZE) {
            throw std::out_of_range("Tree size exceeded");
        } else if (!_check_idx_exists(curr)) {
            throw std::out_of_range("Current index out of range");
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

        return new_chunk_id;
    }

    /* Helper function to check if we can fit something in the short delta*/
    [[nodiscard]] bool _fits_in_short_del(const Tree_pos parent_id, const Tree_pos child_id) {
        const auto delta = child_id - parent_id;
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
        const auto old_chunk_id = (abs_id >> CHUNK_SHIFT);
        const auto old_chunk_offset = (abs_id & CHUNK_MASK);
        
        // Insert a blank chunk after the current chunk
        bool requires_new_chunk = true;
        bool retval_set = false;
        short new_chunk_offset = 0;
        Tree_pos new_chunk_id, retval; /* To store the chunk that is being populated */

        for (short offset = old_chunk_offset; offset < NUM_SHORT_DEL; offset++) {
            const auto curr_id = (old_chunk_id << CHUNK_SHIFT) + offset;

            // Get first and last child abs id
            const auto fc = get_first_child(curr_id);
            const auto lc = get_last_child(curr_id);
    
            if (!requires_new_chunk) {
                // Try fitting first and last child in the short delta
                if (_fits_in_short_del(curr_id, fc) and _fits_in_short_del(curr_id, lc)) {
                    pointers_stack[old_chunk_id].set_first_child_s_at(new_chunk_offset, fc - curr_id);
                    pointers_stack[old_chunk_id].set_last_child_s_at(new_chunk_offset, lc - curr_id);
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
            new_chunk_id = _insert_chunk_after(old_chunk_id);
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
                retval = new_chunk_id << CHUNK_SHIFT;
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
// :private

public:
    /**
     *  Query based API (no updates)
    */
    Tree_pos get_last_child(const Tree_pos& parent_index);
    Tree_pos get_first_child(const Tree_pos& parent_index);
    bool is_last_child(const Tree_pos& self_index);
    bool is_first_child(const Tree_pos& self_index);
    Tree_pos get_sibling_next(const Tree_pos& sibling_id);
    Tree_pos get_sibling_prev(const Tree_pos& sibling_id);
    int get_tree_width(const int& level);


    /**
     *  Update based API (Adds and Deletes from the tree)
     */
    Tree_pos append_sibling(const Tree_pos& sibling_id, const X& data);
    Tree_pos add_child(const Tree_pos& parent_index, const X& data);
// :public

}; // tree class

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
Tree_pos tree<X>::get_last_child(const Tree_pos& parent_index) {
    if (!_check_idx_exists(parent_index)) {
        throw std::out_of_range("Parent index out of range");
    }

    const auto chunk_id = (parent_index >> CHUNK_SHIFT);
    const auto chunk_offset = (parent_index & CHUNK_MASK);
    const auto last_child_s_i = pointers_stack[chunk_id].get_last_child_s_at(chunk_offset);

    Tree_pos child_chunk_id = INVALID;
    if (chunk_offset and (last_child_s_i != INVALID)) {
        // If the short delta contains a value, go to this nearby chunk
        child_chunk_id = chunk_id + last_child_s_i;
    } else {
        // The first entry will always have the chunk id of the child
        child_chunk_id = pointers_stack[chunk_id].get_last_child_l();
    }

    // Iterate in reverse to find the last occupied in child chunk
    if (child_chunk_id != INVALID) {
        for (short offset = NUM_SHORT_DEL - 1; offset >= 0; offset--) {
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
        throw std::out_of_range("Parent index out of range");
    }

    const auto chunk_id = (parent_index >> CHUNK_SHIFT);
    const auto chunk_offset = (parent_index & CHUNK_MASK);
    const auto last_child_s_i = pointers_stack[chunk_id].get_last_child_s_at(chunk_offset);

    Tree_pos child_chunk_id = INVALID;
    if (chunk_offset and (last_child_s_i != INVALID)) {
        // If the short delta contains a value, go to this nearby chunk
        child_chunk_id = chunk_id + last_child_s_i;
    } else {
        // The first entry will always have the chunk id of the child
        child_chunk_id = pointers_stack[chunk_id].get_last_child_l();
    }

    // Iterate in reverse to find the last occupied in child chunk
    if (child_chunk_id != INVALID) {
        for (short offset = NUM_SHORT_DEL - 1; offset >= 0; offset--) {
            if (_contains_data((child_chunk_id << CHUNK_SHIFT) + offset)) {
                return static_cast<Tree_pos>((child_chunk_id << CHUNK_SHIFT) + offset);
            }
        } 
    }

    return static_cast<Tree_pos>(INVALID);
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
        throw std::out_of_range("Index out of range");
    }

    const auto self_chunk_id = (self_index >> CHUNK_SHIFT);
    const auto self_chunk_offset = (self_index & CHUNK_MASK);

    // If this chunk has a next_sibling pointer, certainly not the last child
    if (pointers_stack[self_chunk_id].get_next_sibling() != INVALID) {
        return false;
    }

    // Now, to be the last child, all entries after this should be invalid
    for (short offset = self_chunk_offset; offset < NUM_SHORT_DEL; offset++) {
        const auto last_child_s_i = pointers_stack[self_chunk_id].get_last_child_s_at(offset);
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
        throw std::out_of_range("Index out of range");
    }

    const auto self_chunk_id = (self_index >> CHUNK_SHIFT);
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
        throw std::out_of_range("Sibling index out of range");
    }

    // If this is the last child, no next sibling
    if (is_last_child(sibling_id)) {
        return INVALID;
    }

    // Check if the next sibling is within the same chunk, at idx + 1
    const auto curr_chunk_id = (sibling_id >> CHUNK_SHIFT);
    const auto curr_chunk_offset = (sibling_id & CHUNK_MASK);
    if (curr_chunk_offset < CHUNK_MASK and _contains_data(curr_chunk_id + curr_chunk_offset + 1)) {
        return static_cast<Tree_pos>(curr_chunk_id + curr_chunk_offset + 1);
    }

    // Just jump to the next sibling chunk, or returns invalid
    return pointers_stack[sibling_id].get_next_sibling(); // default is INVALID
}

/**
 * @brief Get the prev sibling of a node.
 * 
 * @param sibling_id The absolute ID of the sibling node.
 * @return Tree_pos The absolute ID of the prev sibling, INVALID if none
 * 
 * @throws std::out_of_range If the sibling index is out of range
*/
template <typename X>
Tree_pos tree<X>::get_sibling_prev(const Tree_pos& sibling_id) {
    if (!_check_idx_exists(sibling_id)) {
        throw std::out_of_range("Sibling index out of range");
    }

    // If this is the first child, no prev sibling
    if (is_first_child(sibling_id)) {
        return INVALID;
    }

    // Check if the prev sibling is within the same chunk, at idx + 1
    const auto curr_chunk_id = (sibling_id >> CHUNK_SHIFT);
    const auto curr_chunk_offset = (sibling_id & CHUNK_MASK);
    if (curr_chunk_offset > 0 and _contains_data(curr_chunk_id + curr_chunk_offset - 1)) {
        return static_cast<Tree_pos>(curr_chunk_id + curr_chunk_offset - 1);
    }

    // Just jump to the next sibling chunk, or returns invalid
    const auto prev_sibling = pointers_stack[sibling_id].get_prev_sibling();
    
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
    if (!_check_idx_exists(sibling_id)) {
        throw std::out_of_range("Sibling index out of range");
    }

    const auto sibling_chunk_id = (sibling_id >> CHUNK_SHIFT);
    const auto sibling_parent_id = pointers_stack[sibling_chunk_id].get_parent();
    const auto sib_parent_chunk_id = (sibling_parent_id >> CHUNK_SHIFT);
    const auto last_sib_chunk_id = get_last_child(sibling_parent_id);

    // Can fit the sibling in the same chunk
    for (short offset = 0; offset < NUM_SHORT_DEL; offset++) {
        if (!_contains_data((last_sib_chunk_id << CHUNK_SHIFT) + offset)) {
            // Put the data here and update bookkeeping
            data_stack[(last_sib_chunk_id << CHUNK_SHIFT) + offset] = data;
            return static_cast<Tree_pos>((last_sib_chunk_id << CHUNK_SHIFT) + offset);
        }
    }

    // Create a new chunk for the sibling and fill data there 
    const auto new_sib_chunk_id = _create_space(sibling_parent_id, data);
    pointers_stack[new_sib_chunk_id].set_prev_sibling(last_sib_chunk_id);
    pointers_stack[last_sib_chunk_id].set_next_sibling(new_sib_chunk_id);

    // Update the last child pointer of the parent
    if ((sibling_parent_id & CHUNK_MASK) == 0) {
        // It is the first in the cluster, so we can fit the chunk id directly
        pointers_stack[sib_parent_chunk_id].set_last_child_l(new_sib_chunk_id);
    } else {
        // Try to fit the delta in the short delta pointers
        if (_fits_in_short_del(sibling_parent_id, new_sib_chunk_id)) {
            pointers_stack[sib_parent_chunk_id].set_last_child_s_at((sibling_parent_id & CHUNK_MASK) - 1, 
                                                                     static_cast<Short_delta>(delta));
        } else {
            // Break the parent chunk from the parent id
            const auto new_parent_chunk_id = _break_chunk_from(sibling_parent_id);

            // Set the last child pointer of the parent to the new parent chunk
            pointers_stack[new_parent_chunk_id].set_last_child_l(new_sib_chunk_id);
        }
    }

    return new_sib_chunk_id << CHUNK_SHIFT;
}

/**
 * @brief Add a chlid to a node at the end of all it's children.
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
        throw std::out_of_range("Parent index out of range");
    }

    const auto last_child_id = get_last_child(parent_index);

    // This is not the first child being added
    if (last_child_id != INVALID) {
        return append_sibling(last_child_id, data);
    }

    const auto child_chunk_id = _create_space(parent_index, data);
    const auto parent_chunk_id = (parent_index >> CHUNK_SHIFT);
    const auto parent_chunk_offset = (parent_index & CHUNK_MASK);

    /* Update the parent's first and last child pointers to this new child */
    if (parent_chunk_offset == 0) {
        // The offset is 0, we can fit the chunk id of the child directly
        pointers_stack[parent_chunk_id].set_first_child_l(child_chunk_id);
        pointers_stack[parent_chunk_id].set_last_child_l(child_chunk_id);
    } else {
        // Try to fit the delta in the short delta pointers
        const auto delta = child_chunk_id - parent_chunk_id;
        if (_fits_in_short_del(parent_index, child_chunk_id)) {
            pointers_stack[parent_chunk_id].set_first_child_s_at(parent_chunk_offset - 1, 
                                                                 static_cast<Short_delta>(delta));
            pointers_stack[parent_chunk_id].set_last_child_s_at(parent_chunk_offset - 1, 
                                                                static_cast<Short_delta>(delta));
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
} // hhds namespace