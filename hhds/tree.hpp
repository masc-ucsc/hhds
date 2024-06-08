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
    struct __attribute__((packed)) short_child {
        Tree_pos delta : SHORT_DELTA;
    };

    std::array<short_child, CHUNK_MASK> first_child_s;
    std::array<short_child, CHUNK_MASK> last_child_s;
// :private

public:
    /* DEFAULT CONSTRUCTOR */
    Tree_pointers()
        : parent(MAX_TREE_SIZE), next_sibling(0), prev_sibling(0),
          first_child_l(0), last_child_l(0) {
        std::fill(first_child_s.begin(), first_child_s.end(), short_child{0});
        std::fill(last_child_s.begin(), last_child_s.end(), short_child{0});
    }

    /* COPY CONSTRUCTOR */
    Tree_pointers(const Tree_pointers& other)
        : parent(other.parent), next_sibling(other.next_sibling), prev_sibling(other.prev_sibling),
          first_child_l(other.first_child_l), last_child_l(other.last_child_l),
          first_child_s(other.first_child_s), last_child_s(other.last_child_s) {}

    /* PARAM CONSTRUCTOR */
    Tree_pointers(Tree_pos p, Tree_pos ns, Tree_pos ps, Tree_pos fcl, Tree_pos lcl,
                  const std::array<short_child, 7>& fcs, const std::array<short_child, 7>& lcs)
        : parent(p), next_sibling(ns), prev_sibling(ps), first_child_l(fcl), last_child_l(lcl),
          first_child_s(fcs), last_child_s(lcs) {}

    // Getters
    Tree_pos get_parent() const { return parent; }
    Tree_pos get_next_sibling() const { return next_sibling; }
    Tree_pos get_prev_sibling() const { return prev_sibling; }
    Tree_pos get_first_child_l() const { return first_child_l; }
    Tree_pos get_last_child_l() const { return last_child_l; }
    const std::array<short_child, 7>& get_first_child_s() const { return first_child_s; }
    const std::array<short_child, 7>& get_last_child_s() const { return last_child_s; }

    // Setters
    void set_parent(Tree_pos p) { parent = p; }
    void set_next_sibling(Tree_pos ns) { next_sibling = ns; }
    void set_prev_sibling(Tree_pos ps) { prev_sibling = ps; }
    void set_first_child_l(Tree_pos fcl) { first_child_l = fcl; }
    void set_last_child_l(Tree_pos lcl) { last_child_l = lcl; }
    void set_first_child_s(uint32_t idx, Tree_pos fcs) { first_child_s[idx] = short_child{fcs}; }
    void set_last_child_s(uint32_t idx, Tree_pos lcs) { last_child_s[idx] = short_child{lcs}; }

    // Operators
    constexpr bool operator==(const Tree_pointers& other) const {
        return parent == other.parent && next_sibling == other.next_sibling &&
               prev_sibling == other.prev_sibling && first_child_l == other.first_child_l &&
               last_child_l == other.last_child_l && first_child_s == other.first_child_s &&
               last_child_s == other.last_child_s;
    }
    constexpr bool operator!=(const Tree_pointers& other) const { return !(*this == other); }
    void invalidate() { parent = MAX_TREE_SIZE; }

    // Checkers
    [[nodiscard]] constexpr bool is_invalid() const { return parent == MAX_TREE_SIZE; }
// :public

}; // Tree_pointers class

template <typename X>
class tree {
private:
    /* The tree pointers and data stored separately */
    std::vector<Tree_pointers>  pointers_stack;
    std::vector<X>              data_stack;
// :protected

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
        const auto chunk_id = (parent_index >> 3);
        const auto last_child_s = pointers_stack[chunk_id].get_last_child_s();

        // Try checking if we stored it in a short delta
        for (const auto& delta : last_child_s) {
            if (delta.delta != 0) {
                const auto child_chunk_id = chunk_id + delta.delta;
                assert(pointers_stack[child_chunk_id].get_parent() == parent_index);

                // NOTE: The last_child will be the last used offset here
                for (short offset = (1 << 3) - 2; offset >= 0; --offset) {
                    if (!pointers_stack[child_chunk_id + offset].is_invalid()) {
                        return child_chunk_id + offset;
                    }
                }
            }
        }

        // If it’s not in the short delta, it’s in the long one.
        const auto last_child_l = pointers_stack[chunk_id].get_last_child_l();

        for (short offset = CHUNK_MASK; offset >= 0; --offset) {
            if (!pointers_stack[last_child_l + offset].is_invalid()) {
                return last_child_l + offset;
            }
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