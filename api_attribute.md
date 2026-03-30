# HHDS Attribute API Direction

This document describes the attribute model for HHDS and how downstream
projects such as LiveHD can extend it without editing HHDS core code.

This document complements:

- [`api.md`](/Users/renau/projs/hhds/api.md) for the structural API direction
- [`TODO.md`](/Users/renau/projs/hhds/TODO.md) for the implementation roadmap

## Goals

- Keep HHDS structural.
- Keep metadata external to the core graph/tree structure.
- Allow HHDS to provide a few built-in attributes such as `name`.
- Allow downstream projects to add more attributes such as `bits`, `sign`,
  `offset`, and `delay` without editing HHDS core classes.
- Use the same mechanism for node and pin attributes.
- Preserve HHDS lightweight ID types (`_flat`, `_hier`, `_class`) as storage
  keys and `Node`/`Pin` wrappers as the API surface.

## Non-goals

- HHDS should not require a closed central enum listing every attribute.
- HHDS should not require editing `Node` / `Pin` wrapper classes whenever a
  new downstream attribute is added.
- HHDS should not force all attributes to exist on both nodes and pins.

## Key design decisions

### Node/Pin are the API surface

`Node` and `Pin` are the rich wrappers that carry a `shared_ptr` to `Graph`
(and through it to `GraphLibrary`). They are always the entry point for
attribute access.

The lightweight ID types (`_flat`, `_hier`, `_class`, `_compact`) have no
accessor methods. They are used only for storage and indexing. A `Node`/`Pin`
can be constructed from any lightweight ID plus a `GraphLibrary`.

### Tag objects, not enums

A closed enum is a poor fit for cross-project extension. Instead, attributes
are represented by tag objects with associated type information:

```cpp
namespace hhds::attrs {

struct name_t {
  using value_type = std::string;
  using key_type   = Node_class;   // storage key
};
inline constexpr name_t name{};

struct pin_name_t {
  using value_type = std::string;
  using key_type   = Pin_class;
};
inline constexpr pin_name_t pin_name{};

}  // namespace hhds::attrs
```

Each tag declares:

- `value_type` — the stored data type
- `key_type` — which lightweight ID type keys the storage map

### ADL-based customization

Attribute dispatch uses ADL (argument-dependent lookup) through free functions
with the `hhds_attr_*` prefix. The tag's namespace controls which overloads
ADL finds.

No `tag_invoke`. No virtual dispatch. No closed registry.

### Lazy typed storage on GraphLibrary

`GraphLibrary` provides type-erased lazy storage keyed by tag type. The first
`set()` on any attribute lazily creates the storage map. No explicit
registration step is needed.

## Concept and return type

```cpp
namespace hhds {

template <class Tag>
concept Attribute = requires {
  typename Tag::value_type;
  typename Tag::key_type;
};

template <Attribute Tag>
using attr_result_t = std::conditional_t<
    std::is_trivially_copyable_v<typename Tag::value_type>
        && sizeof(typename Tag::value_type) <= 16,
    typename Tag::value_type,
    const typename Tag::value_type&>;

}  // namespace hhds
```

Trivially copyable types up to 16 bytes are returned by value. Larger or
non-trivial types are returned by `const&`.

## CPO entry points

```cpp
namespace hhds {

inline constexpr struct attr_get_fn {
  template <Attribute Tag, class Owner>
  constexpr attr_result_t<Tag> operator()(Tag tag, const Owner& owner) const {
    return hhds_attr_get(tag, owner);  // ADL
  }
} attr_get{};

inline constexpr struct attr_set_fn {
  template <Attribute Tag, class Owner>
  constexpr void operator()(Tag tag, const Owner& owner,
                            const typename Tag::value_type& value) const {
    hhds_attr_set(tag, owner, value);  // ADL
  }
} attr_set{};

inline constexpr struct attr_has_fn {
  template <Attribute Tag, class Owner>
  constexpr bool operator()(Tag tag, const Owner& owner) const {
    return hhds_attr_has(tag, owner);  // ADL
  }
} attr_has{};

inline constexpr struct attr_del_fn {
  template <Attribute Tag, class Owner>
  constexpr void operator()(Tag tag, const Owner& owner) const {
    hhds_attr_del(tag, owner);  // ADL
  }
} attr_del{};

}  // namespace hhds
```

## Lazy storage on GraphLibrary

```cpp
class GraphLibrary {
  std::unordered_map<std::type_index, std::any> attr_stores_;

public:
  template <Attribute Tag>
  auto& attr_store(Tag) {
    using Map = std::unordered_map<typename Tag::key_type, typename Tag::value_type>;
    auto idx = std::type_index(typeid(Tag));
    auto it = attr_stores_.find(idx);
    if (it == attr_stores_.end()) {
      it = attr_stores_.emplace(idx, Map{}).first;
    }
    return std::any_cast<Map&>(it->second);
  }

  // Clear all entries for one attribute tag
  template <Attribute Tag>
  void attr_clear(Tag) {
    attr_stores_.erase(std::type_index(typeid(Tag)));
  }

  // Clear all attribute maps across all tags
  void attr_clear() {
    attr_stores_.clear();
  }
};
```

Attribute storage is created on first use.

## Naming convention for del vs clear

- `del()` — removes a single entry (one node/pin's attribute)
- `clear(tag)` — removes all entries for one attribute tag across a
  `GraphLibrary` or `Forest`
- `clear()` — removes all attribute maps across all tags

Examples:

```cpp
node.attr(hhds::attrs::name).del();       // delete one node's name
glib->attr_clear(hhds::attrs::name);      // clear all name entries
glib->attr_clear();                       // clear all attribute data
```

## AttrRef proxy

```cpp
namespace hhds {

template <Attribute Tag, class Owner>
class AttrRef {
  const Owner& owner_;
public:
  explicit AttrRef(const Owner& owner) : owner_(owner) {}

  attr_result_t<Tag> get() const { return attr_get(Tag{}, owner_); }
  bool               has() const { return attr_has(Tag{}, owner_); }
  void set(const typename Tag::value_type& v) { attr_set(Tag{}, owner_, v); }
  void del()       { attr_del(Tag{}, owner_); }
};

}  // namespace hhds
```

## Accessor on Node and Pin

`Node` and `Pin` expose a single `attr()` method that returns an `AttrRef`:

```cpp
class Node {
public:
  GraphLibrary& glib() const;
  Node_flat     flat() const;
  Node_hier     hier() const;
  Node_class    nclass() const;

  template <Attribute Tag>
  auto attr(Tag) const { return AttrRef<Tag, Node>(*this); }
};

class Pin {
public:
  GraphLibrary& glib() const;
  Pin_flat      flat() const;
  Pin_hier      hier() const;
  Pin_class     pclass() const;

  template <Attribute Tag>
  auto attr(Tag) const { return AttrRef<Tag, Pin>(*this); }
};
```

## Built-in HHDS attribute implementation

HHDS provides a small number of built-in attributes. Each is implemented as
ADL overloads in the tag's namespace:

```cpp
namespace hhds::attrs {

// --- name (nodes) ---

inline attr_result_t<name_t> hhds_attr_get(name_t, const Node& n) {
  return n.glib().attr_store(name).at(n.nclass());
}

inline void hhds_attr_set(name_t, const Node& n, const std::string& v) {
  n.glib().attr_store(name)[n.nclass()] = v;
}

inline bool hhds_attr_has(name_t, const Node& n) {
  return n.glib().attr_store(name).contains(n.nclass());
}

inline void hhds_attr_del(name_t, const Node& n) {
  n.glib().attr_store(name).erase(n.nclass());
}

// --- name (pins) ---

inline attr_result_t<name_t> hhds_attr_get(name_t, const Pin& p) {
  return p.glib().attr_store(name).at(p.pclass());
}

// ... same pattern for set/has/del on Pin ...

}  // namespace hhds::attrs
```

## Node and pin support

An attribute tag is not tied to exactly one owner kind. A tag may support:

- nodes only
- pins only
- both nodes and pins

Unsupported combinations fail at compile time because no matching ADL overload
exists:

```cpp
node.attr(hhds::attrs::name).get();      // OK
pin.attr(hhds::attrs::name).get();       // OK
pin.attr(hhds::attrs::pin_name).get();   // OK

node.attr(hhds::attrs::pin_name).get();  // compile error: no ADL match
```

## Downstream extension: LiveHD example

LiveHD adds new attributes without modifying HHDS. Each attribute lives in its
own header file.

### File: `livehd/attrs/bits.hpp`

```cpp
#pragma once
#include "hhds/attr.hpp"

namespace livehd::attrs {

struct bits_t {
  using value_type = int;
  using key_type   = hhds::Node_flat;  // keyed globally across hierarchy
};
inline constexpr bits_t bits{};

inline int hhds_attr_get(bits_t, const hhds::Node& n) {
  return n.glib().attr_store(bits).at(n.flat());
}

inline void hhds_attr_set(bits_t, const hhds::Node& n, int v) {
  n.glib().attr_store(bits)[n.flat()] = v;
}

inline bool hhds_attr_has(bits_t, const hhds::Node& n) {
  return n.glib().attr_store(bits).contains(n.flat());
}

inline void hhds_attr_del(bits_t, const hhds::Node& n) {
  n.glib().attr_store(bits).erase(n.flat());
}

}  // namespace livehd::attrs
```

### File: `livehd/attrs/delay.hpp`

```cpp
#pragma once
#include "hhds/attr.hpp"

namespace livehd::attrs {

struct delay_t {
  using value_type = float;
  using key_type   = hhds::Pin_flat;   // delay is per-pin, global
};
inline constexpr delay_t delay{};

inline float hhds_attr_get(delay_t, const hhds::Pin& p) {
  return p.glib().attr_store(delay).at(p.flat());
}

inline void hhds_attr_set(delay_t, const hhds::Pin& p, float v) {
  p.glib().attr_store(delay)[p.flat()] = v;
}

inline bool hhds_attr_has(delay_t, const hhds::Pin& p) {
  return p.glib().attr_store(delay).contains(p.flat());
}

inline void hhds_attr_del(delay_t, const hhds::Pin& p) {
  p.glib().attr_store(delay).erase(p.flat());
}

}  // namespace livehd::attrs
```

### File: `livehd/attrs/sign.hpp`

Same pattern — one tag, ADL overloads, one header.

### File: `livehd/attrs/all.hpp`

Optional umbrella include:

```cpp
#pragma once
#include "livehd/attrs/bits.hpp"
#include "livehd/attrs/delay.hpp"
#include "livehd/attrs/sign.hpp"
```

### Usage in LiveHD code

```cpp
#include "livehd/attrs/bits.hpp"
#include "livehd/attrs/delay.hpp"

node.attr(livehd::attrs::bits).set(32);
int b = node.attr(livehd::attrs::bits).get();   // int by value

pin.attr(livehd::attrs::delay).set(1.5f);
float d = pin.attr(livehd::attrs::delay).get();  // float by value

if (node.attr(livehd::attrs::bits).has()) {
  node.attr(livehd::attrs::bits).del();           // delete single entry
}

// Clear all delay data for this attribute
glib->attr_clear(livehd::attrs::delay);

// Clear all attribute data across all tags
glib->attr_clear();
```

No HHDS files were edited. No registration call.

## HHDS file layout

- `hhds/attr.hpp`
  - `Attribute` concept
  - `attr_result_t`
  - CPOs (`attr_get`, `attr_set`, `attr_has`, `attr_del`)
  - `AttrRef`
- `hhds/attrs/name.hpp`
  - `name_t` tag and ADL overloads
- `hhds/attrs/pin_name.hpp`
  - `pin_name_t` tag and ADL overloads
- `hhds/attrs/all.hpp`
  - optional umbrella include

## Compile-time behavior

The dispatch is compile-time in the sense that:

- the attribute tag type is known statically
- the selected ADL overload is chosen statically
- unsupported owner/tag combinations fail at compile time

The underlying storage lookup is runtime because attributes live in maps.

This is the correct tradeoff:

- extensible API surface
- no closed registry
- no virtual dispatch
- no repeated edits to HHDS core classes
