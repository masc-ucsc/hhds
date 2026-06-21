// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// ASCII case-insensitive string hashing/equality for name-keyed maps (module
// names, IO-pin names). LiveHD/Pyrope match names case-insensitively while
// preserving the original spelling: the first key inserted wins as the stored
// key. Only 'A'..'Z' fold; digits, '_', '.', and backticks are untouched.

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace hhds {

[[nodiscard]] inline char ci_ascii_tolower(char c) {
  return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

[[nodiscard]] inline bool ci_str_equal(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) {
    return false;
  }
  for (size_t i = 0; i < a.size(); ++i) {
    if (ci_ascii_tolower(a[i]) != ci_ascii_tolower(b[i])) {
      return false;
    }
  }
  return true;
}

// FNV-1a over ASCII-lowercased bytes: case-insensitive, allocation-free.
[[nodiscard]] inline uint64_t ci_str_hash(std::string_view s) {
  uint64_t h = 1469598103934665603ull;
  for (char c : s) {
    h ^= static_cast<unsigned char>(ci_ascii_tolower(c));
    h *= 1099511628211ull;
  }
  return h;
}

// Transparent functors for case-insensitive std::string-keyed containers
// (ankerl::unordered_dense::map / std::unordered_map). is_transparent enables
// string_view lookups without allocating an std::string.
struct Ci_hash {
  using is_transparent = void;
  [[nodiscard]] uint64_t operator()(std::string_view s) const { return ci_str_hash(s); }
};
struct Ci_eq {
  using is_transparent = void;
  [[nodiscard]] bool operator()(std::string_view a, std::string_view b) const { return ci_str_equal(a, b); }
};

}  // namespace hhds
