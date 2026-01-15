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

// Inter-graph Vid encoding:
// - bit 63: negative flag (1 => this Vid refers to a vertex in another graph)
// - bits [62:44]: graph id (Gid)
// - bits [43:0]: local vid (node/pin id + driver/sink bit)
// Layout: 1 + (64 - 1 - (Nid_bits + 2)) + (Nid_bits + 2) = 64
static constexpr int Vid_flag_bits  = 2;                // bit0: pin/node, bit1: driver/sink
static constexpr int Vid_local_bits = Nid_bits + 2;     // 44
static constexpr int Gid_bits       = 64 - 1 - Vid_local_bits;  // 19

static constexpr uint64_t Vid_negative_mask = 1ULL << 63;
static constexpr int      Vid_gid_shift     = Vid_local_bits;

static constexpr uint64_t Vid_local_mask = (Vid_local_bits == 64) ? ~0ULL : ((1ULL << Vid_local_bits) - 1ULL);
static constexpr uint64_t Vid_gid_mask   = (Gid_bits == 64) ? ~0ULL : ((1ULL << Gid_bits) - 1ULL);

[[nodiscard]] static constexpr inline bool vid_is_negative(Vid v) noexcept { return (v & Vid_negative_mask) != 0; }
[[nodiscard]] static constexpr inline Vid  vid_clear_negative(Vid v) noexcept { return v & ~Vid_negative_mask; }
[[nodiscard]] static constexpr inline Gid  vid_get_gid(Vid v) noexcept {
	v = vid_clear_negative(v);
	return (v >> Vid_gid_shift) & Vid_gid_mask;
}
[[nodiscard]] static constexpr inline Vid vid_get_local(Vid v) noexcept {
	v = vid_clear_negative(v);
	return v & Vid_local_mask;
}
[[nodiscard]] static constexpr inline Vid vid_make_remote(Gid gid, Vid local_vid) noexcept {
	return Vid_negative_mask | ((gid & Vid_gid_mask) << Vid_gid_shift) | (local_vid & Vid_local_mask);
}

static constexpr Nid     Nid_invalid  = (static_cast<Nid>(1) << Nid_bits) - 1;
static constexpr Port_id Port_invalid = (static_cast<Port_id>(1) << Port_bits) - 1;
static constexpr Gid     Gid_invalid  = (static_cast<Gid>(1) << Gid_bits) - 1;

}  // namespace hhds
