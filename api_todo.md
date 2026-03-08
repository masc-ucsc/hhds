# HHDS API TODO

Planned API changes to align HHDS with real EDA/compiler usage patterns (informed by LiveHD lgraph/lnast).

A sample of the still not implemented API is at:
```
bazel run //hhds:iterators
```

### Already implemented

The following features are done and tested (see `tests/iterators_impl.cpp`):

- Graph-side compact ID types: `Node_class`, `Pin_class`, `Node_flat`, `Pin_flat`,
  `Node_hier`, `Pin_hier`, `Edge_class`, `Edge_flat`, `Edge_hier`, `Gid`,
  with `operator==`, `operator!=`, and `AbslHashValue`
- Direction-aware edge iteration: `inp_edges()`, `out_edges()` for `Node_class` and `Pin_class`
- `del_edge()` for all `Node_class`/`Pin_class` combinations
- Span-based graph traversal: `fast_class()`, `forward_class()`, `fast_flat()`,
  `forward_flat()`, `fast_hier()`, `forward_hier()` returning `std::span`
- Creation APIs returning compact types: `create_node()` → `Node_class`,
  `Node_class::create_pin()` → `Pin_class`, `add_edge(Node_class, Node_class)` pin-0 shorthand
- Node pin iteration: `get_pins()`, `get_driver_pins()`, `get_sink_pins()`

---

## 1. Tree-Side Compact ID Types (High Priority)

The graph-side compact types are done. The tree-side equivalents (`Tid`,
`Tnode_class`, `Tnode_flat`, `Tnode_hier`) are not yet defined.

`Tid` is the tree-level analogue of `Gid`.

```cpp
using Tid = uint64_t;  // Tree ID (unique per tree instance in a Forest)
```

```cpp
// Per-tree local (single tree scope)
// Tid is the raw internal node ID; Tree_pos is the lower-level implementation
// index inside HHDS storage and never appears in any public API.
struct Tnode_class {
  Tid get_raw_tid() const;  // raw internal ID: for debugging or cross-HHDS internals only
private:
  Tid tid;
};

// Cross-tree but flattened (each tree instance gets a unique Tid)
// root_tid / current_tid identify trees by their root node's Tid.
struct Tnode_flat {
  Tid      get_root_tid() const;
  Tid      get_current_tid() const;
private:
  Tid      root_tid;     // root Tid of the top-level tree in hierarchy
  Tid      current_tid;  // root Tid of the tree this node belongs to
  Tid      tid;          // raw node ID within the current tree
};

// With hierarchy — same pattern as graph: hier_ref is the hierarchy tree,
// hier_pos is the current position. Walking the hierarchy tree gives the
// full path of tree instances.
struct Tnode_hier {
  Tid      get_current_tid() const;
  Tid      get_root_tid() const;
private:
  std::shared_ptr<tree<Tid>> hier_ref;  // hierarchy tree (each node stores a root Tid)
  Tree_pos hier_pos;                    // position within the hierarchy tree (internal)
  Tid      tid;                         // raw node ID within the current tree
};
```

Conversions follow the same one-way rules as graph compact types (see
`to_class`, `to_flat` already implemented for graph types). Same rules
apply to `Tnode_*`.

All tree compact types need `operator==`, `operator!=`, and `AbslHashValue`.

---

## 2. Tree/Forest and Graph/GraphLibrary Ownership with Smart Pointers (High Priority)

Creation and lookup functions return `std::shared_ptr` — callers hold shared
ownership. Each object exposes `get_tid()` or `get_gid()` to retrieve the
numeric ID (useful for debugging/printing but not the primary handle).

```cpp
// Forest — creates trees, returns shared ownership
// Root must be set explicitly via tree<X>::add_root() after creation.
std::shared_ptr<tree<X>> create_tree();
std::shared_ptr<tree<X>> get_tree(Tid tid);

// tree<X> exposes its ID
Tid tree<X>::get_tid() const;

// GraphLibrary — creates graphs, returns shared ownership
std::shared_ptr<Graph> create_graph();
std::shared_ptr<Graph> get_graph(Gid gid);

// Graph exposes its ID
Gid Graph::get_gid() const;
```

The `_hier` types hold a `std::shared_ptr<tree<Gid>>` (graph) or
`std::shared_ptr<tree<Tid>>` (tree) to the hierarchy tree,
ensuring it stays alive while references exist. The `_class` and `_flat`
types are lightweight value types with no smart pointers.

---

## 3. Stop Overloading `Tree_pos` for Tree References (Medium Priority)

Currently `Tree_pos` is overloaded with negative values for tree references.
With the introduction of `Tid` (section 1) and `create_tree` returning
`std::shared_ptr<tree<X>>` (section 2), this overloading is no longer needed.
`Tid` is the tree-level analogue of `Gid` and is used wherever a tree
identity is required. Internal `Tree_pos` values remain private.

---

## 4. Naming in GraphLibrary and Forest (Medium Priority)

Needed for hierarchy — sub-nodes/sub-trees reference sub-graphs/sub-trees by name.
Lookup by name returns `std::shared_ptr` (consistent with section 2).

An optional `Gid`/`Tid` can be specified at creation time to preserve IDs across
save/reload cycles. If the given name already exists with a **different** ID, or
the given ID already exists with a **different** name, the call must trigger an
error (assertion / exception). This prevents silent ID remapping when restoring
serialized designs.

```cpp
// GraphLibrary
std::shared_ptr<Graph> create_graph(std::string_view name);
std::shared_ptr<Graph> create_graph(std::string_view name, Gid gid);  // reload with preserved ID
std::shared_ptr<Graph> find_graph(std::string_view name) const;
std::shared_ptr<Graph> find_graph(Gid gid) const;

// Forest
std::shared_ptr<tree<X>> create_tree(std::string_view name);  // root set via add_root()
std::shared_ptr<tree<X>> create_tree(std::string_view name, Tid tid);  // reload with preserved ID
std::shared_ptr<tree<X>> find_tree(std::string_view name) const;
std::shared_ptr<tree<X>> find_tree(Tid tid) const;
```

---

## 5. Update Tree Iterators to Return `Tnode_class` / `Tnode_hier` (Medium Priority)

Current tree iterators dereference to `Tree_pos`. They should yield `Tnode_class`
for single-tree iteration, or `Tnode_hier` when traversing across a forest.

```cpp
// Tree creation returns Tnode_class directly
Tnode_class root  = my_tree.add_root(data);   // sets the root; tree must be empty
Tnode_class child = my_tree.add_child(root, data);
Tnode_class root2 = my_tree.get_root();        // retrieve the root of a non-empty tree

// Data access on Tnode_class:
//   get_data()  — returns const X&; read-only, no editing allowed
//   ref_data()  — returns std::unique_ptr<X>; for mutable access; do not hold across mutations
const X&           tnode.get_data()  const;
std::unique_ptr<X> tnode.ref_data();

// Single-tree (yields Tnode_class)
for (auto tnode : my_tree.pre_order()) {
  const auto& data = tnode.get_data();  // read-only
  auto parent = tnode.get_parent();     // returns Tnode_class
}

// Hierarchical across forest (yields Tnode_hier)
for (auto tnode : my_forest.pre_order_hier(tree_ref)) {
  // tnode is Tnode_hier, can cross subtree boundaries
}
```

---

## 6. Forest / GraphLibrary Iteration (Low Priority)

```cpp
// Forest
std::span<const std::shared_ptr<tree<X>>> each_tree() const;
size_t tree_count() const;

// GraphLibrary
std::span<const std::shared_ptr<Graph>> each_graph() const;
size_t graph_count() const;  // live count (excluding tombstones)
```

Note: `GraphLibrary::live_count()` already exists but returns `Gid` not `size_t`.

---

## 7. `is_valid()` Helpers (Low Priority)

```cpp
// Tree
static constexpr bool is_valid(Tnode_class tnode);

// Graph
static constexpr bool is_valid(Node_class node);
static constexpr bool is_valid(Pin_class pin);
```

---

## 8. Hierarchy Naming Consistency (Low Priority)

Align tree and graph hierarchy vocabulary:

```
Tree  current: add_subtree_ref / is_subtree_ref / get_subtree_ref
Graph current: set_subnode      / has_subnode     / get_subnode

Proposed: both use consistent pattern:
  set_sub / has_sub / get_sub
  — or —
  set_subtree / set_subgraph (domain-specific but parallel)
```

---

## 9. Hierarchy Cursor (High Priority)

A cursor-like API (similar to tree-sitter cursors) for navigating the instance
hierarchy formed by connected forests or connected graph libraries. The cursor
supports manual up/down/next/prev navigation across module boundaries, giving
explicit control that the automatic `_hier` iterators do not.

### Motivation

The `_hier` iterators (`fast_hier`, `forward_hier`) automatically descend into
sub-instances during traversal. But many compiler passes need explicit control:
inspect the current hierarchy level, decide whether to descend, go back up, or
move to a sibling instance. A cursor gives this control while maintaining the
hierarchy context (which path was taken to reach the current position).

### Root Context

A cursor is always rooted at a specific instance. This disambiguates the parent
when the same sub-module is instantiated multiple times:

```
CPU_A (gid=1)             CPU_B (gid=5)
  ├── ALU (gid=2)           └── ALU (gid=2)
  │   └── Adder (gid=3)         └── Adder (gid=3)
  └── RegFile (gid=4)
```

A cursor rooted at CPU_A: descending into ALU, then going up, returns to CPU_A.
A cursor rooted at CPU_B: descending into ALU, then going up, returns to CPU_B.

Without a root, going up from ALU would reach both CPU_A and CPU_B — breaking
the tree abstraction. Multi-parent traversal is not supported: EDA modules can
pass instantiation parameters, so the traversal semantics depend on which
parent you came from. Instead, use `get_callers()` to discover all parents,
then create a rooted cursor from whichever one you need.

### Hierarchy Cursor for Graphs

The cursor navigates the **instance hierarchy** (which graph instances
contain which sub-instances), not the node/edge structure within a single graph.
Each position in the hierarchy tree corresponds to a graph instance.

```cpp
class HierCursor {
  const GraphLibrary* lib;
  std::shared_ptr<tree<Gid>> hier_tree;  // hierarchy tree (each node stores a Gid)
  Tree_pos hier_pos;                     // current position in hierarchy tree

  // Hierarchy navigation (return true if moved, false if at boundary)
  bool goto_parent();         // go up one hierarchy level
  bool goto_first_child();    // go down to first sub-instance
  bool goto_last_child();     // go down to last sub-instance
  bool goto_next_sibling();   // next sub-instance at same level
  bool goto_prev_sibling();   // prev sub-instance at same level

  // Access the graph at current hierarchy position
  Gid get_current_gid() const;   // graph instance at current position
  Gid get_root_gid() const;      // graph at root of cursor

  // Hierarchy info
  bool is_root() const;                       // at top of cursor scope
  bool is_leaf() const;                       // no sub-instances below
  int  depth() const;                         // distance from root
  bool is_subnode(Node_hier node) const;      // true if node has a registered sub-instance (via set_subnode)

  // Iterate nodes within current hierarchy level.
  // Yields Node_hier so hierarchy context is preserved.
  std::span<const Node_hier> each_node() const;

  // Reset cursor to a different graph within the same hierarchy tree
  void reset(Gid gid);
};
```

### Hierarchy Cursor for Trees/Forest

For trees in a forest, the same cursor concept applies. The hierarchy tree
records which tree instances reference which sub-trees via `subtree_ref`.

```cpp
class ForestCursor {
  Forest<X>* forest;
  std::shared_ptr<tree<Tid>> hier_tree;  // hierarchy tree (each node stores a Tid)
  Tree_pos hier_pos;

  bool goto_parent();
  bool goto_first_child();
  bool goto_last_child();
  bool goto_next_sibling();
  bool goto_prev_sibling();

  Tid get_current_tid() const;
  Tid get_root_tid() const;

  bool is_root() const;
  bool is_leaf() const;
  int  depth() const;

  // Iterate nodes within current tree
  std::span<const Tnode_class> each_tnode() const;

  // Reset cursor to a different tree within the same hierarchy (identified by root Tid)
  void reset(Tid root_tid);
};
```

### Creating a Cursor

```cpp
// Graph — rooted at a specific graph instance
HierCursor cursor = lib.create_cursor(root_gid);

// Forest — rooted at a specific tree (identified by the root Tid of that tree)
ForestCursor cursor = forest.create_cursor(root_tid);
```

### Discovering Callers

A graph or tree may be instantiated by multiple parents. Each target maintains
a set of callers, updated automatically: `set_subnode()` / `add_subtree_ref()`
adds the caller to the target's set; `delete_node()` / `delete_leaf()` /
`delete_subtree()` removes it. The set is typically small (a module is
instantiated a handful of times in most EDA designs).

```cpp
// Graph — who instantiates this graph?
// Returns a span over the set of (caller_gid, caller_node) pairs.
// The set lives inside the target graph (or GraphLibrary).
struct CallerInfo {
  Gid        caller_gid;   // graph that contains the subnode
  Node_class caller_node;  // node within that graph that has set_subnode
};
std::span<const CallerInfo> get_callers(Gid gid) const;

// Forest — who references this tree?
struct TreeCallerInfo {
  Tid    caller_tid;   // tree that contains the subtree_ref
  Tnode_class caller_tnode; // node within that tree that has add_subtree_ref
};
std::span<const TreeCallerInfo> get_callers(Tid ref) const;
```

Usage — inspect all instantiation contexts and pick one:
```cpp
for (auto& c : lib.get_callers(alu_gid)) {
  fmt::print("ALU instantiated by graph {}\n", c.caller_gid);
}
// Create a cursor rooted at one specific caller
auto first_caller = *lib.get_callers(alu_gid).begin();
HierCursor cursor = lib.create_cursor(first_caller.caller_gid);
cursor.goto_first_child();  // descend into ALU from that specific parent
```

### Hierarchy Tree Lifecycle

Each graph in the GraphLibrary has an associated hierarchy tree (`tree<Gid>`),
created empty when the graph is created. `create_node()` does not modify the
hierarchy tree. `set_subnode(node, sub_gid)` adds a new child to the hierarchy
tree (appended as last child, which is significantly faster than ordered
insertion). `delete_node()` on a subnode either removes the child from the
hierarchy tree or, for speed, sets its Gid to 0 (tombstone). The same pattern
applies to Forest: `add_subtree_ref()` adds a child to the forest's hierarchy
tree; deletion removes or tombstones it.

### Relationship to `_hier` Types

The cursor internally holds the same `(hier_ref, hier_pos)` pair that
`Node_hier`, `Pin_hier`, and `Tnode_hier` carry. When iterating nodes at the
current hierarchy level, the cursor can yield `_hier` values that inherit the
cursor's hierarchy context:

```cpp
for (auto node : cursor.each_node()) {
  // node is Node_hier with hierarchy context from the cursor
  if (cursor.is_subnode(node)) {
    cursor.goto_first_child();  // descend into sub-instance
    // ... work at lower level ...
    cursor.goto_parent();       // return
  }
}
```

### Relationship to Automatic `_hier` Iterators

The cursor and the automatic iterators serve complementary purposes:

- `fast_hier()` / `forward_hier()`: automatic DFS traversal that yields every
  node across all hierarchy levels. Best for linear passes that visit everything.
- `HierCursor` / `ForestCursor`: manual navigation of the hierarchy tree.
  Best for passes that need to inspect, skip, or selectively descend into
  sub-instances. Also useful for interactive/incremental tools that explore
  the design hierarchy on demand.

The automatic iterators can be thought of as a pre-order DFS walk of the
cursor's hierarchy tree, yielding nodes at each level.

---

## Design Notes

### Pin Direction Is Edge-Level

Pin direction (driver vs sink) comes from edges, not pin construction.
This is intentional: most cells have 1 input + 1 output on pin 0, both
sharing a single 32-byte Node entry. Creating separate driver/sink pins
would waste 32 bytes per pin. The `inp_edges`/`out_edges` API filters
by direction at query time without storing it on the pin.

### Attributes Are External

HHDS does not store node/pin attributes (name, bitwidth, delay, etc.).
Users maintain external tables keyed by `Node_flat`, `Pin_flat`, etc.
Named pins come from cell type definitions (`cell.hpp`) or LNAST types
(`lnast_ntype.hpp`), not from the graph structure.

### Compact Types for External Tables

Three tiers, matching LiveHD's Compact_class / Compact_flat / Compact:

- `_class`: per-graph/tree local scope (smallest, no Gid/Tid overhead)
- `_flat`: cross-graph/tree with flattened hierarchy (each instance gets a unique Gid/Tid)
- `_hier`: full hierarchy via shared pointer to hierarchy tree + position in it

All must support `AbslHashValue`, `operator==`, and `operator!=` for use as
`absl::flat_hash_map` keys.
