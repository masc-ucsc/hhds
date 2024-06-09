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
using namespace std;

using Tree_pos = uint64_t;

static constexpr Tree_pos INVALID = 0;                     // ROOT ID
static constexpr Tree_pos ROOT = 0;                        // ROOT ID
static constexpr short CHUNK_BITS = 43;                    // Number of chunks in a tree node
static constexpr short SHORT_DELTA = 21;                   // Amount of short delta allowed
static constexpr short MAX_OFFSET = 3;                     // The number of bits in a chunk offset
static constexpr short CHUNK_SIZE = 1 << MAX_OFFSET;       // Size of a chunk in bits
static constexpr short CHUNK_MASK = CHUNK_SIZE - 1;        // Mask for chunk offset
static constexpr uint64_t MAX_TREE_SIZE = 1LL << CHUNK_BITS; // Maximum number of nodes in the tree

struct __attribute__((packed)) Tree_pointers {
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
    } first_child_s[CHUNK_SIZE - 1], last_child_s[CHUNK_SIZE - 1];

    // Gives 70 bits
    // struct __attribute__((packed)) short_child {
    //     Tree_pos delta : SHORT_DELTA;
    // } first_child_s[CHUNK_SIZE - 1], last_child_s[CHUNK_SIZE - 1];

    // Short deltas
    // These give 64 bit alignment
    // Tree_pos first_child_s_0 : SHORT_DELTA;
    // Tree_pos first_child_s_1 : SHORT_DELTA;
    // Tree_pos first_child_s_2 : SHORT_DELTA;
    // Tree_pos first_child_s_3 : SHORT_DELTA;
    // Tree_pos first_child_s_4 : SHORT_DELTA;
    // Tree_pos first_child_s_5 : SHORT_DELTA;
    // Tree_pos first_child_s_6 : SHORT_DELTA;

    // Tree_pos last_child_s_0 : SHORT_DELTA;
    // Tree_pos last_child_s_1 : SHORT_DELTA;
    // Tree_pos last_child_s_2 : SHORT_DELTA;
    // Tree_pos last_child_s_3 : SHORT_DELTA;
    // Tree_pos last_child_s_4 : SHORT_DELTA;
    // Tree_pos last_child_s_5 : SHORT_DELTA;
    // Tree_pos last_child_s_6 : SHORT_DELTA;

    // Gives 70 bytes, 3 for each arr
    // std::array<short_child, CHUNK_MASK> first_child_s;
    // std::array<short_child, CHUNK_MASK> last_child_s;

    // Gives 
    // uint_32t first_child_s[CHUNK_MASK] : SHORT_DELTA;
    // uint_32t last_child_s[CHUNK_MASK] : SHORT_DELTA;

    Tree_pointers()
        : parent(MAX_TREE_SIZE), next_sibling(0), prev_sibling(0),
          first_child_l(0), last_child_l(0) {

        // Fill first_child_s and last_child_s
        // for (int i = 0; i < CHUNK_SIZE - 1; i++) {
        //     first_child_s[i].delta = 0;
        //     last_child_s[i].delta = 0;
        // }

        // std::fill(first_child_s.begin(), first_child_s.end(), short_child{0});
        // std::fill(last_child_s.begin(), last_child_s.end(), short_child{0});
        
        // first_child_s_0 = 0;
        // first_child_s_1 = 0;
        // first_child_s_2 = 0;
        // first_child_s_3 = 0;
        // first_child_s_4 = 0;
        // first_child_s_5 = 0;
        // first_child_s_6 = 0;

        // last_child_s_0 = 0;
        // last_child_s_1 = 0;
        // last_child_s_2 = 0;
        // last_child_s_3 = 0;
        // last_child_s_4 = 0;
        // last_child_s_5 = 0;
        // last_child_s_6 = 0;

        // for (int i = 0; i < CHUNK_MASK; i++) {
        //     first_child_s[i] = 0;
        //     last_child_s[i] = 0;
        // }
    }
};

int main() {
    
    Tree_pointers test;
    cout << sizeof(test) << endl;
    // cout << sizeof(test.first_child_s[0]) << endl;

    return 0;
}