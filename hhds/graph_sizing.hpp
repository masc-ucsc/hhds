#pragma once
#include <cstdint>

namespace hhds {

static constexpr int Nid_bits = 42;
static constexpr int Port_bits = 22;

using Nid = uint64_t;
using Pid = uint64_t;
using Type = uint16_t;
using Port_id = uint32_t;

static constexpr Nid Nid_invalid = (Nid(1) << Nid_bits) - 1;
static constexpr Port_id Port_invalid = (Port_id(1) << Port_bits) - 1;

} // namespace hhds