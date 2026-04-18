#pragma once

#include <string>

#include "hhds/attr.hpp"

namespace hhds::attrs {

struct name_t {
  using value_type = std::string;
  using storage    = hhds::flat_storage;
};

inline constexpr name_t name{};

}  // namespace hhds::attrs

namespace hhds {

template <>
[[nodiscard]] inline std::string attr_tag_name<attrs::name_t>() {
  return "hhds::attrs::name";
}

}  // namespace hhds
