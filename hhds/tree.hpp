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

static constexpr Tree_pos INVALID = 0;                     // ROOT ID
static constexpr Tree_pos ROOT = 0;                        // ROOT ID
static constexpr short CHUNK_BITS = 43;                    // Number of chunks in a tree node
static constexpr short SHORT_DELTA = 21;                   // Amount of short delta allowed
static constexpr short MAX_OFFSET = 3;                     // The number of bits in a chunk offset
static constexpr short CHUNK_SIZE = 1 << MAX_OFFSET;       // Size of a chunk in bits
static constexpr short CHUNK_MASK = CHUNK_SIZE - 1;        // Mask for chunk offset
static constexpr short NUM_SHORT_DEL = CHUNK_MASK;         // Mask for chunk offset
static constexpr uint64_t MAX_TREE_SIZE = 1 << CHUNK_BITS; // Maximum number of nodes in the tree

class __attribute__((packed, aligned(512))) Tree_pointers {
private:
    // We only store the exact ID of parent
    Tree_pos parent                 : CHUNK_BITS + MAX_OFFSET;
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

    // Getters
    Tree_pos get_parent() const { return parent; }
    Tree_pos get_next_sibling() const { return next_sibling; }
    Tree_pos get_prev_sibling() const { return prev_sibling; }
    Tree_pos get_first_child_l() const { return first_child_l; }
    Tree_pos get_last_child_l() const { return last_child_l; }

    // Public getters for short child pointers
    Tree_pos get_first_child_s_at(short index) const { return get_first_child_s(index); }
    Tree_pos get_last_child_s_at(short index) const { return get_last_child_s(index); }

    // Setters
    void set_parent(Tree_pos p) { parent = p; }
    void set_next_sibling(Tree_pos ns) { next_sibling = ns; }
    void set_prev_sibling(Tree_pos ps) { prev_sibling = ps; }
    void set_first_child_l(Tree_pos fcl) { first_child_l = fcl; }
    void set_last_child_l(Tree_pos lcl) { last_child_l = lcl; }

    // Public setters for short child pointers
    void set_first_child_s_at(short index, Tree_pos fcs) { set_first_child_s(index, fcs); }
    void set_last_child_s_at(short index, Tree_pos lcs) { set_last_child_s(index, lcs); }

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
    [[nodiscard]] constexpr bool is_invalid() const { return parent == INVALID; }
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
// :private

public:
    /**
     *  Query based API (no updates)
    /*

    /**
     * @brief Get absolute ID of the last child of a node.
     * 
     * @param parent_index The absolute ID of the parent node.
     * @return Tree_pos The absolute ID of the last child, INVALID if none
    */
    Tree_pos get_last_child(const Tree_pos& parent_index) {
        const auto chunk_id = (parent_index >> MAX_OFFSET);
        const auto chunk_offset = (parent_index & CHUNK_MASK);
        const auto last_child_s_i = pointers_stack[chunk_id].get_last_child_s_at(
                                                            parent_index & CHUNK_MASK
                                                        );

        if (chunk_offset) {
            
        } else {
            const auto last_child_l = pointers_stack[chunk_id].get_last_child_l();
        }

        return INVALID;
    }

    Tree_pos get_first_child(const Tree_pos& parent_index) {
        auto chunk_id = (parent_index >> 3);
        auto first_child_s = pointers_stack[chunk_id].get_first_child_s();

        // Try checking if we stored it in a short delta
        for (const auto& delta : first_child_s) {
            if (delta.delta != 0) {
                auto child_chunk_id = chunk_id + delta.delta;
                assert(pointers_stack[child_chunk_id].get_parent() == parent_index);
                // NOTE: The first_child is always the first in the cluster
                return child_chunk_id;
            }
        }

        // If it's not in the short delta, it's in the long one.
        auto first_child_l = pointers_stack[chunk_id].get_first_child_l();

        // No need to verify offset, first one has to be the first_child
        return first_child_l;
    }

    bool is_last_child(const Tree_pos& self_index) {
        auto chunk_id = (self_index >> 3);
        auto parent_abs_id = pointers_stack[chunk_id].get_parent();

        // It has to be the parent’s last child
        return get_last_child(parent_abs_id) == self_index;
    }

    bool is_first_child(const Tree_pos& self_index) {
        auto chunk_id = (self_index >> 3);
        auto parent_abs_id = pointers_stack[chunk_id].get_parent();

        // It has to be the parent’s first child
        return get_first_child(parent_abs_id) == self_index;
    }

    Tree_pos get_sibling_next(const Tree_pos& sibling_id) {
        auto chunk_id = (sibling_id >> 3);
        auto parent_abs_id = pointers_stack[chunk_id].get_parent();
        auto last_child_id = get_last_child(parent_abs_id);

        // If this is the last child, no next sibling
        if (sibling_id == last_child_id) {
            return INVALID;
        }

        // If there are other options within the chunk
        if ((sibling_id - chunk_id < ((1 << 3) - 1)) && !pointers_stack[sibling_id + 1].is_invalid()) {
            return sibling_id + 1;
        }

        // Just jump to the next sibling chunk, or returns invalid
        auto next_sib_chunk = pointers_stack[sibling_id].get_next_sibling();
        return (next_sib_chunk != 0 ? next_sib_chunk : INVALID);
    }

    Tree_pos get_sibling_prev(const Tree_pos& sibling_id) {
        auto chunk_id = (sibling_id >> 3);
        auto parent_abs_id = pointers_stack[chunk_id].get_parent();
        auto first_child_id = get_first_child(parent_abs_id);

        // If this is the first child, no previous sibling
        if (sibling_id == first_child_id) {
            return INVALID;
        }

        // If there are other options within the chunk
        if ((sibling_id - chunk_id) && !pointers_stack[sibling_id - 1].is_invalid()) {
            return sibling_id - 1;
        }

        // Just jump to the previous sibling chunk, or returns invalid
        auto prev_sib_chunk = pointers_stack[sibling_id].get_prev_sibling();
        return (prev_sib_chunk != 0 ? prev_sib_chunk : INVALID);
    }

    int get_tree_width(const int& level) {
        // Placeholder: Implement the actual width calculation using BFS or additional bookkeeping
        return 0;
    }
}
// :public

} // hhds namespace