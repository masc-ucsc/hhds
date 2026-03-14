# HHDS API TODO

Pending API work after the tree refactor completed in [plan.md](/Users/renau/projs/hhds/plan.md).

Current tree status is no longer TODO:

- `hhds::Tree` / `hhds::Forest` are structural and non-templated
- tree wrappers exist as `Tree::Node_class`, `Tree::Node_flat`, `Tree::Node_hier`
- tree traversals return wrappers, not raw `Tree_pos`
- tree node payload is external metadata keyed by wrappers
- tree node `Type` is inline via `get_type()` / `set_type()`

See:

- [hhds/tests/tree_sample.cpp](/Users/renau/projs/hhds/hhds/tests/tree_sample.cpp)
- [hhds/tests/tree_wrapper_test.cpp](/Users/renau/projs/hhds/hhds/tests/tree_wrapper_test.cpp)
- [hhds/tests/tree_type_test.cpp](/Users/renau/projs/hhds/hhds/tests/tree_type_test.cpp)
- [hhds/tests/iterators_impl.cpp](/Users/renau/projs/hhds/hhds/tests/iterators_impl.cpp)

The old `tree<X>`, `Forest<X>`, `Tnode_*`, `get_data()`, and `ref_data()` sketches are obsolete and intentionally removed from this TODO.

## 1. Graph Wrapper Cleanup

Graph wrappers are still behind the tree-side model.

Pending:

- remove stored `Graph*` from `Node_class` and `Pin_class`
- make graph wrappers pure ID/context holders, matching tree wrappers
- move wrapper-owned creation helpers onto the container API:
  - current: `node.create_pin(port_id)`
  - target: `graph.create_pin(node, port_id)`
- keep `Node_flat`, `Pin_flat`, `Node_hier`, and `Pin_hier` as hashable value types

Notes:

- `Node_hier` / `Pin_hier` should keep explicit hierarchy identity (`hier_tid`, `hier_pos`)
- this is the main remaining graph/tree asymmetry from the original unification plan

## 2. Named Library / Forest Lookup

`GraphLibrary` currently supports numeric IDs only. `Forest` currently supports numeric `Tid` only.

Pending:

- optional names for graphs and trees
- `create_*` / `find_*` overloads by name
- optional explicit ID creation for reload workflows
- conflict checks when `(name, id)` pairs disagree

Target shape:

```cpp
auto g = lib.create_graph("alu");
auto g2 = lib.find_graph("alu");

auto tid = forest.create_tree("parser");
auto* t = forest.find_tree("parser");
```

Tree-side note:

- keep the current structural `Forest` ownership model
- do not reintroduce `tree<X>` or `shared_ptr<tree<X>>`

## 3. Forest / GraphLibrary Iteration Helpers

Pending:

- iteration over all live graphs in `GraphLibrary`
- iteration over all live trees in `Forest`
- consistent count helpers using `size_t`

Likely shape:

```cpp
size_t graph_count() const;
size_t tree_count() const;
```

If span-based iteration is added, it should avoid exposing tombstones as live entries.

## 4. Graph `is_valid()` Parity

Tree wrappers already have:

```cpp
Tree::is_valid(Tree::Node_class)
Tree::is_valid(Tree::Node_flat)
Tree::is_valid(Tree::Node_hier)
```

Pending on graph side:

```cpp
static constexpr bool is_valid(Node_class node);
static constexpr bool is_valid(Pin_class pin);
static constexpr bool is_valid(Node_flat node);
static constexpr bool is_valid(Pin_flat pin);
static constexpr bool is_valid(Node_hier node);
static constexpr bool is_valid(Pin_hier pin);
```

## 5. Hierarchy Naming Consistency

Tree and graph still use different public verbs for the same concept.

Current:

- tree: `add_subtree_ref`, `is_subtree_ref`, `get_subtree_ref`
- graph: `set_subnode`, `has_subnode`, `get_subnode`

Pending:

- choose one consistent naming family, or
- keep domain-specific names but make the parallel explicit in the docs

This is mostly API cleanup, but it matters before save/load/print and cursor APIs are frozen.

## 6. Hierarchy Cursor And Caller Tracking

Tree-side cursor support is now implemented on the structural API:

- `Tree::create_cursor(...)` for single-tree navigation
- `Forest::create_cursor(...)` for hierarchy-aware navigation across subtree refs
- cursor state is `tid + pos` for forest traversal, matching the `_hier` identity model
- the cursor keeps shared ownership of the active `Tree` / `Forest`, while wrappers remain pointer-free value types

Pending graph-side capabilities:

- rooted hierarchy cursor over graph instances
- manual navigation:
  - `goto_parent()`
  - `goto_first_child()`
  - `goto_last_child()`
  - `goto_next_sibling()`
  - `goto_prev_sibling()`
- iterate nodes at the current hierarchy level while preserving `_hier` context
- caller tracking for shared subgraphs

Pending tree-side capabilities:

- caller tracking for referenced trees

Updated tree-side shape should follow the structural API:

```cpp
auto forest = std::make_shared<hhds::Forest>();
const hhds::Tid top_tid = forest->create_tree();
auto& top = forest->get_tree(top_tid);
const auto top_root = top.add_root_node();
auto cursor = forest->create_cursor(top_tid, top_root.get_current_pos());
```

Not the old payload-owning `Forest<X>` / `tree<X>` model.

## 7. Save / Load / Print

This is the next major cross-cutting API area from [plan.md](/Users/renau/projs/hhds/plan.md).

Pending:

- `Tree::save/load/print`
- `Forest::save/load`
- `Graph::save/load/print`
- `GraphLibrary::save/load`

Direction:

- HHDS owns structural serialization
- external metadata is provided through callbacks
- printed output may optionally include external metadata through callbacks
- saved IDs must remain stable across round-trips

## 8. Documentation Cleanup Around Pending APIs

When the pending items above move forward, keep these documentation rules:

- use `Tree::Node_class`, `Tree::Node_flat`, `Tree::Node_hier`
- do not reintroduce `Tnode_*`
- do not document payload-bearing tree wrappers
- treat tree metadata as external maps keyed by wrapper values
- keep examples aligned with [hhds/tests/tree_sample.cpp](/Users/renau/projs/hhds/hhds/tests/tree_sample.cpp) and [hhds/tests/iterators_impl.cpp](/Users/renau/projs/hhds/hhds/tests/iterators_impl.cpp)
