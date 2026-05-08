#!/bin/bash
# Compare hhds performance with edges in pure inline storage vs pure overflow.
#
#   sedges_only   topology -> 2 edges/node forward
#                              middle nodes have ~4 total edges
#                              all stored in 4-slot sedges_ packed path (fastest)
#                              never enters ledge0/ledge1 / sedges_extra / overflow
#
#   overflow_only topology -> 16 edges/node forward
#                              middle nodes have ~32 total edges
#                              well past 10-edge node-overflow threshold
#                              hhds flushes inline content into overflow set;
#                              all edges live in the overflow set with no
#                              inline content remaining.
#
# Both topologies are run across the same node sweep (10 -> 10^6) and
# emitted into one CSV. plot.py / print_table.py distinguish lines by
# the `topology` column.
#
# Usage:
#   bash run_inline_vs_overflow.sh [output-dir]

set -euo pipefail

cd "$(dirname "$0")/.."  # run from bench/ dir

OUT_DIR="${1:-results/$(date +%Y-%m-%d)}"
mkdir -p "$OUT_DIR"

BENCH="../../bazel-bin/hhds/bench/hhds_bench"

if [[ ! -x "$BENCH" ]]; then
  echo "bench binary missing — run: bazel build //hhds/bench:hhds_bench" >&2
  exit 1
fi

OPS="build_node,build_edges,traverse_fast_class,traverse_forward_class,lookup"
SIZES="10,100,1000,10000,100000,1000000"
CSV="$OUT_DIR/inline_vs_overflow.csv"

# Sedges-only first -> creates CSV with header + sedges rows.
.venv/bin/python scripts/run_sweep.py \
  --sweep nodes \
  --bench "$BENCH" \
  --ops "$OPS" \
  --sizes "$SIZES" \
  --out "$CSV" \
  --runs 5 \
  --topology sedges_only \
  --timeout-per-cell 240

# Overflow-only second -> append to same CSV (skip header).
TMP="$OUT_DIR/_inline_vs_overflow_tmp.csv"
.venv/bin/python scripts/run_sweep.py \
  --sweep nodes \
  --bench "$BENCH" \
  --ops "$OPS" \
  --sizes "$SIZES" \
  --out "$TMP" \
  --runs 5 \
  --topology overflow_only \
  --timeout-per-cell 240

tail -n +2 "$TMP" >> "$CSV"
rm -f "$TMP"

echo ""
echo "Combined CSV: $CSV"
echo ""
echo "Done. Next:"
echo "  .venv/bin/python scripts/plot.py --csv $CSV --out $OUT_DIR/inline_vs_overflow.png --title 'hhds: sedges_only (≤4 edges/node) vs overflow_only (16 edges/node)'"
echo "  .venv/bin/python scripts/print_table.py --csv $CSV"
