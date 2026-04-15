# HHDS TODO: Attribute System

This file tracks the attribute system implementation. It depends on the
structural cleanup in [`todo_first.md`](/Users/renau/projs/hhds/todo_first.md).
API examples live in [`sample.md`](/Users/renau/projs/hhds/sample.md).

## Design decisions (finalized)

### Two storage levels

| Level | Key | Storage location | Map type |
|-------|-----|-----------------|----------|
| `flat_storage` | `Nid` | each `Graph` / `Tree` | `map<Nid, T>` |
| `hier_storage` | `(Tree_pos, Nid)` | each `Graph` / `Tree` (per root) | `map<(Tree_pos, Nid), T>` |

- `Nid` encodes pin-vs-node, so a single map handles both `Node` and `Pin`
  attributes.
- No attribute maps on `GraphLibrary` or `Forest` — all storage is per-graph
  or per-tree.
- Note: the traversal modes (`_class`, `_flat`, `_hier`) are orthogonal to
  storage levels. `_class` and `_flat` traversals both use `flat_storage`
  attributes; `_hier` traversal can use both storage levels.

### Tag declarations

Each attribute is a struct with `value_type` and `storage`:

```cpp
namespace hhds {
  struct flat_storage {};
  struct hier_storage {};
}

// Example: built-in name (flat)
namespace hhds::attrs {
struct name_t {
  using value_type = std::string;
  using storage    = hhds::flat_storage;
};
inline constexpr name_t name{};
}

// Example: downstream bits (flat — same across hierarchy)
namespace livehd::attrs {
struct bits_t {
  using value_type = int;
  using storage    = hhds::flat_storage;
};
inline constexpr bits_t bits{};
}

// Example: downstream hbits (hier — can differ per hierarchy instance)
namespace livehd::attrs {
struct hbits_t {
  using value_type = int;
  using storage    = hhds::hier_storage;
};
inline constexpr hbits_t hbits{};
}
```

No ADL overloads needed per-tag. The generic `AttrRef<Tag>` handles
`get`/`set`/`has`/`del` via `if constexpr` on `Tag::storage`.

### Compile-time dispatch

`node.attr(tag)` uses `if constexpr` (or `consteval`) on `Tag::storage` to
select the correct map — zero runtime branching for storage selection.

Accessing a `hier_storage` attribute on a `Node` without hierarchy context
is a runtime error (`I(...)` assertion), not a compile-time error. The `Node`
type is the same regardless of traversal mode.

### attr_clear semantics

- `g->attr_clear(tag)` — like `std::map::clear()`: empties all entries for
  this attribute, map stays registered. `g->has_attr(tag)` is still true.
  Nothing to persist when empty.
- `g->clear()` — clears graph body + all attribute maps + all registrations.

### Persistence

- Attribute maps are saved/loaded alongside bodies.
- Empty maps are not persisted.
- Load/save must not mix attributes across tags — each tag's map is
  independent.
- Downstream attribute types need registration to enable deserialization.
  The mechanism for this is TBD (likely a type registry at save/load time).

### Extension model

Downstream projects define new attributes by declaring a tag struct in their
own namespace. No HHDS files edited. No registration for in-memory use — only
for persistence of custom attributes.

## Concept and infrastructure

File: `hhds/attr.hpp`

```cpp
namespace hhds {

struct flat_storage {};
struct hier_storage {};

template <class Tag>
concept Attribute = requires {
  typename Tag::value_type;
  typename Tag::storage;
  requires std::is_same_v<typename Tag::storage, flat_storage>
        || std::is_same_v<typename Tag::storage, hier_storage>;
};

template <Attribute Tag>
using attr_result_t = std::conditional_t<
    std::is_trivially_copyable_v<typename Tag::value_type>
        && sizeof(typename Tag::value_type) <= 16,
    typename Tag::value_type,
    const typename Tag::value_type&>;

}  // namespace hhds
```

## AttrRef proxy

```cpp
namespace hhds {

template <Attribute Tag>
class AttrRef {
  Node& node_;  // or Pin&
public:
  attr_result_t<Tag> get() const;
  bool               has() const;
  void set(const typename Tag::value_type& v);
  void del();
};

}  // namespace hhds
```

`AttrRef` is returned by `node.attr(tag)`. Internally it uses `if constexpr`
on `Tag::storage` to access the correct map on the `Graph`.

## Lazy typed storage on Graph/Tree

```cpp
class Graph {
  std::unordered_map<std::type_index, std::any> attr_stores_;

public:
  template <Attribute Tag>
  auto& attr_store(Tag) {
    if constexpr (std::is_same_v<typename Tag::storage, flat_storage>) {
      using Map = std::unordered_map<Nid, typename Tag::value_type>;
      // lazy create on first access
    } else {
      using Map = std::unordered_map<std::pair<Tree_pos, Nid>, typename Tag::value_type>;
      // lazy create on first access
    }
  }

  template <Attribute Tag>
  void attr_clear(Tag) {
    // clear entries, keep map registered
  }

  template <Attribute Tag>
  bool has_attr(Tag) const;

  void clear();  // clear body + all attr maps
};
```

## Built-in attributes

### `hhds::attrs::name`

File: `hhds/attrs/name.hpp`

```cpp
namespace hhds::attrs {
struct name_t {
  using value_type = std::string;
  using storage    = hhds::flat_storage;
};
inline constexpr name_t name{};
}
```

Works on both `Node` and `Pin`. Used automatically by `print()`.

### `hhds::attrs::pin_name` (read-only)

Returns the port name from the node's `GraphIO` declaration. This is not a
stored attribute — it resolves dynamically from `GraphIO`. Accessed via
`pin.get_pin_name()` rather than the `attr()` mechanism.

## Pre-requisite: Remove `tree<X>` template and `data_stack`

The templated `tree<X>` class carries a `data_stack` (`std::vector<X>`) that
stores per-node payload alongside the structural `pointers_stack`. Once the
attribute system is in place, per-node data should be stored as attributes
instead.

- [ ] Remove the `tree<X>` template class and `PayloadForest<X>`.
- [ ] Remove `data_stack` and all `get_data`/`set_data`/`operator[]` methods.
- [ ] Migrate any existing users of `tree<X>` to use the `Tree` class +
      attributes for per-node data.

## Implementation steps

1. Add storage markers (`flat_storage`, `hier_storage`) and `Attribute` concept.
2. Add type-erased `attr_stores_` map to `Graph` and `Tree`.
3. Add `attr_store(tag)`, `attr_clear(tag)`, `has_attr(tag)` to `Graph`/`Tree`.
4. Add `AttrRef<Tag>` proxy.
5. Add `attr(tag)` template method to `Node` and `Pin`.
6. Add `hhds::attrs::name` built-in attribute.
7. Wire `print()` to use the name attribute.
8. Add attribute persistence (save/load with body).
9. Add tests:
   - flat get/set/has/del on Node and Pin.
   - hier get/set/has/del with hierarchy context.
   - attr_clear preserves map registration.
   - g->clear() removes everything.
   - round-trip persistence of attributes.
   - downstream attribute extension.
   - runtime error on hier attr without hier context.

## HHDS file layout

- `hhds/attr.hpp` — concept, storage markers, `AttrRef`
- `hhds/attrs/name.hpp` — built-in name attribute tag
