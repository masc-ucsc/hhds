# Graph and tree iteration

HHDS traversal APIs have the same shape for graphs and trees:

```cpp
for (auto node : object->scope().nodes(order)) {
  // ...
}
```

The scope selects the identity and hierarchy semantics. The optional order
selects how nodes within that scope are emitted. Omitting the order selects the
least expensive natural order: graph storage order or tree preorder.

## Graph scopes

| Scope | Meaning | Node identity | Compact subnode loop |
|---|---|---|---|
| `graph->body()` | Nodes stored in this graph body only | `Class_index` | The call-site node is visited once |
| `graph->definitions()` | Every reachable graph definition, once per `Gid` | `Definition_index` | Definitions remain unique; no occurrence expansion |
| `graph->grouped_hierarchy()` | The root body and every stored instance path | `Occurrence_index` | One grouped call-site occurrence; the path has no ordinal |
| `graph->occurrences()` | The physical hierarchy represented by the graph | `Occurrence_index` | One virtual occurrence per loop ordinal |

`body()` returns `Node_class` handles. `definitions()` also uses definition
handles backed by `Node_class`. The two hierarchical scopes return
`Occurrence_node`; use `base_node()` when class-storage attributes or the
underlying definition are needed.

Examples:

```cpp
// This graph body, in dependency order.
for (auto node : graph->body().nodes(hhds::Node_order::forward)) {
  // ...
}

// Unique reachable definitions, callees before callers.
for (auto node : graph->definitions().nodes()) {
  // ...
}

// Stored hierarchy, preserving compact groups.
for (auto node : graph->grouped_hierarchy().nodes()) {
  // ...
}

// Physical occurrences, including virtual compact-loop ordinals.
for (auto node : graph->occurrences().nodes(hhds::Node_order::forward)) {
  // node.path() identifies this occurrence
}
```

The occurrence view does not clone, unroll, or otherwise mutate graph storage.
Loop ordinals exist only in `Occurrence_path` and `Occurrence_index`.

### Graph order and cuts

`nodes()` uses storage order. Ordered traversals use:

- `Node_order::forward`: drivers before consumers.
- `Node_order::reverse`: consumers before drivers.

Loop-break placement is an optional second argument:

```cpp
view.nodes(hhds::Node_order::forward, hhds::Cut_placement::last)
```

`Cut_placement` is `first`, `last`, `both`, or `omit`. It controls where cut
nodes are emitted; it does not change the selected hierarchy scope.

### Hierarchy policy

`definitions()`, `grouped_hierarchy()`, and `occurrences()` accept a
`Hierarchy_policy`. The policy returns one action for each `Instance_site`:

| Action | Call-site occurrence | Descendants |
|---|---:|---:|
| `Instance_action::descend` | included | included |
| `Instance_action::opaque` | included | excluded |
| `Instance_action::prune` | excluded | excluded |

Keep the view alive when reusing its policy result across node iteration,
counting, lifting, and edge traversal:

```cpp
auto view = graph->occurrences(policy);
for (auto node : view.nodes()) {
  // ...
}
auto count = view.size_exact();
```

`grouped_hierarchy().instances()` visits instance groups without visiting all
nodes in their bodies. Each `Instance_group` reports its stored path and
physical `multiplicity()`.

The hierarchical views also provide `lift()` for root-body nodes and pins, and
`reachable_pins()` for occurrence-aware traversal across graph boundaries.

### Cost model

- `body().nodes()` is a streaming storage walk.
- `body().nodes(order)` uses the graph's dependency-order traversal caches.
- `definitions().nodes()` materializes the unique reachable definitions.
- Hierarchical `nodes()` is streaming in its natural order.
- Ordered hierarchical `nodes(order)` materializes occurrences to compute a
  global dependency order.
- `size_exact()` may return no value if an exact physical count overflows;
  `size_hint()` then provides a conservative hint.

Do not mutate a graph or its library while consuming a hierarchy view or a
range obtained from it.

## Tree scopes

Trees use the same scope-first spelling and structural orders:

| Scope | Meaning | Natural key |
|---|---|---|
| `tree->body()` | Nodes in this tree body only | `Tree_class_index` |
| `tree->definitions()` | Every reachable subtree definition, once per `Tid` | `Tree_flat_index` |
| `tree->occurrences()` | Every subtree instance | `Tree_hier_index` |

```cpp
for (auto node : tree->body().nodes(hhds::Tree_order::preorder)) {
  // ...
}

for (auto node : tree->occurrences().nodes(hhds::Tree_order::postorder)) {
  // ...
}
```

The default is preorder. Explicit orders are `Tree_order::preorder` and
`Tree_order::postorder`.

Every tree scope can also start at a node, which restricts the walk to that
subtree:

```cpp
auto root = tree->get_root_node();
for (auto node : root.definitions().nodes(hhds::Tree_order::preorder)) {
  // ...
}
```

`node.sibling_order()` is the separate local operation for walking a node and
its following siblings; it does not select a hierarchy scope.
