# HHDS: Hardware Hierarchical Dynamic Structure

HHDS is a C++23 library for compact tree and graph data structures intended for
EDA and compiler workloads. The focus is on memory efficiency, stable identity,
and a clean hierarchy model that can support translation between tree and graph
representations.

The repository is being simplified toward a clean standalone API. The current
implementation already contains the core structural containers, but the public
surface is being cleaned up around a declaration-centric model. The roadmap for
that work lives in [`TODO.md`](/Users/renau/projs/hhds/TODO.md), and the
intended public API shape lives in [`api.md`](/Users/renau/projs/hhds/api.md).

## Design goals

- Clean standalone library first.
- Stable tombstone-style internal IDs.
- Required immutable names for user-facing objects.
- Structural containers with external metadata.
- Similar `Forest` / `GraphLibrary` APIs where it is natural.
- Lazy loading for large graph/tree bodies.
- Assertions for invalid usage instead of exceptions.

## Core concepts

### Tree side

- `Forest` owns named tree declarations and tree bodies.
- `TreeIO` is the declaration object.
- `Tree` is the optional implementation body attached to a `TreeIO`.
- Trees stay generic and structural. There is no built-in input/output model on
  the tree side.
- Tree bodies can be empty.

### Graph side

- `GraphLibrary` owns named graph declarations and graph bodies.
- `GraphIO` is the declaration object.
- `Graph` is the optional implementation body attached to a `GraphIO`.
- `GraphIO` owns ordered input/output declarations.
- `Graph` owns only the implementation body, not the declaration interface.
- Graph bodies can be empty.

### Metadata

HHDS is intended to stay structural. User metadata should live in external
maps keyed by wrapper objects such as:

- `Node_class`
- `Node_flat`
- `Node_hier`
- `Pin_class`
- `Pin_flat`
- `Pin_hier`

Future metadata registration belongs at `Forest` / `GraphLibrary`, not at
individual `Tree` / `Graph` objects.

## Structural properties

### Graph

- Mutable graph with node and pin deletion.
- Ordered pins matter.
- Tombstone deletion: IDs are never reused.
- Small-edge optimization with overflow handling for high-degree cases.
- Inline `Type` field for structure-relevant semantics.
- Parallel read-only access is expected; mutations are single-threaded.

### Tree

- Optimized for AST-like traversal and mutation patterns.
- Chunked storage with 8-node blocks.
- Efficient sibling traversal.
- Tombstone deletion: IDs are never reused.
- Subtree references through `Forest`.
- Three natural traversal modes: pre-order, post-order, and sibling-order.
- Inline `Type` field for structure-relevant semantics.

## Planned public direction

The intended public model is declaration-first:

```cpp
auto glib = std::make_shared<hhds::GraphLibrary>();
auto gio  = glib->create_graphio("alu");
auto g    = gio->create_graph();

auto forest = std::make_shared<hhds::Forest>();
auto tio    = forest->create_treeio("parser");
auto t      = tio->create_tree();
```

Key behavior targets:

- Names are required and immutable.
- `Tree` / `Graph` bodies are created only through `TreeIO` / `GraphIO`.
- Deleting a body clears only the implementation.
- Deleting a declaration removes declaration plus body, while tombstone identity
  stays internal.
- Normal `find_*` APIs ignore tombstones and return `nullptr`.
- `GraphIO` owns graph IO declarations and should be the only public API for
  graph IO mutation.

## Build

```bash
bazel build //hhds:all
bazel build //hhds:core
bazel build //hhds:graph
```

## Test

```bash
bazel test //hhds:all

bazel run //hhds:deep_tree_correctness
bazel run //hhds:wide_tree_correctness
bazel run //hhds:chip_typical_correctness
bazel run //hhds:chip_typical_long_correctness
bazel run //hhds:forest_correctness

bazel test //hhds:graph_test
bazel test //hhds:graph_bench
```

## Notes

- Binary persistence is the intended normal format.
- Text read/write is useful for debugging and manual intervention, but does not
  need a stable long-term format.
- Declaration and body persistence should be separate.
- Large bodies should support automatic unload/reload based on `shared_ptr`
  lifetime.
