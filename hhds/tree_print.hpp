// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hhds {

enum class Statement_class : uint8_t {
  Node = 0,     // %pos = type @(attrs)
  Assign,       // %pos = assign @(attrs)
  Attr,         // attr @(attrs)
  Open_call,    // type instance { ... } (scope, parent access)
  Closed_call,  // type instance { ... } (scope, no parent access)
  Open_def,     // def type instance { ... }
  Closed_def,   // def type instance { ... }
  End,          // implicit in tree (closing brace)
  Use           // use @(attrs)
};

struct Type_entry {
  std::string_view name;
  Statement_class  sclass = Statement_class::Node;
};

}  // namespace hhds
