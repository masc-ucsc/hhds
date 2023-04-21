// See LICENSE.txt for details

#pragma once

#include <cstdint>

namespace hhds {

enum class Entry_type : uint8_t { Free = 0, Node, Pin, Overflow };

using Nid=uint32_t;
using Pid=uint32_t;

}; // namespace hhds

