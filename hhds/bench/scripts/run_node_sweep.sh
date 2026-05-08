#!/bin/bash
# Sweep node count from 10 -> 1,000,000 across the core ops.
# Topology fixed at 'chain' (predictable, linear traversal cost).
# Pins fixed at 1 (pin-0 fast path — best case for hhds).
#
# Usage:
#   bash run_node_sweep.sh [output-dir]
#
# Output:
#   <output-dir>/node_sweep.csv  — master CSV
#
# After this finishes, plot.py and print_table.py consume node_sweep.csv.

set -euo pipefail

cd "$(dirname "$0")/.."  # run from bench/ dir

OUT_DIR="${1:-results/$(date +%Y-%m-%d)}"
mkdir -p "$OUT_DIR"

BENCH="../../bazel-bin/hhds/bench/hhds_bench"

if [[ ! -x "$BENCH" ]]; then
  echo "bench binary missing — run: bazel build //hhds/bench:hhds_bench" >&2
  exit 1
fi

# Five core ops covering creation, edge insertion, traversal (both
# variants), and lookup. add_pin / mutate / hier ops have their own
# sweeps in run_pin_sweep.sh / run_hier_sweep.sh.
OPS="build_node,build_edges,traverse_fast_class,traverse_forward_class,lookup"

# 10, 100, 1K, 10K, 100K, 1M — six points, clean log-log.
SIZES="10,100,1000,10000,100000,1000000"

.venv/bin/python scripts/run_sweep.py \
  --sweep nodes \
  --bench "$BENCH" \
  --ops "$OPS" \
  --sizes "$SIZES" \
  --out "$OUT_DIR/node_sweep.csv" \
  --runs 5 \
  --topology chain

echo ""
echo "Done. Next:"
echo "  .venv/bin/python scripts/plot.py --csv $OUT_DIR/node_sweep.csv --out $OUT_DIR/node_sweep.png --title 'hhds: wall-time vs node count (chain)'"
echo "  .venv/bin/python scripts/print_table.py --csv $OUT_DIR/node_sweep.csv"
