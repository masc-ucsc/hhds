#!/usr/bin/env python3
"""Plot a scaling chart from a master CSV produced by run_sweep.py.

Output is a log-log line plot (one line per op, x = swept axis value,
y = median wall_ns over all runs in that cell). Optionally splits by
library when multiple libraries appear in the same CSV.

Example:
    python3 plot.py \\
        --csv results/node_sweep.csv \\
        --out results/node_sweep.png \\
        --title "hhds: wall-time vs node count (chain topology)"
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

import matplotlib
matplotlib.use("Agg")  # headless — no DISPLAY needed
import matplotlib.pyplot as plt
import pandas as pd


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser()
    p.add_argument("--csv", required=True)
    p.add_argument("--out", required=True)
    p.add_argument("--title", default="")
    p.add_argument("--y", default="wall_ns",
                   help="Column to plot on y axis (wall_ns / peak_rss_kb / bytes_per_node).")
    p.add_argument("--y-label", default="",
                   help="Override y-axis label. Default derived from --y.")
    return p.parse_args()


Y_LABELS = {
    "wall_ns": "wall-clock (ns, median)",
    "peak_rss_kb": "peak RSS (kB)",
    "bytes_per_node": "bytes per node",
    "instructions": "instructions",
    "cycles": "cycles",
    "ipc": "instructions per cycle",
}

X_LABELS = {
    "nodes": "node count",
    "pins": "pins per node",
    "hier": "submodule instances",
}


def main() -> int:
    args = parse_args()

    df = pd.read_csv(args.csv)
    if df.empty:
        sys.exit(f"{args.csv}: no rows")

    axis = df["axis"].iloc[0]
    if not (df["axis"] == axis).all():
        sys.exit(f"CSV mixes axes ({df['axis'].unique()}) — can't plot one chart")

    # The CSV's `size` column always means node count, but the axis being
    # swept may be pins or hier. Pick the right column to plot on x.
    axis_col = {"nodes": "size", "pins": "pins_per_node", "hier": "hier_size"}[axis]
    df = df.rename(columns={axis_col: "_x"})

    # Group by (library, topology, op, _x). Topology is included so that
    # overflow-comparison runs (chain vs dense) plot as separate lines.
    grp = df.groupby(["library", "topology", "op", "_x"])[args.y].median().reset_index()

    fig, ax = plt.subplots(figsize=(10, 6))

    libs = sorted(grp["library"].unique())
    topos = sorted(grp["topology"].unique())
    ops = sorted(grp["op"].unique())

    # Color palette: matplotlib default cycles 10 colors; for >10 ops we
    # rely on tab20.
    cmap = plt.get_cmap("tab20" if len(ops) > 10 else "tab10")
    op_color = {op: cmap(i % cmap.N) for i, op in enumerate(ops)}

    # Markers distinguish the OTHER axes (library and/or topology) so a
    # single op's color appears as a family of related lines.
    markers = ["o", "s", "^", "D", "v", "P", "X", "*"]
    # Pair each (library, topology) combo with one marker.
    lib_topo_marker = {}
    for i, (lib, topo) in enumerate(sorted({(l, t) for l in libs for t in topos})):
        lib_topo_marker[(lib, topo)] = markers[i % len(markers)]
    # Dashed line style for "overflow-heavy" topologies so chain vs dense
    # is visually obvious even in greyscale.
    overflow_topos = {"dense"}

    for lib in libs:
        for topo in topos:
            for op in ops:
                sub = grp[(grp["library"] == lib) & (grp["topology"] == topo) & (grp["op"] == op)].sort_values("_x")
                if sub.empty:
                    continue
                # Build label: drop redundant axes when only one value present.
                parts = [op]
                if len(libs) > 1:
                    parts.insert(0, lib)
                if len(topos) > 1:
                    parts.append(f"({topo})")
                label = " ".join(parts) if len(parts) > 1 else op
                ax.plot(sub["_x"], sub[args.y],
                        marker=lib_topo_marker[(lib, topo)],
                        color=op_color[op],
                        linewidth=1.5,
                        markersize=6,
                        linestyle="--" if topo in overflow_topos else "-",
                        label=label)

    ax.set_xscale("log")
    ax.set_yscale("log")
    ax.set_xlabel(X_LABELS.get(axis, axis))
    ax.set_ylabel(args.y_label or Y_LABELS.get(args.y, args.y))
    if args.title:
        ax.set_title(args.title)
    ax.grid(True, which="both", linestyle="--", alpha=0.4)
    ax.legend(loc="best", fontsize=9, ncol=2 if len(ops) > 6 else 1)

    out_path = Path(args.out)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    fig.tight_layout()
    fig.savefig(out_path, dpi=144)
    print(f"Wrote {out_path} ({len(grp)} (lib, op, size) groups)", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
