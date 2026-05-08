#!/bin/bash
# Compare hhds performance with vs without edge-storage overflow.
#
#   chain  topology -> 1 edge per node    -> stays in inline storage  (no overflow)
#   dense  topology -> 32 edges per node  -> always spills to overflow scanning array
#
# (hhds's MAX_EDGES = 8 inline slots per node/pin, so 32 forces overflow on every node.)
#
# Both topologies are run across the same node sweep (10 -> 10^6) and
# emitted into a single CSV. plot.py and print_table.py distinguish
# rows by the `topology` column, so the chart shows two lines per op
# (solid = chain, dashed = dense).
#
# Usage:
#   bash run_overflow_compare.sh [output-dir]

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
CSV="$OUT_DIR/overflow_compare.csv"

# Run chain first -> writes header + chain rows.
.venv/bin/python scripts/run_sweep.py \
  --sweep nodes \
  --bench "$BENCH" \
  --ops "$OPS" \
  --sizes "$SIZES" \
  --out "$CSV" \
  --runs 5 \
  --topology chain \
  --timeout-per-cell 180

# Run dense second -> appends rows to same CSV. The header is already
# present, and the bench binary uses --no-header from run_sweep.py.
# But run_sweep.py's --out creates a new file, so we need a small trick:
# write dense to a temp CSV, then append dense's data rows (skipping the
# header) to the master.
DENSE_CSV="$OUT_DIR/_overflow_dense_tmp.csv"
.venv/bin/python scripts/run_sweep.py \
  --sweep nodes \
  --bench "$BENCH" \
  --ops "$OPS" \
  --sizes "$SIZES" \
  --out "$DENSE_CSV" \
  --runs 5 \
  --topology dense \
  --timeout-per-cell 180

# Append all but the header line from the dense CSV to the chain CSV.
tail -n +2 "$DENSE_CSV" >> "$CSV"
rm -f "$DENSE_CSV"

echo ""
echo "Combined CSV: $CSV"
echo ""
echo "Done. Next:"
echo "  .venv/bin/python scripts/plot.py --csv $CSV --out $OUT_DIR/overflow_compare.png --title 'hhds: chain (no overflow) vs dense (always overflow)'"
echo "  .venv/bin/python scripts/print_table.py --csv $CSV"
