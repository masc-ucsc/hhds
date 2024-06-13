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
 * NEVER let it exceed 512 bits.
*/

using Tree_pos = uint64_t;

static constexpr Tree_pos INVALID = 0;                          // ROOT ID
static constexpr Tree_pos ROOT = 0;                             // ROOT ID
static constexpr short CHUNK_BITS = 43;                         // Number of chunks in a tree node
static constexpr short SHORT_DELTA = 21;                        // Amount of short delta allowed
static constexpr short CHUNK_SHIFT = 3;                         // The number of bits in a chunk offset
static constexpr short CHUNK_SIZE = 1 << CHUNK_SHIFT;           // Size of a chunk in bits
static constexpr short CHUNK_MASK = CHUNK_SIZE - 1;             // Mask for chunk offset
static constexpr short NUM_SHORT_DEL = CHUNK_MASK;              // Mask for chunk offset
static constexpr uint64_t MAX_TREE_SIZE = 1LL << CHUNK_BITS;    // Maximum number of nodes in the tree

class __attribute__((packed, aligned(64))) Tree_pointers {
private:
    // We only store the exact ID of parent
    Tree_pos parent                 : CHUNK_BITS + CHUNK_SHIFT;
    Tree_pos next_sibling           : CHUNK_BITS;
    Tree_pos prev_sibling           : CHUNK_BITS;

    // Long child pointers
    Tree_pos first_child_l          : CHUNK_BITS;
    Tree_pos last_child_l           : CHUNK_BITS;

    // Short (delta) child pointers
    Tree_pos first_child_s_0        : SHORT_DELTA;
    Tree_pos first_child_s_1        : SHORT_DELTA;
    Tree_pos first_child_s_2        : SHORT_DELTA;
    Tree_pos first_child_s_3        : SHORT_DELTA;
    Tree_pos first_child_s_4        : SHORT_DELTA;
    Tree_pos first_child_s_5        : SHORT_DELTA;
    Tree_pos first_child_s_6        : SHORT_DELTA;

    Tree_pos last_child_s_0         : SHORT_DELTA;
    Tree_pos last_child_s_1         : SHORT_DELTA;
    Tree_pos last_child_s_2         : SHORT_DELTA;
    Tree_pos last_child_s_3         : SHORT_DELTA;
    Tree_pos last_child_s_4         : SHORT_DELTA;
    Tree_pos last_child_s_5         : SHORT_DELTA;
    Tree_pos last_child_s_6         : SHORT_DELTA;

    // Helper function to access first child pointers by index
    Tree_pos _get_first_child_s(short index) const {
        switch (index) {
            case 0: return first_child_s_0;
            case 1: return first_child_s_1;
            case 2: return first_child_s_2;
            case 3: return first_child_s_3;
            case 4: return first_child_s_4;
            case 5: return first_child_s_5;
            case 6: return first_child_s_6;
            default: throw std::out_of_range("Invalid index for first_child_s");
        }
    }

    void _set_first_child_s(short index, Tree_pos value) {
        switch (index) {
            case 0: first_child_s_0 = value; break;
            case 1: first_child_s_1 = value; break;
            case 2: first_child_s_2 = value; break;
            case 3: first_child_s_3 = value; break;
            case 4: first_child_s_4 = value; break;
            case 5: first_child_s_5 = value; break;
            case 6: first_child_s_6 = value; break;
            default: throw std::out_of_range("Invalid index for first_child_s");
        }
    }

    // Helper function to access last child pointers by index
    Tree_pos _get_last_child_s(short index) const {
        switch (index) {
            case 0: return last_child_s_0;
            case 1: return last_child_s_1;
            case 2: return last_child_s_2;
            case 3: return last_child_s_3;
            case 4: return last_child_s_4;
            case 5: return last_child_s_5;
            case 6: return last_child_s_6;
            default: throw std::out_of_range("Invalid index for last_child_s");
        }
    }

    void _set_last_child_s(short index, Tree_pos value) {
        switch (index) {
            case 0: last_child_s_0 = value; break;
            case 1: last_child_s_1 = value; break;
            case 2: last_child_s_2 = value; break;
            case 3: last_child_s_3 = value; break;
            case 4: last_child_s_4 = value; break;
            case 5: last_child_s_5 = value; break;
            case 6: last_child_s_6 = value; break;
            default: throw std::out_of_range("Invalid index for last_child_s");
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

    // Public getters for short child pointers
    Tree_pos get_first_child_s_at(short index) const { return _get_first_child_s(index); }
    Tree_pos get_last_child_s_at(short index) const { return _get_last_child_s(index); }

    // Setters
    void set_parent(Tree_pos p) { parent = p; }
    void set_next_sibling(Tree_pos ns) { next_sibling = ns; }
    void set_prev_sibling(Tree_pos ps) { prev_sibling = ps; }
    void set_first_child_l(Tree_pos fcl) { first_child_l = fcl; }
    void set_last_child_l(Tree_pos lcl) { last_child_l = lcl; }

    // Public setters for short child pointers
    void set_first_child_s_at(short index, Tree_pos fcs) { _set_first_child_s(index, fcs); }
    void set_last_child_s_at(short index, Tree_pos lcs) { _set_last_child_s(index, lcs); }

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
    Tree_pos create_space(const Tree_pos &parent_index, const X& data) {
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
// :private

public:
    /**
     *  Query based API (no updates)
    */

    /**
     * @brief Get absolute ID of the last child of a node.
     * 
     * @param parent_index The absolute ID of the parent node.
     * @return Tree_pos The absolute ID of the last child, INVALID if none
     * 
     * @throws std::out_of_range If the parent index is out of range
    */
    Tree_pos get_last_child(const Tree_pos& parent_index) {
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
                if (_contains_data(child_chunk_id + offset)) {
                    return static_cast<Tree_pos>(child_chunk_id + offset);
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
    Tree_pos get_first_child(const Tree_pos& parent_index) {
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
                if (_contains_data(child_chunk_id + offset)) {
                    return static_cast<Tree_pos>(child_chunk_id + offset);
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
    bool is_last_child(const Tree_pos& self_index) {
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
            if (_contains_data(self_chunk_id + offset)) {
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
    bool is_first_child(const Tree_pos& self_index) {
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
    Tree_pos get_sibling_next(const Tree_pos& sibling_id) {
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
    Tree_pos get_sibling_prev(const Tree_pos& sibling_id) {
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

    int get_tree_width(const int& level) {
        // Placeholder: Implement the actual width calculation using BFS or additional bookkeeping
        return 0;
    }

    /**
     *  Update based API (Adds and Deletes from the tree)
     */

    /**
     * @brief Append a sibling to a node.
     * 
     * @param sibling_id The absolute ID of the sibling node.
     * @param data The data to be stored in the new sibling.
     * 
     * @return Tree_pos The absolute ID of the new sibling.
     * @throws std::out_of_range If the sibling index is out of range
     */
    Tree_pos add_child(const Tree_pos& parent_index, const X& data) {
        if (!_check_idx_exists(parent_index)) {
            throw std::out_of_range("Parent index out of range");
        }

        const auto last_child_id = get_last_child(parent_index);

        // This is not the first child being added
        if (last_child_id != INVALID) {
            return append_sibling(last_child_id, data);
        }

        const auto child_chunk_id = create_space(parent_index, data);
        const auto parent_chunk_id = (parent_index >> CHUNK_SHIFT);
        const auto parent_chunk_offset = (parent_index & CHUNK_MASK);

        /* Update the parent's first and last child pointers to this new child */
        if (parent_chunk_offset == 0) {
            // The offset is 0, we can fit the chunk id of the child directly
            pointers_stack[parent_chunk_id].set_first_child_l(child_chunk_id);
            pointers_stack[parent_chunk_id].set_last_child_l(child_chunk_id);
        } else {
            const auto grand_parent_id = pointers_stack[parent_chunk_id].get_parent();
            auto new_chunk_id = create_space(grand_parent_id, X());

            // Add bookkeeping to the fresh sibling chunk
            pointers_stack[new_chunk_id].set_parent(grand_parent_id);
            pointers_stack[new_chunk_id].set_prev_sibling(parent_chunk_id);
            pointers_stack[new_chunk_id].set_next_sibling(pointers_stack[parent_chunk_id].get_next_sibling());
            pointers_stack[parent_chunk_id].set_next_sibling(new_chunk_id);

            if (pointers_stack[new_chunk_id].get_next_sibling() != INVALID) {
                pointers_stack[pointers_stack[new_chunk_id].get_next_sibling()].set_prev_sibling(new_chunk_id);
            }

            pointers_stack[new_chunk_id].set_first_child_l(child_chunk_id);
            pointers_stack[new_chunk_id].set_last_child_l(child_chunk_id);

            // Move entries from the parent chunk to the new chunk
            for (short offset = parent_chunk_offset; offset < NUM_SHORT_DEL; ++offset) {
                const auto current_child_id = parent_chunk_id + offset;

                if (_contains_data(current_child_id)) {
                    const auto current_child_delta = static_cast<Tree_pos>(current_child_id - new_chunk_id);

                    if (current_child_delta < (1 << SHORT_DELTA)) {
                        // We can fit the delta in short delta pointers
                        pointers_stack[new_chunk_id].set_first_child_s_at(offset, current_child_delta);
                        pointers_stack[new_chunk_id].set_last_child_s_at(offset, current_child_delta);
                    } else {
                        // If we can't fit the delta, move the rest to a new chunk
                        auto next_new_chunk_id = create_space(grand_parent_id, X());

                        pointers_stack[next_new_chunk_id].set_parent(grand_parent_id);
                        pointers_stack[next_new_chunk_id].set_prev_sibling(new_chunk_id);
                        pointers_stack[new_chunk_id].set_next_sibling(next_new_chunk_id);
                        new_chunk_id = next_new_chunk_id;
                        offset = -1;  // Restart the offset loop for the new chunk
                    }
                }
            }

            // Invalidate the moved entries in the parent chunk
            for (short offset = parent_chunk_offset; offset < NUM_SHORT_DEL; ++offset) {
                data_stack[parent_chunk_id + offset] = X();
            }
        }
    }

    // :public
    }; // tree class
} // hhds namespace