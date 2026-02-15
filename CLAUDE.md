# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

HHDS (Hardware Hierarchical Dynamic Structure) is a C++23 library providing highly optimized graph and tree data structures for hardware EDA tools and compilers. These are common representations in EDA/compiler workflows where translating between tree and graph forms is frequent.

The library is space-efficient by design, as EDA graphs can grow to billions of nodes. Other languages (Rust) exist in the repo only for benchmarking comparisons.

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

# Individual correctness tests (cc_binary, use `run`)
bazel run //hhds:deep_tree_correctness
bazel run //hhds:wide_tree_correctness
bazel run //hhds:chip_typical_correctness
bazel run //hhds:chip_typical_long_correctness
bazel run //hhds:forest_correctness

# Individual benchmark/test targets (cc_test, use `test`)
bazel test //hhds:graph_test
bazel test //hhds:graph_bench
```

Tests tagged `-long1` through `-long8`, `-manual`, and `-fixme` are excluded by default.

## Formatting and Linting

```bash
clang-format -i <file>                           # C++ formatting (Google-based, 132 col limit)
bazel build --config=clang-tidy //hhds:all        # Static analysis
```

Compiler warnings are errors (`-Werror`).

## Sanitizers

```bash
bazel build --config=asan //hhds:all       # Address sanitizer (Linux)
bazel build --config=asan_macos //hhds:all # Address sanitizer (macOS)
bazel build --config=tsan //hhds:all       # Thread sanitizer
bazel build --config=ubsan //hhds:all      # Undefined behavior sanitizer
```

## Architecture

### Core Libraries

**Tree (`tree.hpp` / `tree.cpp`)** — Template-based tree optimized for AST-like workloads:
- Chunked storage: 8 nodes per chunk, 64-byte aligned
- Delta-compressed child pointers (18-bit short, 49-bit long)
- Tombstone deletion (IDs never reused)
- Three iterators: `pre_order`, `post_order`, `sibling_order`
- Forest container (`Forest<T>`) for managing multiple trees and hierarchy across them with reference counting

**Graph (`graph.hpp` / `graph.cpp`)** — Optimized for EDA netlists:
- Nodes represent gates/components; each node has ordered input/output pins (pin order matters, e.g. a-b != b-a for Subtract)
- Pin 0 is the most common and has special optimizations to avoid creating separate pin/node entries
- 32-bit node IDs (Nid), bi-directional edges with pins
- Vector of 4 entry types: Free, Node, Pin, Overflow
- Node/Pin are 16 bytes, Overflow 32 bytes
- Small edge counts stored inline; overflow via scanning array; high-degree nodes use `emhash8::HashSet`
- Graph library support for multiple graphs and hierarchy across them
- Topological sort support

**Traversal consistency:** Both tree and graph provide hierarchical and non-hierarchical traversals with a consistent API, since compilers frequently translate between tree and graph representations.

### ID Types

- `Tree_pos` (int64_t) — tree node positions
- `Nid` — graph node IDs
- `Pid` — pin IDs
- `Vid` — vertex IDs

### Dependencies

- `iassert` — custom assertion library
- `emhash` — hash containers
- `googletest` / `google_benchmark` — testing (dev)
- `abseil-cpp` — benchmark comparisons (dev)

## Key Design Principles

- Space efficiency is paramount — graphs can reach billions of nodes
- Cache locality via chunked allocation, delta compression, minimal metadata
- Tombstone deletion everywhere — deleted IDs are never reused
- Overflow strategy: inline small data → scanning array → hash set for high-degree nodes
- Pin 0 optimization avoids allocating separate entries for the most common pin
- Graph operations are single-threaded for mutations; parallel read-only is supported
- The `graph` library suppresses some warnings (`-Wno-shadow`, `-Wno-unused-parameter`, etc.) in its build target
