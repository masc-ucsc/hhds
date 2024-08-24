// See LICENSE.txt for details

#pragma once

#include <cstdint>
// #include "hash_set2.hpp" // emhash2
// #include "hash_set3.hpp" // emhash7
// #include "hash_set4.hpp" // emhash9
#include "hash_set8.hpp"  // emhash8

namespace hhds {

enum class Entry_type : uint8_t { Free = 0, Node, Pin };

using Set = emhash8::HashSet<uint32_t>;
using Nid = uint32_t;
using Pid = uint32_t;

using Oid = uint32_t;  // Internal only for Overflow ID

};  // namespace hhds
