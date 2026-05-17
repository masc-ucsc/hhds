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
        fig, ax = plt.subplots(figsize=(8, 5))

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
        ax.legend()
        if all(int(row["x_value"]) > 0 for row in rows):
            ax.set_xscale("log")
        ax.set_yscale("log")
        fig.tight_layout()

        name = safe_name(f"{operation}_{scenario}_{x_axis}.png")
        fig.savefig(out_dir / name, dpi=160)
        plt.close(fig)


if __name__ == "__main__":
    main()
