//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include <limits>
#pragma once

namespace hhds {
    using Port_id                  = uint32_t;  // ports have a set order (a-b != b-a)
    //constexpr int     Port_bits    = 22;
    constexpr int Port_bits = std::numeric_limits<Port_id>::digits - 10; // Inquiry
    constexpr Port_id Port_invalid = ((1 << Port_bits) - 1);

    using Nid                      = uint64_t;  // ports have a set order (a-b != b-a)
    constexpr int Nid_bits = std::numeric_limits<Nid>::digits - 22;
    //constexpr int     Nid_bits     = 42;
    constexpr Port_id Nid_invalid  = ((1 << Port_bits) - 1);
}