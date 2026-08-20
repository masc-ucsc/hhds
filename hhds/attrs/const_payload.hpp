// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#pragma once

#include <string>

#include "hhds/attr.hpp"

namespace hhds::attrs {

// Opaque serialized value carried by a Graph::CONST_NODE driver pin. HHDS
// interns these payloads but does not interpret their encoding.
struct const_payload_t {
  using value_type = std::string;
  using storage    = hhds::flat_storage;
};

inline constexpr const_payload_t const_payload{};

}  // namespace hhds::attrs

namespace hhds {

template <>
[[nodiscard]] inline std::string attr_tag_name<attrs::const_payload_t>() {
  // Default persistence id. A client that already persists this payload under
  // its own identifier may rename it once, before any Graph is persisted, with
  // register_attr_tag<attrs::const_payload_t>("<its id>").
  return "hhds::attrs::const_payload";
}

}  // namespace hhds
