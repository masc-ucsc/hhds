# HHDS API TODO

Planned API changes to align HHDS with real EDA/compiler usage patterns (informed by LiveHD lgraph/lnast).

A sample of the still not implemented API is at:
```
bazel test -c dbg //hhds:iterators
```

---

## 1. Compact ID Types (High Priority)

All iterators and public APIs should return these types, not raw `Nid`/`Pid`/`Tree_pos`.
Raw IDs (`Nid`, `Pid`, `Tree_pos`) are **private to the implementation**.
Users never see or handle raw IDs directly — creation functions return compact
types, and all query/mutation APIs accept compact types.

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
// in it (hier_tid). The hierarchy tree encodes the design instance tree:
// each tree node maps to a Gid. Walking from hier_tid to the root gives
// the full hierarchy path. get_current_gid() reads the Gid at hier_tid;
// get_root_gid() reads the Gid at the tree root.
struct Node_hier {
  Gid get_current_gid() const;    // Gid at hier_tid
  Gid get_root_gid() const;       // Gid at hierarchy tree root
  Port_id get_port_id() const;    // always 0 for Node
private:
  std::shared_ptr<tree<Gid>> hier_ref;  // hierarchy tree (each node stores a Gid)
  Tid hier_tid;                    // current position in hierarchy tree
  Nid nid;
};

struct Pin_hier {
  Gid get_current_gid() const;
  Gid get_root_gid() const;
  Nid get_nid() const;
  Port_id get_port_id() const;
private:
  std::shared_ptr<tree<Gid>> hier_ref;
  Tid hier_tid;
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
// hier_tid is the current position. Walking the hierarchy tree gives the
// full path of tree instances.
struct Tnode_hier {
  Tid      get_current_tid() const;
  Tid      get_root_tid() const;
private:
  std::shared_ptr<tree<Tid>> hier_ref;  // hierarchy tree (each node stores a Tid)
  Tid hier_tid;
  Tree_pos pos;
};
```

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

All compact types need `operator==`, `operator<`, and `std::hash` specializations
so they can be used as keys in external attribute tables (the primary use case).

---

## 2. Direction-Aware Edge Iteration (High Priority)

The #1 usage pattern in LiveHD. Every compiler pass iterates input or output edges.

```cpp
// On Graph — single-graph scope, returns Edge_class
EdgeRange<Edge_class> inp_edges(Node_class node) const;   // edges where node is sink
EdgeRange<Edge_class> out_edges(Node_class node) const;    // edges where node is driver
EdgeRange<Edge_class> inp_edges(Pin_class pin) const;
EdgeRange<Edge_class> out_edges(Pin_class pin) const;
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

## 4. Lazy Graph Traversal Iterators (High Priority)

`fast_iter()` currently returns `std::vector<FastIterator>` — won't scale to
billions of nodes. Replace with lazy ranges that yield compact types directly.

Creation functions return compact types (`Node_class`, `Pin_class`):
```cpp
Node_class create_node();                              // returns Node_class directly
Pin_class  create_pin(Node_class node, Port_id port);  // returns Pin_class directly
void       add_edge(Node_class driver, Node_class sink);    // pin-0 shorthand
void       add_edge(Pin_class driver, Pin_class sink);
```

```cpp
// Single-graph scope
NodeRange<Node_class> fast_class() const;
NodeRange<Node_class> forward_class() const;   // topological order

// Flattened cross-graph scope
NodeRange<Node_flat> fast_flat() const;
NodeRange<Node_flat> forward_flat() const;

// Full hierarchy scope
NodeRange<Node_hier> fast_hier() const;
NodeRange<Node_hier> forward_hier() const;
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

## 8. `connect(Node_class, Node_class)` Pin-0 Shorthand (Medium Priority)

Most cells have just pin-0 sink + pin-0 driver. Leverages existing pin-0 optimization.
This is the same as `add_edge(Node_class, Node_class)` from section 4.

```cpp
void connect(Node_class driver, Node_class sink);  // implicit pin 0 on both
```

---

## 9. Node Pin Iteration (Medium Priority)

Avoid forcing users to walk the pin linked list manually.

```cpp
PinRange<Pin_class> get_pins(Node_class node) const;       // all pins
PinRange<Pin_class> get_driver_pins(Node_class node) const; // pins that drive edges
PinRange<Pin_class> get_sink_pins(Node_class node) const;   // pins that sink edges
```

---

## 10. Update Tree Iterators to Return `Tnode_class` / `Tnode_hier` (Medium Priority)

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

## 11. Forest / GraphLibrary Iteration (Low Priority)

```cpp
// Forest
ForestRange<std::shared_ptr<tree<X>>> each_tree() const;
size_t tree_count() const;

// GraphLibrary
GraphRange<std::shared_ptr<Graph>> each_graph() const;
size_t graph_count() const;  // live count (excluding tombstones)
```

---

## 12. `operator[]` Return by Reference (Low Priority)

```cpp
// Current (returns by value):
X operator[](Tnode_class tnode);

// Fix:
X& operator[](Tnode_class tnode);
const X& operator[](Tnode_class tnode) const;
```

---

## 13. `is_valid()` Helpers (Low Priority)

```cpp
// Tree
static constexpr bool is_valid(Tnode_class tnode);

// Graph
static constexpr bool is_valid(Node_class node);
static constexpr bool is_valid(Pin_class pin);
```

---

## 14. Hierarchy Naming Consistency (Low Priority)

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

## 15. Hierarchy Cursor (High Priority)

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
  Tid hier_tid;                     // current position in hierarchy tree

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

  // Iterate nodes within current hierarchy level (single-graph scope)
  NodeRange<Node_class> each_node() const;

  // Reset cursor to a different position (must be within same hierarchy tree)
  void reset(Tid position);
};
```

### Hierarchy Cursor for Trees/Forest

For trees in a forest, the same cursor concept applies. The hierarchy tree
records which tree instances reference which sub-trees via `subtree_ref`.

```cpp
class ForestCursor {
  Forest<X>* forest;
  std::shared_ptr<tree<Tid>> hier_tree;  // hierarchy tree (each node stores a Tid)
  Tid hier_tid;

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
  TnodeRange<Tnode_class> each_tnode() const;

  void reset(Tid position);
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
// Returns an iterator over the set of (caller_gid, caller_node) pairs.
// The set lives inside the target graph (or GraphLibrary).
struct CallerInfo {
  Gid        caller_gid;   // graph that contains the subnode
  Node_class caller_node;  // node within that graph that has set_subnode
};
auto get_callers(Gid gid) const;  // iterable over CallerInfo

// Forest — who references this tree?
struct TreeCallerInfo {
  Tid    caller_tid;   // tree that contains the subtree_ref
  Tnode_class caller_tnode; // node within that tree that has add_subtree_ref
};
auto get_callers(Tid ref) const;  // iterable over TreeCallerInfo
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

### Relationship to `_hier` Types

The cursor internally holds the same `(hier_ref, hier_tid)` pair that
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

All must support hashing and equality for use as hash map keys.
