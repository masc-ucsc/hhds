# HHDS API TODO

Planned API changes to align HHDS with real EDA/compiler usage patterns (informed by LiveHD lgraph/lnast).

---

## 1. Compact ID Types (High Priority)

All iterators and public APIs should return these types, not raw `Nid`/`Pid`/`Tree_pos`.
The raw IDs remain accessible but only via explicit method calls.

### New Type Alias

```cpp
using Tid = uint64_t;  // Tree node ID (position in hierarchy tree)
```

### Graph IDs

```cpp
// Per-graph local (no hierarchy info, single graph scope)
struct Node_class {
  Nid nid;

  Nid get_nid() const;        // explicit access to raw ID
  Port_id get_port_id() const; // always 0 for Node (consistent with Pin_class)
};

struct Pin_class {
  Pid pid;

  Pid get_pid() const;
  Nid get_nid() const;        // master node
  Port_id get_port_id() const;
};

// Cross-graph but flattened (each hierarchy instance gets a unique Gid)
struct Node_flat {
  Gid root_gid;     // top-level graph
  Gid current_gid;  // graph instance this node belongs to

  Nid nid;

  Gid get_root_gid() const;
  Gid get_current_gid() const;
  Nid get_nid() const;
  Port_id get_port_id() const; // always 0 for Node
};

struct Pin_flat {
  Gid root_gid;
  Gid current_gid;
  Pid pid;

  Gid get_root_gid() const;
  Gid get_current_gid() const;
  Pid get_pid() const;
  Nid get_nid() const;        // master node
  Port_id get_port_id() const;
};

// With hierarchy — carries a pointer to the hierarchy tree and a position
// in it (hier_tid). The hierarchy tree encodes the design instance tree:
// each tree node maps to a Gid. Walking from hier_tid to the root gives
// the full hierarchy path. get_current_gid() reads the Gid at hier_tid;
// get_root_gid() reads the Gid at the tree root.
struct Node_hier {
  std::shared_ptr<Tree> hier_ref;  // hierarchy tree (design instance tree)
  Tid hier_tid;                    // current position in hierarchy tree
  Nid nid;

  Nid get_nid() const;
  Gid get_current_gid() const;    // Gid at hier_tid
  Gid get_root_gid() const;       // Gid at hierarchy tree root
  Port_id get_port_id() const;    // always 0 for Node
};

struct Pin_hier {
  std::shared_ptr<Tree> hier_ref;
  Tid hier_tid;
  Pid pid;

  Pid get_pid() const;
  Gid get_current_gid() const;
  Gid get_root_gid() const;
  Nid get_nid() const;
  Port_id get_port_id() const;
};
```

### Tree IDs

```cpp
// Per-tree local (single tree scope)
struct Tnode_class {
  Tree_pos pos;

  Tree_pos get_pos() const;
};

// Cross-tree but flattened (each tree instance gets a unique Tid)
struct Tnode_flat {
  Tid      root_tid;
  Tid      current_tid;
  Tree_pos pos;

  Tree_pos get_pos() const;
  Tid      get_root_tid() const;
  Tid      get_current_tid() const;
};

// With hierarchy — same pattern as graph: hier_ref is the hierarchy tree,
// hier_tid is the current position. Walking the hierarchy tree gives the
// full path of tree instances.
struct Tnode_hier {
  std::shared_ptr<Tree> hier_ref;
  Tid hier_tid;
  Tree_pos pos;

  Tree_pos get_pos() const;
  Tid      get_current_tid() const;
  Tid      get_root_tid() const;
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
EdgeRange<Edge_class> inp_edges(Nid nid) const;   // edges where nid is sink
EdgeRange<Edge_class> out_edges(Nid nid) const;    // edges where nid is driver
EdgeRange<Edge_class> inp_edges(Pid pid) const;
EdgeRange<Edge_class> out_edges(Pid pid) const;
```

Usage:
```cpp
for (auto edge : graph.out_edges(nid)) {
  auto drv = edge.driver;  // Pin_class on this node
  auto snk = edge.sink;    // Pin_class on destination
}
```

Direction is determined from the edge, not from pin construction (pin 0
driver+sink share a single Node entry for space efficiency).

---

## 3. `del_edge` (High Priority)

```cpp
void del_edge(Pid driver_id, Pid sink_id);
```

Must handle all three storage tiers: inline slots, scanning array, hash set.
Tombstone or compaction strategy TBD.

---

## 4. Lazy Graph Traversal Iterators (High Priority)

`fast_iter()` currently returns `std::vector<FastIterator>` — won't scale to
billions of nodes. Replace with lazy ranges that yield `Node_flat` or `Node_hier`.

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

## 5. Tree/Forest Ownership with Smart Pointers (High Priority)

```cpp
// Forest creates trees and returns shared ownership
std::shared_ptr<tree<X>> create_tree(const X& root_data);
std::shared_ptr<tree<X>> get_tree(Tree_ref ref);

// GraphLibrary creates graphs and returns shared ownership
std::shared_ptr<Graph> create_graph();
std::shared_ptr<Graph> get_graph(Gid id);
```

The `_hier` types hold `std::shared_ptr<Tree>` to the hierarchy tree,
ensuring it stays alive while references exist. The `_class` and `_flat`
types are lightweight value types with no smart pointers.

---

## 6. Distinct `Tree_ref` Type (Medium Priority)

Stop overloading `Tree_pos` with negative values for tree references.

```cpp
struct Tree_ref {
  int64_t id;

  bool is_valid() const;
  bool operator==(const Tree_ref&) const;
};
```

`Forest::create_tree` returns `Tree_ref`, not `Tree_pos`.

---

## 7. Graph Naming in GraphLibrary (Medium Priority)

Needed for hierarchy — sub-nodes reference sub-graphs by name.

```cpp
std::shared_ptr<Graph> create_graph(std::string_view name);
Gid find_graph(std::string_view name) const;
std::string_view get_name(Gid id) const;
```

---

## 8. `connect(Nid, Nid)` Pin-0 Shorthand (Medium Priority)

Most cells have just pin-0 sink + pin-0 driver. Leverages existing pin-0 optimization.

```cpp
void connect(Nid driver_nid, Nid sink_nid);  // implicit pin 0 on both
```

---

## 9. Node Pin Iteration (Medium Priority)

Avoid forcing users to walk the pin linked list manually.

```cpp
PinRange<Pin_class> get_pins(Nid nid) const;       // all pins
PinRange<Pin_class> get_driver_pins(Nid nid) const; // pins that drive edges
PinRange<Pin_class> get_sink_pins(Nid nid) const;   // pins that sink edges
```

---

## 10. Update Tree Iterators to Return `Tnode_class` / `Tnode_hier` (Medium Priority)

Current tree iterators dereference to `Tree_pos`. They should yield `Tnode_class`
for single-tree iteration, or `Tnode_hier` when traversing across a forest.

```cpp
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
X operator[](const Tree_pos& idx);

// Fix:
X& operator[](const Tree_pos& idx);
const X& operator[](const Tree_pos& idx) const;
```

---

## 13. `is_valid()` Helpers (Low Priority)

```cpp
// Tree
static constexpr bool is_valid(Tree_pos pos) { return pos != INVALID; }

// Graph (already has Nid_invalid etc, but add convenience)
static constexpr bool is_valid(Nid nid) { return nid != Nid_invalid; }
static constexpr bool is_valid(Pid pid) { return pid != Pid_invalid; }
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
