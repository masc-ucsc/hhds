#!/usr/bin/env python3
"""Render one plot per operation/scenario from comparison.csv."""

import argparse
import csv
import os
import re
import tempfile
from collections import defaultdict
from pathlib import Path

os.environ.setdefault("MPLCONFIGDIR", str(Path(tempfile.gettempdir()) / "hhds_benchmarks_final_mpl"))

import matplotlib.pyplot as plt

# Roughly 2x matplotlib defaults for everything near the axes -- title, axis
# labels, tick labels -- so the plots are readable in the thesis PDF without
# zooming (reviewer request). The legend is intentionally kept small (it is
# not a label and the figure caption already names the libraries).
plt.rcParams.update({
    "font.size":        20,
    "axes.titlesize":   24,
    "axes.titlepad":    14,  # extra breathing room between title and axes
    "axes.labelsize":   22,
    "xtick.labelsize":  20,
    "ytick.labelsize":  20,
    "legend.fontsize":  18,  # color key for the libraries, sits below the plot
    "lines.linewidth":  2.5,
    "lines.markersize": 9,
})


LIBRARIES = ("hhds", "livehd", "boost")
COLORS = {
    "hhds": "#d62728",
    "livehd": "#2ca02c",
    "boost": "#1f77b4",
}


def safe_name(text):
    return re.sub(r"[^A-Za-z0-9_.-]+", "_", text).strip("_")


def numeric(value):
    if value == "N/A" or value == "":
        return None
    return float(value)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--csv", required=True)
    parser.add_argument("--out-dir", required=True)
    args = parser.parse_args()

    groups = defaultdict(list)
    with Path(args.csv).open(newline="") as handle:
        for row in csv.DictReader(handle):
            groups[(row["operation"], row["scenario"], row["x_axis"])].append(row)

    out_dir = Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)

    for (operation, scenario, x_axis), rows in sorted(groups.items()):
        rows.sort(key=lambda row: int(row["x_value"]))
        # constrained_layout is the only layout engine that properly accounts
        # for the out-of-axes legend (set via bbox_to_anchor below). The
        # earlier combination of tight_layout() + bbox_inches="tight" left
        # the title visibly hugging the top because tight_layout doesn't see
        # the legend at all.
        fig, ax = plt.subplots(figsize=(8, 6.5), constrained_layout=True)

        plotted = False
        for library in LIBRARIES:
            xs = []
            ys = []
            for row in rows:
                y = numeric(row[library])
                if y is None:
                    continue
                xs.append(int(row["x_value"]))
                ys.append(y)
            if xs:
                ax.plot(xs, ys, marker="o", linewidth=2, label=library, color=COLORS[library])
                plotted = True

        if not plotted:
            plt.close(fig)
            continue

        ax.set_title(f"{operation} / {scenario}")
        ax.set_xlabel(x_axis.replace("_", " "))
        time_unit = rows[0].get("time_unit", "ns/op")
        ax.set_ylabel(f"median time ({time_unit})")
        ax.grid(True, which="both", linestyle="--", alpha=0.35)
        # Place the legend OUTSIDE the axes, below the x-axis, as a single
        # horizontal row. Guarantees zero overlap with any data line and
        # stays in the same place across all panels.
        ax.legend(
            loc="upper center",
            bbox_to_anchor=(0.5, -0.18),
            ncol=3,
            frameon=False,
        )
        if all(int(row["x_value"]) > 0 for row in rows):
            ax.set_xscale("log")
        ax.set_yscale("log")
        # No fig.tight_layout(); constrained_layout already handles spacing.

        # Vector PDF output: crisp at any zoom, selectable text, print-quality.
        # dpi is irrelevant for a vector format.
        name = safe_name(f"{operation}_{scenario}_{x_axis}.pdf")
        fig.savefig(out_dir / name)
        plt.close(fig)


if __name__ == "__main__":
    main()
