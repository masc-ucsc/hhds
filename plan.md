# HHDS Structural API Unification

## Summary

Before adding save/load/print, first rework the tree side so tree and graph follow the same public model.

The main change is:

- remove embedded `X` payload storage from `tree<X>` and `Forest<X>`
- make tree purely structural, like graph is primarily structural
- move all tree content to external metadata keyed by public tree wrapper objects
- introduce tree wrapper types scoped inside `Tree`, parallel to graph:
  - `Tree::Node_class`
  - `Tree::Node_flat`
  - `Tree::Node_hier`

This should be the first phase. Save/load/print should be designed on top of that unified model, not on top of the current templated tree payload API.

## Wrapper Design

All wrapper types (tree and graph) are pure lightweight ID holders. They do not hold pointers to their parent container.

### What each wrapper tier holds

| Tier | Graph | Tree |
|------|-------|------|
| `Node_class` | `Nid` | `Tree_pos` |
| `Node_flat` | `Nid`, root `Gid`, current `Gid` | `Tree_pos`, root forest ID, current forest ID |
| `Node_hier` | `Nid`, root `Gid`, current `Gid`, hierarchy `tree_pos` | `Tree_pos`, root forest ID, current forest ID, hierarchy `tree_pos` |

`_class` is for single-container operations (no hierarchy context).
`_flat` adds which container is the root and which is the current in a hierarchy.
`_hier` adds the exact position in the hierarchy tree, since the same container can appear at multiple hierarchy nodes.

### Operations live on containers, not wrappers

All operations are methods on `Tree`, `Graph`, `Forest`, or `GraphLibrary`. Wrappers are passed as arguments:

```cpp
Tree t;
auto root = t.add_root();           // returns Tree::Node_class
auto child = t.add_child(root);     // takes Tree::Node_class
t.get_first_child(root);            // takes Tree::Node_class
```

Wrappers are comparable, hashable, and usable as keys in standard containers. They have no methods that mutate or query the container.

### Container lookup

`Forest` and `GraphLibrary` provide static lookup by ID, so that given a forest/library ID you can retrieve the container without needing a stored pointer.

### Graph wrappers: remove stored pointer

The current `Graph::Node_class` holds a `Graph*`. This should be changed to hold only `Nid` (and `Gid` for `_flat`/`_hier`), matching the lightweight ID-only design. Same for `Pin_class` and other graph wrappers.

## Core API Direction

### Tree and Forest redesign

Replace:

- `tree<X>`
- `Forest<X>`

with non-templated structural containers:

- `Tree`
- `Forest`

The tree should only own:

- node existence
- child/sibling/parent structure
- subtree references
- traversal state and hierarchy helpers

It should not own arbitrary node payload data.

Remove the current data-facing API shape:

- `get_data`
- `set_data`
- `operator[]`
- iterator `.get_data()` methods
- APIs that require `const X&` payload on node creation

Replace node creation with structural-only forms:

- `add_root()`
- `add_child(parent)`
- `append_sibling(node)`
- `insert_next_sibling(node)`

If needed, allow optional reserve/capacity helpers, but no embedded node payload.

### Embedded `Type` field (kept in both tree and graph)

Graph nodes store a 16-bit `Type` inline. This is intentional, not a metadata leak: iterators and traversals behave differently depending on type. For example, graph topological sort starts from flops/registers (identified by type) to break loops.

Tree nodes should add a similarly small `Type` field. This gives both structures a uniform `get_type(node)` / `set_type(node, t)` API for the one piece of semantic information that affects structural traversal behavior.

Everything else (names, bitwidths, opcodes, source locations, etc.) remains external metadata.

### Traversal consistency

Tree traversals (`pre_order`, `post_order`, `sibling_order`) keep their tree-specific names since they describe meaningful tree operations. However, they return wrapper objects instead of raw `Tree_pos`:

- `pre_order()` / `post_order()` / `sibling_order()` return `Tree::Node_class`
- flat and hierarchical variants return `Tree::Node_flat` / `Tree::Node_hier`

Graph keeps its existing traversal names (`fast_class`, `forward_class`, `fast_flat`, etc.).

The symmetry is in the wrapper tiers (`_class`, `_flat`, `_hier`), not in forcing identical traversal names.

## Metadata Model

No metadata framework is added to HHDS.

Tree metadata becomes external, same as graph metadata is expected to be:

- `std::map<Tree::Node_class, T>`
- `absl::flat_hash_map<Tree::Node_flat, T>`
- `absl::flat_hash_map<Tree::Node_hier, T>`

This makes tree content consistent with the intended graph usage and avoids a special case where tree embeds payload while graph does not.

The implication is important:

- AST opcode, token, symbol, source location, widths, attributes, and any compiler semantics live outside the tree structure
- HHDS tree becomes a generic hierarchical skeleton

## Forest and GraphLibrary: Same Pattern

`Forest` (manages trees) and `GraphLibrary` (manages graphs) follow the same design:

- tombstone deletion — IDs never reused
- static lookup by ID to retrieve a container
- hierarchy represented as a tree where each node maps to a child container
- both can share the same hierarchy tree type since after de-templating, the hierarchy tree is just a structural `Tree` with external metadata mapping nodes to container IDs

EDA tools frequently convert between tree and graph representations while preserving the same hierarchy. The unified Forest/GraphLibrary pattern supports this.

## ID Stability

All internal IDs (`Tree_pos`, `Nid`, `Pid`, `Gid`, forest IDs) are stable across serialization. Save/load preserves exact ID values.

This means:

- tombstones are preserved in the serialized format (deleted slots are not compacted)
- external metadata keyed by IDs remains valid after load
- serialized files grow with total allocations, not just live data

This is acceptable because the typical EDA/compiler workflow creates fresh structures per pass rather than doing sustained create/delete cycles on the same structure. Tombstone accumulation is bounded in practice.

## Save/Load/Print Impact

Save/load/print should be planned after the tree rework, because the external API and serialization model change materially.

After the tree redesign:

- `Tree::save/load/print`
- `Forest::save/load`
- `Graph::save/load/print`
- `GraphLibrary::save/load`

all follow the same pattern:

- save/load core structure internally
- query or populate external metadata through callbacks
- print structure plus optional metadata through callbacks

This also makes the save format cleaner:

- structural payload is always HHDS-owned
- semantic payload is always callback-owned

## Implementation Plan

### 1. Tree refactor first

Refactor the current tree implementation into non-templated structural storage.

Key changes:

- remove `std::vector<X> data_stack`
- remove all template dependence from the structural tree container
- remove iterator `.get_data()` methods
- add a small `Type` field to tree nodes (matching graph's existing 16-bit `Type`)
- preserve the current chunked layout and performance-oriented node linkage
- keep subtree reference support

Forest should also become non-templated and manage structural trees only. Adopt tombstone deletion matching GraphLibrary's pattern.

### 2. Introduce tree public wrappers

Add `Tree::Node_class`, `Tree::Node_flat`, and `Tree::Node_hier` as scoped types inside `Tree`.

They should mirror graph wrapper behavior:

- pure ID holders (no container pointers)
- comparable
- hashable
- lightweight
- safe as keys in standard metadata containers
- sufficient for local, flattened, and hierarchical traversals

Update graph wrappers (`Graph::Node_class`, `Graph::Pin_class`, etc.) to also remove stored `Graph*` pointers, becoming pure ID holders.

### 3. Align tree traversal API with graph

Tree traversals return `Tree::Node_class` (or `_flat`/`_hier` variants) instead of raw `Tree_pos`.

Keep tree-specific traversal names (`pre_order`, `post_order`, `sibling_order`).

Update existing tests to use structural-only tree creation and external metadata maps.

### 4. Then design save/load/print

Once tree and graph are aligned structurally, add:

- object methods for save/load/print
- callback-based metadata serialization and printing
- parallel save/load across independent trees/graphs
- parallel save/load across forest/library members

### 5. Then benchmark and stabilize

After the tree redesign and before broad adoption:

- validate no major regression in tree creation/traversal performance
- validate wrapper-based API overhead is negligible or acceptable
- validate external metadata maps are practical for expected EDA workloads

## Test Plan

Update all existing tree correctness tests (`deep_tree_correctness`, `wide_tree_correctness`, `chip_typical_correctness`, `chip_typical_long_correctness`, `forest_correctness`) to use the new non-templated API with external metadata.

Add tests for:

- structural tree creation without embedded payload
- subtree references after removing `X`
- wrapper equality and hashing for `Tree::Node_class`, `Tree::Node_flat`, `Tree::Node_hier`
- tree traversal returning wrappers instead of raw positions
- forest behavior after becoming non-templated with tombstone deletion
- external metadata keyed by tree wrappers
- consistency between tree and graph wrapper tiers
- ID stability across save/load round-trips
- later, save/load/print round-trips on the refactored tree/forest

## Relationship to api_todo.md

`api_todo.md` describes in-progress API work (student-driven) that is mostly independent of this plan. However, several items in `api_todo.md` were written before the unification decision and need updates to align:

1. **Templated types**: `api_todo.md` uses `tree<X>`, `Forest<X>`, `tree<Gid>`, `tree<Tid>` throughout. After this plan, tree is non-templated. Hierarchy trees become plain `Tree` with external metadata mapping nodes to `Gid`/`Tid`.

2. **Wrapper naming**: `api_todo.md` uses standalone `Tnode_class`, `Tnode_flat`, `Tnode_hier`. This plan scopes them as `Tree::Node_class`, `Tree::Node_flat`, `Tree::Node_hier`.

3. **Wrapper methods**: `api_todo.md` section 5 shows `tnode.get_data()` and `tnode.ref_data()` as methods on the wrapper. These should be removed. Wrappers are pure ID holders — no data access methods. The only embedded per-node data is `Type`, accessed via `tree.get_type(node)` / `tree.set_type(node, t)`. Everything else lives in external maps.

4. **`Node_class::create_pin()`**: Listed as already implemented in `api_todo.md`, but relies on a stored `Graph*` pointer inside the wrapper. After this plan, this becomes `graph.create_pin(node, port_id)`.

5. **Hierarchy cursor types**: `api_todo.md` section 9 uses `tree<Gid>` / `tree<Tid>` for hierarchy trees in cursors. After de-templating, these become `Tree` with external `Gid`/`Tid` metadata.

These updates to `api_todo.md` can happen incrementally as the student's work and this plan converge.

## Assumptions

- Tree payload-in-node is the wrong long-term abstraction.
- Tree and graph converge toward the same external philosophy: structural core plus external metadata.
- Raw `Tree_pos` values are stable IDs but accessed through `Tree::Node_class` wrappers externally.
- The tree refactor is phase 1; save/load/print is phase 2.
- Compatibility with the current templated tree API is not a priority — this is a breaking redesign.
- Tombstone growth is bounded because EDA passes create fresh structures rather than doing sustained mutation.
