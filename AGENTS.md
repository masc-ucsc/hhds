# AGENTS.md

Guidance for coding agents working in this repository. Superset of `CLAUDE.md`:
identical content plus the `Source_locator` section below.

## Project Overview

HHDS (Hardware Hierarchical Dynamic Structure) is a C++23 library providing highly optimized graph and tree data structures for hardware EDA tools and compilers. These are common representations in EDA/compiler workflows where translating between tree and graph forms is frequent.

The library is space-efficient by design, as EDA graphs can grow to billions of nodes.

## Build Commands

```bash
bazel build //hhds:all                # Build everything
bazel build //hhds:core                # Tree library only
bazel build //hhds:graph               # Graph library only
bazel build --config=bench //hhds:all  # Optimized benchmark build
```

## Testing

```bash
bazel test //hhds:all                  # Run all tests

# Individual correctness tests (all cc_test — also covered by //hhds:all)
bazel test //hhds:deep_tree_correctness
bazel test //hhds:wide_tree_correctness
bazel test //hhds:chip_typical_correctness
bazel test //hhds:chip_typical_long_correctness
bazel test //hhds:forest_correctness
bazel test //hhds:graph_test

# Benchmarks and probes are `manual` cc_binary targets — excluded from the
# wildcard patterns above, so name them explicitly and use `run`
bazel run //hhds:forward_graph_bench
bazel run //hhds:probe_test
bazel run //hhds/bench:hhds_bench
```

Tests tagged `-long1` through `-long8`, `-manual`, and `-fixme` are excluded by default.

## Formatting and Linting

```bash
clang-format -i <file>   # C++ formatting (Google-based, 132 col limit)
```

Compiler warnings are errors (`-Werror`). `.clang-tidy` and the `@bazel_clang_tidy`
module exist, but no `--config=clang-tidy` is wired up in `.bazelrc` yet — the
command does not run.

Note: `hhds/graph.hpp` is currently NOT `.clang-format`-clean (it is wrapped at
~80 columns rather than the configured 132). Do not reformat it wholesale;
match the surrounding style when editing it.

## Sanitizers

```bash
bazel build --config=asan //hhds:all   # Address sanitizer (Linux and macOS)
bazel build --config=tsan //hhds:all   # Thread sanitizer
bazel build --config=ubsan //hhds:all  # Undefined behavior sanitizer — Linux only:
                                       # .bazelrc passes -lubsan, which Apple clang
                                       # does not ship
bazel build --config=clang --config=msan //hhds:core   # Memory sanitizer (needs an
                                       # instrumented libc++; see .bazelrc)
```

There is no `--config=asan_macos`; plain `--config=asan` works on macOS because
`--enable_platform_specific_config` pulls in the `build:macos` flags.

## Architecture

### Core Libraries

**Tree (`tree.hpp` / `tree.cpp`, plus `tree_print.cpp` / `tree_serial.cpp`)** — non-templated structural tree optimized for AST-like workloads; per-node payload lives in the attribute system (`Attr_host`), not in a type parameter:
- Chunked storage: 8 nodes per chunk (`CHUNK_SIZE`), 64-byte aligned
- Full 64-bit `Tree_pos` parent/sibling pointers plus a first-child and last-child pointer per slot — no bit packing (delta compression survives only on the graph side)
- Tombstone deletion (IDs never reused)
- Traversal is scope-first: `body()`, `definitions()`, `occurrences()`, each with `.nodes()`, `.nodes(Tree_order::preorder)`, `.nodes(Tree_order::postorder)`. `node.sibling_order()` is the separate local sibling walk; `pre_order()`/`post_order()` are private implementation details. See `docs/iterators.md`.
- `Forest` container (not a template) for managing multiple named trees and hierarchy across them, with per-tree reference counting

**Graph (`graph.hpp` / `graph.cpp`)** — Optimized for EDA netlists:
- Nodes represent gates/components; each node has ordered input/output pins (pin order matters, e.g. a-b != b-a for Subtract)
- Pin 0 is the most common and has special optimizations to avoid creating separate pin/node entries
- 64-bit ID words (`Nid`/`Pid`/`Vid` are all `uint64_t`) carrying a 42-bit table index (`Nid_bits`) plus 2 low tag bits; bi-directional edges with pins
- Two flat tables: `std::vector<NodeEntry> node_table` and `std::vector<PinEntry> pin_table`; both entry types are 32 bytes (`static_assert`ed), and deleted nodes are tombstoned via an `alive` bit
- Small edge counts stored inline (packed delta slots plus two full-width long-edge fields — 9 refs per NodeEntry, 6 per PinEntry); an entry that exceeds its inline budget spills its whole edge set into an `ankerl::unordered_dense::set<Vid>` held in `overflow_sets_`
- Graph library support for multiple graphs and hierarchy across them
- Topological sort support

**Traversal consistency:** Both tree and graph expose the same scope-first shape — pick an identity scope, then an order — since compilers frequently translate between tree and graph representations. Graph: `body()`, `definitions()`, `grouped_hierarchy()`, `occurrences()` (the last three take an optional `Hierarchy_policy`), ordered with `Node_order::forward` / `Node_order::reverse` and an optional `Cut_placement`. Tree: `body()`, `definitions()`, `occurrences()`, ordered with `Tree_order::preorder` / `Tree_order::postorder`. Full reference: `docs/iterators.md`.

**Source_locator (`source_locator.hpp`, header-only)** — Compact source-provenance table: a 64-bit `SourceId` → anchor (file + byte span + line). IR nodes/pins carry one `uint64` attribute (`attrs/srcid.hpp`) instead of a filename string + span struct:
- Ids are rapidhashes (vendored `rapidhash.h`, seeded/chained per field) of (path, start_byte, end_byte) — same span, same id in every locator/run/artifact; true collisions linear-probe to a nearby id (same scheme as name-hash gids)
- `combine(parents)` mints an id meaning "all of these anchors" (caller-ordered; `parents[0]` is the primary a diagnostic renders); `resolve_all` expands the combine DAG with a visited set + depth cap
- One instance per single-writer artifact: a member on `Graph` (chains to the library base via `set_base`), and per artifact wrapper on the tree side (one per client IR artifact); `Forest`/`GraphLibrary` hold the loaded read-only base
- `merge()` unions payload-first (cross-order collision convergence), translates combine parents through the remap, and returns old→new for the caller to apply to `attrs::srcid` values; `GraphLibrary::save`/`load_merge` do this natively, `Forest::save` writes the table the caller unioned in
- Persists as `srcmap.txt` next to `library.txt` / `forest.txt`; missing file = empty (older dirs load fine)

### ID Types

- `Tree_pos` (int64_t) — tree node positions
- `Nid` — graph node IDs
- `Pid` — pin IDs
- `Vid` — a stored edge-endpoint reference: a node-or-pin id whose low bits tag pin (bit 0) and edge direction (bit 1)

### Dependencies

- `iassert` — custom assertion library
- `abseil-cpp` — `absl::InlinedVector` / `absl::flat_hash_map`; used in the *public* API of `//hhds:graph` and `//hhds:core`, not dev-only
- `ankerl::unordered_dense` — vendored (`unordered_dense.hpp`); the workhorse hash container for edge overflow sets and internal maps
- `emhash` — legacy baseline; still fetched and still in the `core`/`graph` dep lists, but only `hhds/tests/small_set_bench.cpp` includes it
- `googletest` / `google_benchmark` — testing (dev)

## Key Design Principles

- Space efficiency is paramount — graphs can reach billions of nodes
- Cache locality via chunked allocation, delta-compressed edge slots, minimal metadata
- Tombstone deletion everywhere — deleted IDs are never reused
- Overflow strategy: inline packed delta slots → two full-width long-edge fields → one `ankerl::unordered_dense::set<Vid>` per spilled entry
- Pin 0 optimization avoids allocating separate entries for the most common pin
- Graph operations are single-threaded for mutations; parallel read-only is supported
- Handles and views are non-owning: a `Body_view`/`Grouped_hierarchy_view`/... or a class-iterator range used after its graph was deleted asserts in debug builds (`Graph::assert_accessible`) instead of silently walking a gutted graph
- The `graph` library suppresses some warnings (`-Wno-shadow`, `-Wno-unused-parameter`, etc.) in its build target
