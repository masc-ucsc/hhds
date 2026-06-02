# HHDS: Hardware Hierarchical Dynamic Structure

HHDS is a C++23 library for compact tree and graph data structures intended for
EDA and compiler workloads. The focus is on memory efficiency, stable identity,
and a clean hierarchy model that can support translation between tree and graph
representations.

The public surface is organized around a declaration-centric model: named
declarations own optional implementation bodies, on both the tree and graph
sides.

- [`docs/storage_internals.md`](docs/storage_internals.md) — in-memory layout
  and how storage grows as elements are added.

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

HHDS is intended to stay structural. User metadata lives in attribute maps
stored per-graph/tree, accessed through `Node`/`Pin` rich wrappers:

```cpp
node.attr(hhds::attrs::name).set("adder");
node.attr(livehd::attrs::bits).set(32);
```

Downstream projects add new attributes by declaring tag structs in their own
namespace — no HHDS edits needed.

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

## Public API

The public model is declaration-first:

```cpp
auto glib = std::make_shared<hhds::GraphLibrary>();
auto gio  = glib->create_io("alu");
auto g    = gio->create_graph();

auto forest = std::make_shared<hhds::Forest>();
auto tio    = forest->create_io("parser");
auto t      = tio->create_tree();
```

Key behaviors:

- Names are required and immutable.
- `Tree` / `Graph` bodies are created only through `TreeIO` / `GraphIO`.
- Deleting a body (`delete_tree` / `delete_graph`) clears only the
  implementation.
- Deleting a declaration (`delete_treeio` / `delete_graphio`) removes
  declaration plus body, while tombstone identity stays internal.
- Normal `find_*` APIs ignore tombstones and return `nullptr`.
- `GraphIO` owns graph IO declarations and is the only public API for graph IO
  mutation.

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
```

## Persistence

- Declarations and bodies persist separately. Declarations are written as text
  (`forest.txt` / `library.txt`); bodies are written as binary (`body.bin`, plus
  `overflow_<idx>.bin` for graph overflow sets) with a magic / version / endian
  header.
- The text declaration format is meant for debugging and manual intervention; it
  is not a stable long-term format.
- Planned (not yet implemented): automatic unload/reload of large bodies based
  on `shared_ptr` lifetime.
