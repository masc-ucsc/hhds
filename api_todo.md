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

## 1. Named Library / Forest Lookup

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

## 2. Forest / GraphLibrary Iteration Helpers

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

## 3. Graph Node Type And `loop_last`

Graph nodes already carry inline `Type`.

Pending:

- add one more inline node property: `loop_last`
- expose container-level or node-level API to read/write the flag
- define forward traversal semantics for `loop_last`:
  - when forward traversal reaches a node marked `loop_last`, stop traversal expansion there
  - continue the rest of the traversal first
  - after the remaining traversal finishes, resume traversal using that `loop_last` node as the new source
- add tests that lock down the expected visitation order

This is specifically about forward traversal behavior, not tree traversal.

## 4. Hierarchy Naming Consistency

Tree and graph still use different public verbs for the same concept.

Current:

- tree: `set_subnode`, `has_subnode`, `get_subnode`
- graph: `set_subnode`, `has_subnode`, `get_subnode`

Pending:

- choose one consistent naming family, or
- keep domain-specific names but make the parallel explicit in the docs

This is mostly API cleanup, but it matters before save/load/print and cursor APIs are frozen.

## 5. Hierarchy Cursor And Direct Sub-Instance Queries

Tree-side cursor support is now implemented on the structural API:

- `Tree::create_cursor(...)` for single-tree navigation
- `Forest::create_cursor(...)` for hierarchy-aware navigation across tree subnode refs
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
- direct subgraph enumeration from a graph:
  - `auto subs = graph.get_subs();`
  - each entry is the direct callsite `Node_class` whose `get_subnode(...)` points to another graph in the same library

Pending tree-side capabilities:

- none for cursoring; direct subnode enumeration is available from the tree itself:
  - `auto subs = tree.get_subs();`
  - each entry is the direct callsite `Node_class` whose `get_subnode(...)` points to another tree in the same forest

Updated tree-side shape should follow the structural API:

```cpp
auto forest = std::make_shared<hhds::Forest>();
const hhds::Tid top_tid = forest->create_tree();
auto& top = forest->get_tree(top_tid);
const auto top_root = top.add_root_node();
auto cursor = forest->create_cursor(top_tid, top_root.get_current_pos());
```

Not the old payload-owning `Forest<X>` / `tree<X>` model.

## 6. Save / Load / Print

This is the next major cross-cutting API area from [plan.md](/Users/renau/projs/hhds/plan.md).

Pending:

- `Tree::save/load`
- `Forest::save/load`
- `Graph::save/load/print`
- `GraphLibrary::save/load`

Direction:

- HHDS owns structural serialization
- external metadata is provided through callbacks
- printed output may optionally include external metadata through callbacks
- saved IDs must remain stable across round-trips

## 7. Documentation Cleanup Around Pending APIs

When the pending items above move forward, keep these documentation rules:

- use `Tree::Node_class`, `Tree::Node_flat`, `Tree::Node_hier`
- do not reintroduce `Tnode_*`
- do not document payload-bearing tree wrappers
- treat tree metadata as external maps keyed by wrapper values
- keep examples aligned with [hhds/tests/tree_sample.cpp](/Users/renau/projs/hhds/hhds/tests/tree_sample.cpp) and [hhds/tests/iterators_impl.cpp](/Users/renau/projs/hhds/hhds/tests/iterators_impl.cpp)
