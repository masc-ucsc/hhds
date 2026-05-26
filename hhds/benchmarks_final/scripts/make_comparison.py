#!/usr/bin/env python3
"""Build the wide comparison CSV from per-library raw benchmark CSV files."""

import argparse
import csv
import statistics
from collections import defaultdict
from pathlib import Path


LIBRARIES = ("hhds", "livehd", "boost")


OP_ORDER = {
    "add_nodes": 0,
    "add_nodes_with_pins": 1,
    "add_pins": 2,
    "add_edges": 3,
    "delete_edges": 4,
    "delete_pins": 5,
    "delete_pins_with_edges": 6,
    "delete_nodes_with_edges_and_pins": 7,
    "lookup_nodes": 8,
    "lookup_pins": 9,
    "lookup_edges": 10,
    "traverse_fast_class": 11,
    "traverse_forward_class": 12,
    "traverse_backward_class": 13,
    "traverse_fast_flat": 14,
    "traverse_forward_flat": 15,
    "traverse_backward_flat": 16,
    "traverse_fast_hier": 17,
    "traverse_forward_hier": 18,
    "traverse_backward_hier": 19,
    "traverse_hier_range": 20,
}

SCENARIO_ORDER = {
    "default": 0,
    "sedges_inline": 1,
    "ledge_inline": 2,
    "overflow": 3,
}


def ns_per_op(row):
    wall_ns = int(row["wall_ns"])
    items = int(row["items"])
    if items <= 0:
        return None
    return wall_ns / items


def read_rows(path, rows_by_key):
    csv_path = Path(path)
    if not csv_path.exists():
        return

    with csv_path.open(newline="") as handle:
        reader = csv.DictReader(handle)
        for row in reader:
            if not row.get("wall_ns"):
                continue
            key = (
                row["operation"],
                row["scenario"],
                row["x_axis"],
                int(row["x_value"]),
            )
            library = row["library"]
            value = ns_per_op(row)
            if value is not None:
                rows_by_key[key][library].append(value)


def sort_key(item):
    operation, scenario, x_axis, x_value = item
    return (OP_ORDER.get(operation, 1000), operation, SCENARIO_ORDER.get(scenario, 100), scenario, x_axis, x_value)


def fmt_ns_per_op(values):
    if not values:
        return "N/A"
    return f"{statistics.median(values):.3f}"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--hhds", required=True)
    parser.add_argument("--livehd", required=True)
    parser.add_argument("--boost", required=True)
    parser.add_argument("--out", required=True)
    args = parser.parse_args()

    rows_by_key = defaultdict(lambda: defaultdict(list))
    read_rows(args.hhds, rows_by_key)
    read_rows(args.livehd, rows_by_key)
    read_rows(args.boost, rows_by_key)

    out_path = Path(args.out)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    with out_path.open("w", newline="") as handle:
      writer = csv.writer(handle)
      writer.writerow(["operation", "scenario", "x_axis", "x_value", "time_unit", "hhds", "livehd", "boost"])
      for key in sorted(rows_by_key, key=sort_key):
          row = [key[0], key[1], key[2], key[3], "ns/op"]
          row.extend(fmt_ns_per_op(rows_by_key[key].get(library, [])) for library in LIBRARIES)
          writer.writerow(row)


if __name__ == "__main__":
    main()
