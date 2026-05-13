#!/usr/bin/env python3
"""Sweep runner for hhds_bench / boost_bench / lgraph_bench_thesis.

Calls the bench binary repeatedly with varying parameters and writes one
master CSV that downstream plot.py / print_table.py consume.

Example:
    python3 run_sweep.py \\
        --sweep nodes \\
        --bench bazel-bin/hhds/bench/hhds_bench \\
        --ops build_node,build_edges,traverse_fast_class,traverse_forward_class,lookup \\
        --sizes 10,100,1000,10000,100000,1000000 \\
        --out results/node_sweep.csv \\
        --runs 5

The CSV uses the schema documented in bench/schema.md. The bench binary
emits the header on its first invocation; subsequent invocations append
with --no-header so the output is a single concatenable file.
"""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
import time
from pathlib import Path

VALID_SWEEPS = ("nodes", "pins", "hier")


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser()
    p.add_argument("--sweep", required=True, choices=VALID_SWEEPS,
                   help="Which axis to sweep.")
    p.add_argument("--bench", required=True,
                   help="Path to bench binary (e.g. bazel-bin/hhds/bench/hhds_bench).")
    p.add_argument("--ops", required=True,
                   help="Comma-separated op names (see schema.md).")
    p.add_argument("--sizes", required=True,
                   help="Comma-separated sweep values (e.g. '10,100,1000').")
    p.add_argument("--out", required=True,
                   help="Output CSV path. Parent dir is created if needed.")
    p.add_argument("--runs", type=int, default=5,
                   help="Number of runs per (op, size) cell. Median is what plot.py uses.")
    p.add_argument("--topology", default="chain",
                   help="Synthetic topology (chain/fanout/random_dag/eda_typical).")
    p.add_argument("--seed", default="0xC0FFEE",
                   help="RNG seed for the generator. Same seed = same edges across libs.")
    # Held-constant values for axes NOT being swept. Safe defaults match
    # the plan's workload constants.
    p.add_argument("--fixed-nodes", type=int, default=100_000,
                   help="Node count when --sweep != nodes.")
    p.add_argument("--fixed-pins", type=int, default=1,
                   help="Pins/node when --sweep != pins.")
    p.add_argument("--fixed-hier", type=int, default=1,
                   help="Hier instance count when --sweep != hier.")
    p.add_argument("--timeout-per-cell", type=float, default=120.0,
                   help="Per-(op,size) wall budget in seconds. Cells exceeding this skip.")
    return p.parse_args()


def cell_args(args: argparse.Namespace, op: str, value: int) -> list[str]:
    """Build CLI args for one (op, swept-value) cell."""
    nodes = args.fixed_nodes
    pins  = args.fixed_pins
    hier  = args.fixed_hier
    if args.sweep == "nodes":
        nodes = value
    elif args.sweep == "pins":
        pins = value
    elif args.sweep == "hier":
        hier = value

    return [
        args.bench,
        f"--op={op}",
        f"--topology={args.topology}",
        f"--axis={args.sweep}",
        f"--nodes={nodes}",
        f"--pins={pins}",
        f"--hier={hier}",
        f"--seed={args.seed}",
        f"--runs={args.runs}",
        "--no-header",
    ]


def main() -> int:
    args = parse_args()
    ops = [op.strip() for op in args.ops.split(",") if op.strip()]
    sizes = [int(s.strip()) for s in args.sizes.split(",") if s.strip()]

    out_path = Path(args.out)
    out_path.parent.mkdir(parents=True, exist_ok=True)

    if not Path(args.bench).is_file():
        sys.exit(f"bench binary not found: {args.bench}")

    # Header line — the bench binaries don't write it when --no-header is
    # passed, so the driver writes it once up front.
    header = ("library,op,topology,axis,size,pins_per_node,hier_size,seed,run_idx,"
              "wall_ns,peak_rss_kb,bytes_per_node,l1_misses,llc_misses,instructions,cycles,ipc\n")

    n_cells = len(ops) * len(sizes)
    cell = 0
    t_start = time.time()
    skipped: list[tuple[str, int]] = []

    with out_path.open("w") as fout:
        fout.write(header)
        fout.flush()

        for op in ops:
            for size in sizes:
                cell += 1
                argv = cell_args(args, op, size)
                desc = f"[{cell}/{n_cells}] {op} {args.sweep}={size}"
                print(desc, file=sys.stderr, flush=True)
                t0 = time.time()
                try:
                    proc = subprocess.run(
                        argv,
                        capture_output=True,
                        text=True,
                        timeout=args.timeout_per_cell,
                    )
                except subprocess.TimeoutExpired:
                    elapsed = time.time() - t0
                    print(f"  TIMEOUT after {elapsed:.1f}s — skipping", file=sys.stderr)
                    skipped.append((op, size))
                    continue
                elapsed = time.time() - t0

                if proc.returncode != 0:
                    print(f"  rc={proc.returncode} stderr={proc.stderr.strip()[:200]}",
                          file=sys.stderr)
                    skipped.append((op, size))
                    continue

                fout.write(proc.stdout)
                fout.flush()
                # Quick wall-time signal so the user can spot regressions live.
                lines = [l for l in proc.stdout.strip().split("\n") if l]
                if lines:
                    last_ns = lines[-1].split(",")[9]  # wall_ns column
                    print(f"  ok ({elapsed:.2f}s, last wall_ns={last_ns})",
                          file=sys.stderr)

    total = time.time() - t_start
    print(f"\nWrote {out_path} in {total:.1f}s "
          f"({n_cells - len(skipped)}/{n_cells} cells)",
          file=sys.stderr)
    if skipped:
        print(f"Skipped: {skipped}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
