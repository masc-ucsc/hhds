# Bench CSV Schema

Single source of truth for what every bench binary emits. `hhds_bench`,
`boost_bench`, and the lgraph-side `lgraph_bench_thesis` all write rows
matching this schema, so `scripts/merge_csv.py` can concatenate them
without any per-binary special-casing.

## Header (one line, written by every binary at startup)

```
library,op,topology,axis,size,pins_per_node,hier_size,seed,run_idx,wall_ns,peak_rss_kb,bytes_per_node,l1_misses,llc_misses,instructions,cycles,ipc
```

## Columns

| Column | Type | Source | Notes |
|---|---|---|---|
| `library` | string | bench binary | One of `hhds`, `boost_directed`, `boost_bidirectional`, `lgraph`. |
| `op` | string | `--op` | One of: `build_node`, `build_edges`, `add_pin`, `mutate`, `lookup`, or one of seven traversal variants — see "Traversal ops" below. |
| `topology` | string | `--topology` | `chain`, `fanout`, `random_dag`, `eda_typical`. |
| `axis` | string | `--axis` | Which sweep this row belongs to: `nodes`, `pins`, `hier`. Used by plot scripts to decide the x-axis. |
| `size` | int | `--nodes` | Node count for this run. |
| `pins_per_node` | int | `--pins` | Pins per node. 1 = pin-0-only. |
| `hier_size` | int | `--hier` | Number of subgraph instances at depth-1; 1 = single-graph. |
| `seed` | int | `--seed` | RNG seed used by the generator. Same seed → same topology across libraries. |
| `run_idx` | int | bench loop | 0-indexed run within a (lib, op, params) tuple. |
| `wall_ns` | int | google_benchmark | Median over all loop iterations of `benchmark::State::iterations()`. |
| `peak_rss_kb` | int | wrapper | Filled by `scripts/run_all.sh` from `/usr/bin/time -v` — bench binary writes `0` here, wrapper rewrites the column. |
| `bytes_per_node` | float | wrapper | `(rss_after − rss_before) / size`. Bench binary writes `0`; wrapper rewrites. |
| `l1_misses` | int | wrapper | `perf stat -e L1-dcache-load-misses` (Linux only). Bench binary writes `0`; wrapper rewrites. macOS leaves as `0`. |
| `llc_misses` | int | wrapper | `perf stat -e LLC-load-misses` (Linux only). Same handling. |
| `instructions` | int | wrapper | `perf stat -e instructions` (Linux only). |
| `cycles` | int | wrapper | `perf stat -e cycles` (Linux only). |
| `ipc` | float | wrapper | `instructions / cycles`. macOS: `0`. |

## Why the bench binary writes `0` for memory and perf columns

Memory and cache counters can't be measured from inside the bench
process without skewing the very numbers we want to measure. They're
collected by wrapping the binary externally. The bench binary still
**emits** those columns — with placeholder `0` — so the CSV header
stays uniform and `scripts/run_all.sh` can rewrite them in place.

## Traversal ops

hhds exposes seven distinct traversal entry points
([graph.hpp:507-519](../graph.hpp#L507-L519)). The bench measures each one
as its own `op` so the comparison can isolate the cost of each axis
(ordered vs unordered × class vs flat vs hier × instance-only).

| op | hhds API called | What it visits | When to use |
|---|---|---|---|
| `traverse_forward_class` | `forward_class()` | All nodes of the top graph in topological order | Single-graph topo-sort cost |
| `traverse_fast_class`    | `fast_class()`    | All nodes of the top graph, visit-once unordered | Single-graph fast pass cost |
| `traverse_forward_flat`  | `forward_flat()`  | All nodes across all submodule instances, topo-ordered | Hier-flattening with order |
| `traverse_fast_flat`     | `fast_flat()`     | All nodes across all instances, unordered | Hier-flattening fast |
| `traverse_forward_hier`  | `forward_hier()`  | All nodes per-instance, topo-ordered, with hier context | Hier-aware ordered walk |
| `traverse_fast_hier`     | `fast_hier()`     | All nodes per-instance, unordered | Hier-aware fast walk |
| `traverse_hier_range`    | `hier_range()`    | One yield per submodule instance only (no inner nodes) | Module-tree / instance count |

For Boost.Graph: only `traverse_forward_class` and `traverse_fast_class`
have direct counterparts (BGL has no native hierarchy). The other five
rows are reported as `N/A` for `library=boost_*` in the master CSV.

For lgraph: all seven map to lgraph's `forward()` / `backward()` /
`fast()` / hier-iterator equivalents. See `livehd/lgraph/lgraph.hpp`.

## Determinism contract

For a fixed `(library, op, topology, size, pins_per_node, hier_size, seed)`,
the **edge list and traversal-visited-count must be identical across
all libraries**. Verification step 6 in the plan asserts this. Mismatch
= generator bug.