// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// Case-SENSITIVE string hashing/equality for name-keyed maps (module names,
// IO-pin names). LiveHD/Pyrope match names case-sensitively, so two spellings
// that differ only in letter case are DISTINCT names. The functors are
// transparent (is_transparent) so string_view lookups never allocate a
// std::string.

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace hhds {

// FNV-1a over the raw bytes: deterministic (identical across runs, platforms
// and libraries — unlike std::hash) and allocation-free. Used for the stable,
// cross-library name->gid hash, so the SAME spelling maps to the SAME gid
// everywhere (a future cross-library merge is a no-op for matching names).
[[nodiscard]] inline uint64_t name_hash(std::string_view s) {
  uint64_t h = 1469598103934665603ull;
  for (char c : s) {
    h ^= static_cast<unsigned char>(c);
    h *= 1099511628211ull;
  }
  return h;
}

[[nodiscard]] inline bool name_equal(std::string_view a, std::string_view b) { return a == b; }

// Transparent functors for std::string-keyed containers
// (ankerl::unordered_dense::map / std::unordered_map). is_transparent enables
// string_view lookups without allocating an std::string.
struct Name_hash {
  using is_transparent = void;
  [[nodiscard]] uint64_t operator()(std::string_view s) const { return name_hash(s); }
};
struct Name_eq {
  using is_transparent = void;
  [[nodiscard]] bool operator()(std::string_view a, std::string_view b) const { return name_equal(a, b); }
};

}  // namespace hhds
