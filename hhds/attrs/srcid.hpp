// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstdint>
#include <string>

#include "hhds/attr.hpp"

namespace hhds::attrs {

// Source-provenance id (hhds-srcloc): one Source_locator SourceId per node/pin
// instead of a filename string + span struct. The value is resolved through the
// artifact's Source_locator (own entries first, then the library/forest base).
// Save/merge paths rewrite this attribute's values when a true hash collision
// forces an id remap, which is why hhds owns the tag.
//
// Contract: an id stored on a graph/tree must have been minted through THAT
// artifact's own Source_locator (or already live in the shared base) — the
// collision remap at save time is applied per artifact, so an id borrowed from
// another artifact's un-folded delta would silently miss the rewrite.
struct srcid_t {
  using value_type = uint64_t;
  using storage    = hhds::flat_storage;
};

inline constexpr srcid_t srcid{};

}  // namespace hhds::attrs

namespace hhds {

template <>
[[nodiscard]] inline std::string attr_tag_name<attrs::srcid_t>() {
  return "hhds::attrs::srcid";
}

namespace detail {
// Pre-register the tag so load_attr_stores can deserialize bodies that carry
// it even when nothing in the process touched the attribute beforehand.
inline const bool srcid_tag_registered = []() {
  register_attr_tag<attrs::srcid_t>("hhds::attrs::srcid");
  return true;
}();
}  // namespace detail

}  // namespace hhds
