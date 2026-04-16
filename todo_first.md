# HHDS TODO: Structural Cleanup

This file tracks all implementation work before the attribute system. The
attribute-specific plan lives in [`todo_attr.md`](/Users/renau/projs/hhds/todo_attr.md).
API examples live in [`sample.md`](/Users/renau/projs/hhds/sample.md).

## Goals

- Clean standalone C++ library.
- Declaration-centric API: `TreeIO`/`GraphIO` are the named declarations,
  `Tree`/`Graph` are optional bodies.
- Names are required and immutable.
- Internal IDs are stable (tombstone semantics) and not part of the public API.
- Assertions for invalid usage; no exceptions.
- `Node`/`Pin` rich wrappers are the public API surface; lightweight ID types
  (`Nid`, `Tree_pos`) are internal.

## Naming conventions (decided)

### Public API names

- `create_io(name)` — creates a `TreeIO` (on `Forest`) or `GraphIO` (on
  `GraphLibrary`). Replaces `create_treeio`/`create_graphio`.
- `find_io(name)` — finds a declaration. Replaces `find_treeio`/`find_graphio`.
- `get_io()` — navigates from body back to declaration. Replaces
  `get_treeio`/`get_graphio`.

### Traversal modes

Three traversal modes with distinct semantics:

| Suffix | Scope | Visits |
|--------|-------|--------|
| `_class` | This graph/tree only | Each node once |
| `_flat` | This + all unique subgraphs/subtrees | Each body once |
| `_hier` | Full hierarchy tree | Same body at multiple tree positions |

These names appear on iterators: `forward_class()`, `forward_flat()`,
`forward_hier(tree)` for graphs; `pre_order_class()`, `pre_order_flat()`,
`pre_order_hier(tree)` for trees.

### Index types

Users who need external maps keyed by node identity use opaque hashable index
types obtained from Node:

- `get_class_index()` — valid from any traversal mode
- `get_flat_index()` — valid from `_flat` and `_hier` traversals
- `get_hier_index()` — valid from `_hier` traversal only

Calling an index getter without sufficient context is a runtime error.

### Internal types

`Nid`, `Tree_pos`, `Gid`, `Tid`, and the internal storage structs are not
part of the public API. They may be needed for implementation but should not
appear in user-facing function signatures.

## Rich Node/Pin wrappers (decided)

### Graph Node

- Holds `Graph*` + internal ID (and optionally hierarchy context).
- Exposes: `.attr(tag)`, `.set_subnode()`, `.create_sink_pin()`,
  `.create_driver_pin()`, `.get_sink_pin()`, `.get_driver_pin()`,
  `.set_type()`, `.del_node()`,
  `.inp_edges()`, `.out_edges()`, `.inp_pins()`, `.out_pins()`,
  `.connect_driver()`, `.connect_sink()`, `.is_valid()`, `.is_invalid()`.
- `g->create_node()` returns `Node`.

### Graph Pin

- Holds `Graph*` + internal ID (Nid encodes pin-vs-node).
- Exposes: `.attr(tag)`, `.connect_driver()`, `.connect_sink()`,
  `.del_sink()`, `.del_sink(driver)`, `.del_driver()`, `.del_node()`,
  `.get_pin_name()`, `.is_valid()`, `.is_invalid()`.

### Tree Node

- Holds `Tree*` + internal ID (and optionally hierarchy context).
- Exposes: `.attr(tag)`, `.add_child()`, `.set_subnode()`, `.set_type()`,
  `.del_node()`,
  `.pre_order_class()`, `.pre_order_flat()`, `.pre_order_hier(tree)`,
  `.post_order_class()`, `.post_order_flat()`, `.post_order_hier(tree)`,
  `.sibling_order()`, `.is_valid()`, `.is_invalid()`.
- `t->get_root()` returns `Node`.

### Shared behavior

- All iterators return `Node` (not internal ID types).
- `.attr(tag)` works identically on graph and tree nodes.
- Pins from edge iteration inherit the context level of the originating Node.

## Phase 1: Documentation and cleanup

- [x] Rewrite `README.md` to describe the declaration-centric direction.
- [x] Add `sample.md` with target API examples.
- [x] Consolidate planning into `todo_first.md` + `todo_attr.md`.
- [x] Remove stale planning docs (`api.md`, `api_attribute.md`, `TODO.md`).
- [x] Remove old examples that show payload-bearing trees or direct body creation.
- [x] Remove Rust and `lhtree` comparison code from the repo.
- [x] Simplify build and dependency surface.

## Phase 2: Declaration-centric API

### Graph side

- [x] Add `GraphLibrary::create_io(name)` returning `shared_ptr<GraphIO>`.
- [x] Add `GraphLibrary::find_io(name)` returning `shared_ptr<GraphIO>`.
- [x] Add `GraphIO::create_graph()` and `GraphIO::get_graph()`.
- [x] Add `Graph::get_io()` (navigate back to declaration).
- [x] Make graph body creation impossible without an existing `GraphIO`.
- [x] Remove or hide explicit-ID public graph creation APIs.

### Tree side

- [x] Add `Forest::create_io(name)` returning `shared_ptr<TreeIO>`.
- [x] Add `Forest::find_io(name)` returning `shared_ptr<TreeIO>`.
- [x] Add `TreeIO::create_tree()` and `TreeIO::get_tree()`.
- [x] Add `Tree::get_io()` (navigate back to declaration).
- [x] Make tree body creation impossible without an existing `TreeIO`.
- [x] Remove or hide explicit-ID public tree creation APIs.

### Shared

- [x] Ensure declaration and body share the same internal ID.
- [x] Names are required at `create_io()` time.

Exit criteria: bodies cannot be created publicly without a declaration; names
are required; explicit-ID public creation is gone.

## Phase 3: Graph IO ownership and pin/connect refactor

### Done

- [x] `GraphIO::add_input`, `add_output`, `delete_input`, `delete_output`.
- [x] Move graph port ordering to `GraphIO`.
- [x] Auto-materialize graph IO nodes/pins from `GraphIO` on body creation.
- [x] `Graph::get_input_pin("name")` and `get_output_pin("name")`.
- [x] Body sync when `GraphIO` changes after body exists.

### Remaining

- [x] Remove direct graph-side IO mutation from `Graph`.
- [x] Replace `create_pin` with `create_driver_pin` / `create_sink_pin`:
  - Overloads: `()` default, `(port_id)`, `("name")`.
  - String form resolves through sub-node's `GraphIO`.
  - Methods live on `Node`, not `Graph`.
- [x] Add `get_driver_pin` / `get_sink_pin` (errors if not created).
- [x] Replace `add_edge` with `connect_driver` / `connect_sink` on `Node`/`Pin`.
- [x] Add `get_pin_name()` on `Pin` (returns port name from `GraphIO`).
- [x] Make `set_subnode` a method on `Node` (accepts `GraphIO`/`TreeIO` shared_ptr).
- [x] Graph traversals return one public `Node` handle that carries class/flat/hier context.
- [x] Pins created from `Node` and pins returned by edge iteration inherit the originating context.
- [ ] Store `GraphIO` ID in node's 16-bit type field when ID < 2^16.
- [x] Hide `create_pin` and `add_edge` from the public `Graph` API.
- [ ] Remove private/internal `create_pin` and `add_edge` helpers entirely.

### Rich wrappers (graph)

- [x] Create graph `Node` class holding `Graph*` + `Nid`.
- [x] Create graph `Pin` class holding `Graph*` + `Nid`.
- [x] Move `set_subnode`, `set_type`, pin creation, connect, edge/pin iteration to wrappers.
- [x] Update graph `fast_*` / `forward_*` traversals to return the unified public `Node`.

### Rich wrappers (tree)

- [x] Create tree `Node` class holding `Tree*` + internal ID.
- [x] Move `add_child`, `set_subnode`, `set_type` to tree `Node`.
- [x] Move class traversal start (`pre_order_class`, `post_order_class`, `sibling_order`) to tree `Node`.
- [ ] Update all tree iterators to return `Node`.
- [ ] `Tree::get_root()` returns `Node`.

### Iterator renames and context

- [x] Graph: `fast_class()` / `forward_class()` / `fast_flat()` /
  `forward_flat()` / `fast_hier()` / `forward_hier()` return public `Node`
  handles with `is_class()` / `is_flat()` / `is_hier()`.
- [ ] Tree: `pre_order_class()` / `pre_order_flat()` / `pre_order_hier(tree)`,
  `post_order_class()` / `post_order_flat()` / `post_order_hier(tree)`,
  `sibling_order()`.

Exit criteria: all pins have explicit direction; all edges via connect API;
`create_pin`/`add_edge` hidden from the public API; Node/Pin are the public API
surface for graph, with remaining tree traversal/root cleanup tracked above.

## Phase 4: Lifetime and deletion semantics

### Fine-grained deletion (graph)

Edge deletion (pins and nodes survive):

- [x] `sink_pin.del_sink(driver_pin)` — remove one specific edge into a sink.
- [x] `sink_pin.del_sink()` — remove all edges into a sink.
- [x] `driver_pin.del_driver()` — remove all edges from a driver.

Node deletion (tombstoned, pins removed, all connected edges removed):

- [x] `node.del_node()` — delete node + all its pins + all connected edges.
- [x] `pin.del_node()` — same, via the pin's owning node.

Pins cannot be deleted individually — they live and die with their node.

### Fine-grained deletion (tree)

- [x] `node.del_node()` — delete node + entire subtree (all children recursively).
- [x] Deleted children's attributes are also gone.

### Clear (graph)

- [x] `g->clear()` — clears graph body + all attribute maps, `GraphIO` survives.
- [x] `gio->clear()` — clears declaration + body + attributes, tombstone internal.
- [x] `glib->find_io()` returns `nullptr` for cleared declarations.

### Clear (tree)

- [x] `t->clear()` — clears tree body + all attribute maps, `TreeIO` survives.
- [x] `tio->clear()` — clears declaration + body + attributes, tombstone internal.
- [x] `forest->find_io()` returns `nullptr` for cleared declarations.

### Validity and tombstones

- [x] Add `is_valid()` / `is_invalid()` on graph `Node` and `Pin`.
- [x] Add `is_valid()` / `is_invalid()` on tree `Node`.
- [x] Iterators skip tombstoned entries automatically.
- [x] Tombstone IDs are never reused.
- [x] Audit operations to assert in debug mode on invalid references.

Exit criteria: deletion semantics unambiguous and test-covered; stale handles
queryable; lookups skip tombstones; edge/node/subtree deletion works correctly.

## Phase 5: Persistence and lazy bodies

### Graph

- [ ] Save `GraphIO` declarations and `Graph` bodies separately.
- [ ] Lazy body load on `gio->get_graph()` and `glib->find_io(name)`.
- [ ] Auto-unload when last `shared_ptr<Graph>` disappears.
- [ ] `glib->save()` persists declarations, bodies, and attribute maps.

### Tree

- [ ] Save `TreeIO` declarations and `Tree` bodies separately.
- [ ] Lazy body load on `tio->get_tree()` and `forest->find_io(name)`.
- [ ] Auto-unload when last `shared_ptr<Tree>` disappears.
- [ ] `forest->save()` persists declarations, bodies, and attribute maps.

### Shared

- [ ] Stable name-to-ID mapping across round-trips.
- [ ] Debug text import/export.

Exit criteria: declarations load without bodies; body load is demand-driven;
binary round-trips preserve stable identity.

## Phase 6: Debugging and printing

- [ ] `g->print(std::cout)` — uses built-in name attribute automatically.
- [ ] `t->print(std::cout)` — same for trees.
- [ ] Deleted nodes/pins/declarations hidden from printed output.
- [ ] Optional extra attribute printing via callback.

Exit criteria: tree and graph printing remain practical; deleted data hidden.
