#pragma once
#include <cstdint>

namespace hhds {

static constexpr int Nid_bits  = 42;
static constexpr int Port_bits = 22;

using Nid     = uint64_t;
using Pid     = uint64_t;
using Vid     = uint64_t;
using Gid     = uint64_t;
using Type    = uint16_t;
using Port_id = uint32_t;

static constexpr Gid Gid_invalid = static_cast<Gid>(~Gid{0});

// "No such port". The port_id field is Port_bits wide, so the largest value it
// can hold is reserved as the sentinel and can never name a real pin.
static constexpr Port_id Port_invalid = (Port_id{1} << Port_bits) - 1;

}  // namespace hhds
