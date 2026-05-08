#!/bin/bash
# Sweep pins-per-node across 1, 2, 4, 8, 16, 64, 256 at fixed N=1000.
# Values chosen to cross hhds's three storage regimes:
#   1            -> pin-0 fast path (no pin entry)
#   2..16        -> inline edges in Pin_class
#   64           -> overflow scanning array
#   256          -> emhash8 hashset escape hatch
# This is the headline thesis figure for the inline -> overflow ->
# hashset transition.
#
# Usage:
#   bash run_pin_sweep.sh [output-dir]

set -euo pipefail

cd "$(dirname "$0")/.."  # run from bench/ dir

OUT_DIR="${1:-results/$(date +%Y-%m-%d)}"
mkdir -p "$OUT_DIR"

BENCH="../../bazel-bin/hhds/bench/hhds_bench"

if [[ ! -x "$BENCH" ]]; then
  echo "bench binary missing — run: bazel build //hhds/bench:hhds_bench" >&2
  exit 1
fi

OPS="add_pin"
SIZES="1,2,4,8,16,64,256"

.venv/bin/python scripts/run_sweep.py \
  --sweep pins \
  --bench "$BENCH" \
  --ops "$OPS" \
  --sizes "$SIZES" \
  --out "$OUT_DIR/pin_sweep.csv" \
  --runs 5 \
  --fixed-nodes 1000

echo ""
echo "Done. Next:"
echo "  .venv/bin/python scripts/plot.py --csv $OUT_DIR/pin_sweep.csv --out $OUT_DIR/pin_sweep.png --title 'hhds add_pin: storage-regime crossover (N=1000)'"
echo "  .venv/bin/python scripts/print_table.py --csv $OUT_DIR/pin_sweep.csv"
