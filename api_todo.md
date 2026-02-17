# HHDS API TODO

Planned API changes to align HHDS with real EDA/compiler usage patterns (informed by LiveHD lgraph/lnast).

A sample of the still not implemented API is at:
```
bazel test -c dbg //hhds:iterators
```

---

## 1. Compact ID Types (High Priority)

All iterators and public APIs should return these types, not raw `Nid`/`Pid`/`Tree_pos`.
Raw IDs (`Nid`, `Pid`, `Tree_pos`) are **private to HHDS internals**.
They may still be used internally across HHDS objects for compact storage and
indexing, but users never see or handle them directly. Public creation/query/
mutation APIs return/accept compact types.

Public collection/view APIs should use C++23 `std::span<const T>` instead of
custom `*Range` wrappers, so standard span/ranges operations compose naturally.
Spans are non-owning views: validity lasts until the owning HHDS object mutates
the underlying storage for that view.

### New Type Alias

```cpp
using Tid = uint64_t;  // Tree node ID (position in hierarchy tree)
```

### Graph IDs

```cpp
// Per-graph local (no hierarchy info, single graph scope)
struct Node_class {
  Port_id get_port_id() const; // always 0 for Node (consistent with Pin_class)
private:
  Nid nid;
};

struct Pin_class {
  Nid get_nid() const;        // master node
  Port_id get_port_id() const;
private:
  Pid pid;
};

// Cross-graph but flattened (each hierarchy instance gets a unique Gid)
struct Node_flat {
  Gid get_root_gid() const;
  Gid get_current_gid() const;
  Port_id get_port_id() const; // always 0 for Node
private:
  Gid root_gid;     // top-level graph
  Gid current_gid;  // graph instance this node belongs to
  Nid nid;
};

struct Pin_flat {
  Gid get_root_gid() const;
  Gid get_current_gid() const;
  Nid get_nid() const;        // master node
  Port_id get_port_id() const;
private:
  Gid root_gid;
  Gid current_gid;
  Pid pid;
};

// With hierarchy — carries a pointer to the hierarchy tree and a position
// in it (hier_pos). The hierarchy tree encodes the design instance tree:
// each tree node maps to a Gid. Walking from hier_pos to the root gives
// the full hierarchy path. get_current_gid() reads the Gid at hier_pos;
// get_root_gid() reads the Gid at the tree root.
struct Node_hier {
  Gid get_current_gid() const;    // Gid at hier_pos
  Gid get_root_gid() const;       // Gid at hierarchy tree root
  Port_id get_port_id() const;    // always 0 for Node
private:
  std::shared_ptr<tree<Gid>> hier_ref;  // hierarchy tree (each node stores a Gid)
  Tree_pos hier_pos;                    // current position in hierarchy tree
  Nid nid;
};

struct Pin_hier {
  Gid get_current_gid() const;
  Gid get_root_gid() const;
  Nid get_nid() const;
  Port_id get_port_id() const;
private:
  std::shared_ptr<tree<Gid>> hier_ref;
  Tree_pos hier_pos;
  Pid pid;
};
```

### Tree IDs

```cpp
// Per-tree local (single tree scope)
struct Tnode_class {
private:
  Tree_pos pos;
};

// Cross-tree but flattened (each tree instance gets a unique Tid)
struct Tnode_flat {
  Tid      get_root_tid() const;
  Tid      get_current_tid() const;
private:
  Tid      root_tid;
  Tid      current_tid;
  Tree_pos pos;
};

// With hierarchy — same pattern as graph: hier_ref is the hierarchy tree,
// hier_pos is the current position. Walking the hierarchy tree gives the
// full path of tree instances.
struct Tnode_hier {
  Tid      get_current_tid() const;
  Tid      get_root_tid() const;
private:
  std::shared_ptr<tree<Tid>> hier_ref;  // hierarchy tree (each node stores a Tid)
  Tree_pos hier_pos;
  Tree_pos pos;
};
```

### Conversions Between Compact Tiers

`_hier` carries full hierarchy path context, `_flat` carries only `(root,current)`,
and `_class` is local-only. Conversions are intentionally one-way:

```cpp
// always valid (information is discarded, never invented)
Node_class to_class(Node_hier v);
Node_flat  to_flat(Node_hier v);
Node_class to_class(Node_flat v);

// valid with explicit context
Node_flat  to_flat(Node_class v, Gid current_gid, Gid root_gid = current_gid);

// not allowed: hierarchy path cannot be reconstructed from _class/_flat alone
Node_hier  to_hier(Node_class) = delete;
Node_hier  to_hier(Node_flat)  = delete;
```

Same rules apply to `Pin_*` and `Tnode_*`.

### Edge View

```cpp
struct Edge_class {
  Pin_class driver;
  Pin_class sink;
};

struct Edge_flat {
  Pin_flat driver;
  Pin_flat sink;
};

struct Edge_hier {
  Pin_hier driver;
  Pin_hier sink;
};
```

### Hash/Equality Support

All compact types need `operator==`, `operator!=`, and `AbslHashValue` so they
can be used as keys in `absl::flat_hash_map` / `absl::flat_hash_set` for
external attribute tables (the primary use case).

```cpp
// Example pattern (same for all _class/_flat/_hier types):
struct Node_class {
  constexpr bool operator==(const Node_class &other) const { return nid == other.nid; }
  constexpr bool operator!=(const Node_class &other) const { return !(*this == other); }

  template <typename H> friend H AbslHashValue(H h, const Node_class &s) {
    return H::combine(std::move(h), s.nid);
  }
};
```

---

## 2. Direction-Aware Edge Iteration (High Priority)

The #1 usage pattern in LiveHD. Every compiler pass iterates input or output edges.

```cpp
// On Graph — single-graph scope, returns Edge_class
std::span<const Edge_class> inp_edges(Node_class node) const;   // edges where node is sink
std::span<const Edge_class> out_edges(Node_class node) const;   // edges where node is driver
std::span<const Edge_class> inp_edges(Pin_class pin) const;
std::span<const Edge_class> out_edges(Pin_class pin) const;
```

Usage:
```cpp
auto node = graph.create_node();
for (auto edge : graph.out_edges(node)) {
  auto drv = edge.driver;  // Pin_class on this node
  auto snk = edge.sink;    // Pin_class on destination
}
```

Direction is determined from the edge, not from pin construction (pin 0
driver+sink share a single Node entry for space efficiency).

---

## 3. `del_edge` (High Priority)

```cpp
void del_edge(Pin_class driver, Pin_class sink);
```

Must handle all three storage tiers: inline slots, scanning array, hash set.
Tombstone or compaction strategy TBD.

---

## 4. Span-Based Graph Traversal Views (High Priority)

`fast_iter()` currently returns `std::vector<FastIterator>`.
Expose traversal results as `std::span<const ...>` views over compact types.

Creation functions return compact types (`Node_class`, `Pin_class`).
`add_edge` accepts both `Node_class` and `Pin_class` pairs — a `Node_class`
is equivalent to a `Pin_class` with port 0, so `add_edge(Node_class, Node_class)`
is the pin-0 shorthand (no separate `connect` needed):
```cpp
Node_class create_node();  // creates node only (pin 0 is NOT automatically materialized)
Pin_class  Node_class::create_pin(Port_id port);  // preferred API style
void       add_edge(Node_class driver, Node_class sink);    // pin-0 shorthand
void       add_edge(Pin_class driver, Pin_class sink);
```

Pin-0 behavior:
- `node.create_pin(0)` returns the node's pin-0 handle and marks pin-0 as present.
- `node.create_pin(N!=0)` allocates a regular pin entry for port `N`.
- `add_edge(Node_class, Node_class)` is shorthand for using port 0 on both ends;
  if pin 0 is not present yet, it is materialized lazily.

```cpp
// Single-graph scope
std::span<const Node_class> fast_class() const;
std::span<const Node_class> forward_class() const;   // topological order

// Flattened cross-graph scope
std::span<const Node_flat> fast_flat() const;
std::span<const Node_flat> forward_flat() const;

// Full hierarchy scope
std::span<const Node_hier> fast_hier() const;
std::span<const Node_hier> forward_hier() const;
```

---

## 5. Tree/Forest and Graph/GraphLibrary Ownership with Smart Pointers (High Priority)

Creation and lookup functions return `std::shared_ptr` — callers hold shared
ownership. Each object exposes `get_tid()` or `get_gid()` to retrieve the
numeric ID (useful for debugging/printing but not the primary handle).

```cpp
// Forest — creates trees, returns shared ownership
std::shared_ptr<tree<X>> create_tree(const X& root_data);
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

## 6. Stop Overloading `Tree_pos` for Tree References (Medium Priority)

Currently `Tree_pos` is overloaded with negative values for tree references.
With the introduction of `Tid` (section 1) and `create_tree` returning
`std::shared_ptr<tree<X>>` (section 5), this overloading is no longer needed.
`Tid` is the tree-level analogue of `Gid` and is used wherever a tree
identity is required. Internal `Tree_pos` values remain private.

---

## 7. Naming in GraphLibrary and Forest (Medium Priority)

Needed for hierarchy — sub-nodes/sub-trees reference sub-graphs/sub-trees by name.
Lookup by name returns `std::shared_ptr` (consistent with section 5).

```cpp
// GraphLibrary
std::shared_ptr<Graph> create_graph(std::string_view name);
std::shared_ptr<Graph> find_graph(std::string_view name) const;
std::shared_ptr<Graph> find_graph(Gid gid) const;

// Forest
std::shared_ptr<tree<X>> create_tree(std::string_view name, const X& root_data);
std::shared_ptr<tree<X>> find_tree(std::string_view name) const;
std::shared_ptr<tree<X>> find_tree(Tid tid) const;
```

---

## 8. Node Pin Iteration (Medium Priority)

Avoid forcing users to walk the pin linked list manually.

```cpp
std::span<const Pin_class> get_pins(Node_class node) const;        // all existing pins, including pin 0 when present
std::span<const Pin_class> get_driver_pins(Node_class node) const; // pins that drive edges
std::span<const Pin_class> get_sink_pins(Node_class node) const;   // pins that sink edges
```

Pin 0 is not implicitly present after `create_node()`. It appears in
`get_pins(node)` only after it has been materialized (explicitly by
`node.create_pin(0)` or implicitly by APIs that require pin 0 such as
`add_edge(Node_class, Node_class)`).

---

## 9. Update Tree Iterators to Return `Tnode_class` / `Tnode_hier` (Medium Priority)

Current tree iterators dereference to `Tree_pos`. They should yield `Tnode_class`
for single-tree iteration, or `Tnode_hier` when traversing across a forest.

```cpp
// Tree creation returns Tnode_class directly
Tnode_class root  = my_tree.add_root(data);
Tnode_class child = my_tree.add_child(root, data);

// Single-tree (yields Tnode_class)
for (auto tnode : my_tree.pre_order()) {
  auto& data = tnode.get_data();
  auto parent = tnode.get_parent(); // returns Tnode_class
}

// Hierarchical across forest (yields Tnode_hier)
for (auto tnode : my_forest.pre_order_hier(tree_ref)) {
  // tnode is Tnode_hier, can cross subtree boundaries
}
```

---

## 10. Forest / GraphLibrary Iteration (Low Priority)

```cpp
// Forest
std::span<const std::shared_ptr<tree<X>>> each_tree() const;
size_t tree_count() const;

// GraphLibrary
std::span<const std::shared_ptr<Graph>> each_graph() const;
size_t graph_count() const;  // live count (excluding tombstones)
```

---

## 11. `operator[]` Return by Reference (Low Priority)

```cpp
// Current (returns by value):
X operator[](Tnode_class tnode);

// Fix:
X& operator[](Tnode_class tnode);
const X& operator[](Tnode_class tnode) const;
```

---

## 12. `is_valid()` Helpers (Low Priority)

```cpp
// Tree
static constexpr bool is_valid(Tnode_class tnode);

// Graph
static constexpr bool is_valid(Node_class node);
static constexpr bool is_valid(Pin_class pin);
```

---

## 13. Hierarchy Naming Consistency (Low Priority)

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

## 14. Hierarchy Cursor (High Priority)

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
  bool is_root() const;          // at top of cursor scope
  bool is_leaf() const;          // no sub-instances below
  int  depth() const;            // distance from root

  // Iterate nodes within current hierarchy level.
  // Yields Node_hier so hierarchy context is preserved.
  std::span<const Node_hier> each_node() const;

  // Reset cursor to a different position (must be within same hierarchy tree)
  void reset(Tree_pos position);
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

  void reset(Tree_pos position);
};
```

### Creating a Cursor

```cpp
// Graph — rooted at a specific graph instance
HierCursor cursor = lib.create_cursor(root_gid);

// Forest — rooted at a specific tree
ForestCursor cursor = forest.create_cursor(root_tree_ref);
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
