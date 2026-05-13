#!/usr/bin/env python3
"""Print a markdown-formatted comparison table from a master CSV.

Pivot: rows = sweep value (size), columns = (library, op). One cell =
median wall_ns formatted with SI suffix (ns / µs / ms / s). Useful to
paste directly into the thesis chapter.

Example:
    python3 print_table.py --csv results/node_sweep.csv
"""

from __future__ import annotations

import argparse
import sys

import pandas as pd


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser()
    p.add_argument("--csv", required=True)
    p.add_argument("--metric", default="wall_ns",
                   choices=["wall_ns", "peak_rss_kb", "bytes_per_node",
                            "l1_misses", "llc_misses", "instructions", "cycles", "ipc"],
                   help="Which CSV column to render (median per cell).")
    return p.parse_args()


def fmt_ns(v: float) -> str:
    if pd.isna(v):
        return "—"
    if v < 1_000:
        return f"{v:.0f} ns"
    if v < 1_000_000:
        return f"{v/1_000:.1f} µs"
    if v < 1_000_000_000:
        return f"{v/1_000_000:.2f} ms"
    return f"{v/1_000_000_000:.2f} s"


def fmt_int(v: float) -> str:
    if pd.isna(v):
        return "—"
    return f"{int(v):,}"


def fmt_float(v: float) -> str:
    if pd.isna(v):
        return "—"
    return f"{v:.3f}"


FORMATTERS = {
    "wall_ns": fmt_ns,
    "peak_rss_kb": fmt_int,
    "bytes_per_node": fmt_float,
    "l1_misses": fmt_int,
    "llc_misses": fmt_int,
    "instructions": fmt_int,
    "cycles": fmt_int,
    "ipc": fmt_float,
}


def main() -> int:
    args = parse_args()
    df = pd.read_csv(args.csv)
    if df.empty:
        sys.exit(f"{args.csv}: no rows")

    axis = df["axis"].iloc[0]
    fmt = FORMATTERS[args.metric]

    # The CSV's `size` column is always node count. Pin/hier sweeps need
    # to use their own axis column for the row index.
    axis_col = {"nodes": "size", "pins": "pins_per_node", "hier": "hier_size"}[axis]

    # Median per (library, topology, op, axis_value). Topology is in the
    # group key so overflow-comparison runs (chain vs dense) become side-
    # by-side columns instead of getting averaged together.
    pivot = (df.groupby(["library", "topology", "op", axis_col])[args.metric]
               .median()
               .unstack(["library", "topology", "op"])
               .sort_index())

    # Format header. Drop axes that only have one unique value so the
    # column names stay readable.
    cols = pivot.columns.tolist()  # list of (library, topology, op) tuples
    one_lib = all(lib == cols[0][0] for lib, _, _ in cols)
    one_topo = all(topo == cols[0][1] for _, topo, _ in cols)
    def col_name(lib: str, topo: str, op: str) -> str:
        parts: list[str] = []
        if not one_lib:
            parts.append(lib)
        parts.append(op)
        if not one_topo:
            parts.append(f"({topo})")
        return " ".join(parts)
    header = ["size"] + [col_name(lib, topo, op) for lib, topo, op in cols]

    # Header label tracks which axis was actually swept.
    axis_header = {"nodes": "nodes", "pins": "pins/node", "hier": "instances"}[axis]
    header[0] = axis_header

    print(f"# {axis} sweep — {args.metric} (median across runs)\n")
    print("| " + " | ".join(header) + " |")
    print("|" + "|".join(["---"] * len(header)) + "|")
    for value, row in pivot.iterrows():
        cells = [f"{int(value):,}"] + [fmt(row[col]) for col in cols]
        print("| " + " | ".join(cells) + " |")

    return 0


if __name__ == "__main__":
    sys.exit(main())
