#!/bin/bash
# Sweep submodule-instance count from 1 -> 1000 at a fixed inner-graph
# size (1000 nodes per sub). This is the hierarchy-scaling axis: tests
# how flat / hier / hier_range traversal cost grows as the same class
# graph is instantiated more times.
#
# Usage:
#   bash run_hier_sweep.sh [output-dir]

set -euo pipefail

cd "$(dirname "$0")/.."  # run from bench/ dir

OUT_DIR="${1:-results/$(date +%Y-%m-%d)}"
mkdir -p "$OUT_DIR"

BENCH="../../bazel-bin/hhds/bench/hhds_bench"

if [[ ! -x "$BENCH" ]]; then
  echo "bench binary missing — run: bazel build //hhds/bench:hhds_bench" >&2
  exit 1
fi

# All five hier-aware traversals plus hier_range.
OPS="traverse_fast_flat,traverse_forward_flat,traverse_fast_hier,traverse_forward_hier,traverse_hier_range"

# 1, 10, 100, 1000 — keeps total node count reasonable. With N_inner=1000:
#   hier=1   -> 1K nodes total
#   hier=1K  -> 1M nodes total
SIZES="1,10,100,1000"

.venv/bin/python scripts/run_sweep.py \
  --sweep hier \
  --bench "$BENCH" \
  --ops "$OPS" \
  --sizes "$SIZES" \
  --out "$OUT_DIR/hier_sweep.csv" \
  --runs 5 \
  --fixed-nodes 1000

echo ""
echo "Done. Next:"
echo "  .venv/bin/python scripts/plot.py --csv $OUT_DIR/hier_sweep.csv --out $OUT_DIR/hier_sweep.png --title 'hhds: hier traversals vs instance count (N_inner=1000)'"
echo "  .venv/bin/python scripts/print_table.py --csv $OUT_DIR/hier_sweep.csv"
