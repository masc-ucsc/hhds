#!/usr/bin/env bash
# Fast companion to ../scripts/run_all.sh -- smaller sizes, 3 runs, runs the
# three libraries (HHDS, Boost, LiveHD) in parallel. All output stays inside
# this directory (hhds/benchmarks_final/smaller_benchmarks/) so it does not
# collide with the slow run's CSVs at the parent benchmarks_final/ level.
#
# Safe to run while ../scripts/run_all.sh is still grinding in the background;
# the only shared resource is the bazel-bin/ benchmark binaries (read-only).
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
BENCH_DIR="$REPO_ROOT/hhds/benchmarks_final"
SMALL_DIR="$SCRIPT_DIR"

HHDS_BIN="$REPO_ROOT/bazel-bin/hhds/benchmarks_final/hhds/hhds_final_bench"
BOOST_BIN="$REPO_ROOT/bazel-bin/hhds/benchmarks_final/boost/boost_final_bench"
LIVEHD_ROOT="${LIVEHD_ROOT:-$(cd "$REPO_ROOT/.." && pwd)/livehd}"
LIVEHD_BIN="$LIVEHD_ROOT/bazel-bin/lgraph/livehd_final_bench"

HHDS_CSV="$SMALL_DIR/hhds/results.csv"
BOOST_CSV="$SMALL_DIR/boost/results.csv"
LIVEHD_CSV="$SMALL_DIR/livehd/results.csv"
COMPARISON_CSV="$SMALL_DIR/comparisons/comparison.csv"
PLOT_DIR="$SMALL_DIR/comparisons/plot"
LOG_DIR="$SMALL_DIR/logs"

RUNS="${RUNS:-3}"
NODE_SIZES="${NODE_SIZES:-1000 5000 10000 50000 100000 500000 1000000 5000000}"
PIN_SIZES="${PIN_SIZES:-4 8 16 32 64}"
HIER_NODE_SIZES="${HIER_NODE_SIZES:-10000 100000}"
HIER_SIZES="${HIER_SIZES:-1000 10000}"
PIN_NODE_COUNT="${PIN_NODE_COUNT:-100000}"
FIXED_HIER_SIZE="${FIXED_HIER_SIZE:-100}"
HIER_RANGE_NODE_COUNT="${HIER_RANGE_NODE_COUNT:-10000}"

if [[ -x "$REPO_ROOT/hhds/bench/.venv/bin/python" ]]; then
  PYTHON="${PYTHON:-$REPO_ROOT/hhds/bench/.venv/bin/python}"
else
  PYTHON="${PYTHON:-python3}"
fi

# Verify binaries already exist -- this script does NOT trigger bazel build,
# the slow run owns that. If the binaries are missing, ask the user to wait
# for the slow run's build step or build manually.
for bin in "$HHDS_BIN" "$BOOST_BIN"; do
  if [[ ! -x "$bin" ]]; then
    echo "ERROR: required binary not built: $bin" >&2
    echo "       Run 'bazel build //hhds/benchmarks_final/hhds:hhds_final_bench //hhds/benchmarks_final/boost:boost_final_bench'" >&2
    echo "       (or wait for ../scripts/run_all.sh to finish its bazel step first)" >&2
    exit 1
  fi
done
LIVEHD_AVAILABLE=0
if [[ -x "$LIVEHD_BIN" ]]; then
  LIVEHD_AVAILABLE=1
fi

mkdir -p "$SMALL_DIR/hhds" "$SMALL_DIR/boost" "$SMALL_DIR/livehd" \
         "$SMALL_DIR/comparisons" "$PLOT_DIR" "$LOG_DIR"

HEADER="library,operation,scenario,x_axis,x_value,nodes,pins_per_node,hier_size,k,run_idx,wall_ns,items"
printf '%s\n' "$HEADER" > "$HHDS_CSV"
printf '%s\n' "$HEADER" > "$BOOST_CSV"
printf '%s\n' "$HEADER" > "$LIVEHD_CSV"

run_one() {
  local bin="$1"
  local out="$2"
  local op="$3"
  local scenario="$4"
  local x_axis="$5"
  local nodes="$6"
  local pins="$7"
  local hier="$8"
  local k="$9"

  local label
  label="$(basename "$bin")"
  printf "  %-25s op=%-32s scenario=%-15s nodes=%-10s pins=%-5s hier=%-5s ... " \
    "$label" "$op" "$scenario" "$nodes" "$pins" "$hier"
  local start=$SECONDS

  # A failed cell (e.g. LiveHD overflow >= 10k) just leaves no rows in the CSV
  # -- the comparison stage renders that as N/A -- instead of aborting the
  # whole parallel sweep.
  if "$bin" \
       --op="$op" \
       --scenario="$scenario" \
       --x-axis="$x_axis" \
       --nodes="$nodes" \
       --pins="$pins" \
       --hier="$hier" \
       --k="$k" \
       --runs="$RUNS" \
       --no-header >> "$out" 2>/dev/null; then
    echo "OK ($((SECONDS - start)) s)"
  else
    echo "FAILED ($((SECONDS - start)) s) -- skipping cell"
  fi
}

run_hhds_all() {
  for nodes in $NODE_SIZES; do
    run_one "$HHDS_BIN" "$HHDS_CSV" add_nodes default nodes "$nodes" 1 "$FIXED_HIER_SIZE" 0
    run_one "$HHDS_BIN" "$HHDS_CSV" add_nodes_with_pins default nodes "$nodes" 8 "$FIXED_HIER_SIZE" 0
    run_one "$HHDS_BIN" "$HHDS_CSV" lookup_nodes default nodes "$nodes" 1 "$FIXED_HIER_SIZE" 0
    run_one "$HHDS_BIN" "$HHDS_CSV" delete_nodes_with_edges_and_pins default nodes "$nodes" 8 "$FIXED_HIER_SIZE" 0
  done
  for pins in $PIN_SIZES; do
    run_one "$HHDS_BIN" "$HHDS_CSV" add_pins default pins "$PIN_NODE_COUNT" "$pins" "$FIXED_HIER_SIZE" 0
    run_one "$HHDS_BIN" "$HHDS_CSV" delete_pins default pins "$PIN_NODE_COUNT" "$pins" "$FIXED_HIER_SIZE" 0
    run_one "$HHDS_BIN" "$HHDS_CSV" delete_pins_with_edges default pins "$PIN_NODE_COUNT" "$pins" "$FIXED_HIER_SIZE" 0
    run_one "$HHDS_BIN" "$HHDS_CSV" lookup_pins default pins "$PIN_NODE_COUNT" "$pins" "$FIXED_HIER_SIZE" 0
  done
  for scenario in sedges_inline ledge_inline overflow; do
    for nodes in $NODE_SIZES; do
      run_one "$HHDS_BIN" "$HHDS_CSV" add_edges "$scenario" nodes "$nodes" 1 "$FIXED_HIER_SIZE" 0
      run_one "$HHDS_BIN" "$HHDS_CSV" delete_edges "$scenario" nodes "$nodes" 1 "$FIXED_HIER_SIZE" 0
      run_one "$HHDS_BIN" "$HHDS_CSV" lookup_edges "$scenario" nodes "$nodes" 1 "$FIXED_HIER_SIZE" 0
      run_one "$HHDS_BIN" "$HHDS_CSV" traverse_fast_class "$scenario" nodes "$nodes" 1 "$FIXED_HIER_SIZE" 0
      run_one "$HHDS_BIN" "$HHDS_CSV" traverse_forward_class "$scenario" nodes "$nodes" 1 "$FIXED_HIER_SIZE" 0
      run_one "$HHDS_BIN" "$HHDS_CSV" traverse_backward_class "$scenario" nodes "$nodes" 1 "$FIXED_HIER_SIZE" 0
    done
  done
  for nodes in $HIER_NODE_SIZES; do
    run_one "$HHDS_BIN" "$HHDS_CSV" traverse_fast_flat default nodes "$nodes" 1 "$FIXED_HIER_SIZE" 0
    run_one "$HHDS_BIN" "$HHDS_CSV" traverse_forward_flat default nodes "$nodes" 1 "$FIXED_HIER_SIZE" 0
    run_one "$HHDS_BIN" "$HHDS_CSV" traverse_backward_flat default nodes "$nodes" 1 "$FIXED_HIER_SIZE" 0
    run_one "$HHDS_BIN" "$HHDS_CSV" traverse_fast_hier default nodes "$nodes" 1 "$FIXED_HIER_SIZE" 0
    run_one "$HHDS_BIN" "$HHDS_CSV" traverse_forward_hier default nodes "$nodes" 1 "$FIXED_HIER_SIZE" 0
    run_one "$HHDS_BIN" "$HHDS_CSV" traverse_backward_hier default nodes "$nodes" 1 "$FIXED_HIER_SIZE" 0
  done
  for hier in $HIER_SIZES; do
    run_one "$HHDS_BIN" "$HHDS_CSV" traverse_hier_range default hier "$HIER_RANGE_NODE_COUNT" 1 "$hier" 0
  done
}

run_boost_all() {
  for nodes in $NODE_SIZES; do
    run_one "$BOOST_BIN" "$BOOST_CSV" add_nodes default nodes "$nodes" 1 "$FIXED_HIER_SIZE" 0
    run_one "$BOOST_BIN" "$BOOST_CSV" lookup_nodes default nodes "$nodes" 1 "$FIXED_HIER_SIZE" 0
    run_one "$BOOST_BIN" "$BOOST_CSV" delete_nodes_with_edges_and_pins default nodes "$nodes" 1 "$FIXED_HIER_SIZE" 0
  done
  for scenario in sedges_inline ledge_inline overflow; do
    for nodes in $NODE_SIZES; do
      run_one "$BOOST_BIN" "$BOOST_CSV" add_edges "$scenario" nodes "$nodes" 1 "$FIXED_HIER_SIZE" 0
      run_one "$BOOST_BIN" "$BOOST_CSV" delete_edges "$scenario" nodes "$nodes" 1 "$FIXED_HIER_SIZE" 0
      run_one "$BOOST_BIN" "$BOOST_CSV" lookup_edges "$scenario" nodes "$nodes" 1 "$FIXED_HIER_SIZE" 0
      run_one "$BOOST_BIN" "$BOOST_CSV" traverse_fast_class "$scenario" nodes "$nodes" 1 "$FIXED_HIER_SIZE" 0
      run_one "$BOOST_BIN" "$BOOST_CSV" traverse_forward_class "$scenario" nodes "$nodes" 1 "$FIXED_HIER_SIZE" 0
      run_one "$BOOST_BIN" "$BOOST_CSV" traverse_backward_class "$scenario" nodes "$nodes" 1 "$FIXED_HIER_SIZE" 0
    done
  done
}

run_livehd_all() {
  for nodes in $NODE_SIZES; do
    run_one "$LIVEHD_BIN" "$LIVEHD_CSV" add_nodes default nodes "$nodes" 1 "$FIXED_HIER_SIZE" 0
    run_one "$LIVEHD_BIN" "$LIVEHD_CSV" add_nodes_with_pins default nodes "$nodes" 8 "$FIXED_HIER_SIZE" 0
    run_one "$LIVEHD_BIN" "$LIVEHD_CSV" lookup_nodes default nodes "$nodes" 1 "$FIXED_HIER_SIZE" 0
    run_one "$LIVEHD_BIN" "$LIVEHD_CSV" delete_nodes_with_edges_and_pins default nodes "$nodes" 8 "$FIXED_HIER_SIZE" 0
  done
  for pins in $PIN_SIZES; do
    run_one "$LIVEHD_BIN" "$LIVEHD_CSV" add_pins default pins "$PIN_NODE_COUNT" "$pins" "$FIXED_HIER_SIZE" 0
    run_one "$LIVEHD_BIN" "$LIVEHD_CSV" delete_pins default pins "$PIN_NODE_COUNT" "$pins" "$FIXED_HIER_SIZE" 0
    run_one "$LIVEHD_BIN" "$LIVEHD_CSV" delete_pins_with_edges default pins "$PIN_NODE_COUNT" "$pins" "$FIXED_HIER_SIZE" 0
    run_one "$LIVEHD_BIN" "$LIVEHD_CSV" lookup_pins default pins "$PIN_NODE_COUNT" "$pins" "$FIXED_HIER_SIZE" 0
  done
  for scenario in sedges_inline ledge_inline overflow; do
    for nodes in $NODE_SIZES; do
      run_one "$LIVEHD_BIN" "$LIVEHD_CSV" add_edges "$scenario" nodes "$nodes" 1 "$FIXED_HIER_SIZE" 0
      run_one "$LIVEHD_BIN" "$LIVEHD_CSV" delete_edges "$scenario" nodes "$nodes" 1 "$FIXED_HIER_SIZE" 0
      run_one "$LIVEHD_BIN" "$LIVEHD_CSV" lookup_edges "$scenario" nodes "$nodes" 1 "$FIXED_HIER_SIZE" 0
      run_one "$LIVEHD_BIN" "$LIVEHD_CSV" traverse_fast_class "$scenario" nodes "$nodes" 1 "$FIXED_HIER_SIZE" 0
      run_one "$LIVEHD_BIN" "$LIVEHD_CSV" traverse_forward_class "$scenario" nodes "$nodes" 1 "$FIXED_HIER_SIZE" 0
      # LiveHD's backward iterator is unimplemented -- skip; the comparison
      # stage will fill those cells with N/A.
    done
  done
  for nodes in $HIER_NODE_SIZES; do
    run_one "$LIVEHD_BIN" "$LIVEHD_CSV" traverse_fast_flat default nodes "$nodes" 1 "$FIXED_HIER_SIZE" 0
    run_one "$LIVEHD_BIN" "$LIVEHD_CSV" traverse_forward_flat default nodes "$nodes" 1 "$FIXED_HIER_SIZE" 0
    run_one "$LIVEHD_BIN" "$LIVEHD_CSV" traverse_fast_hier default nodes "$nodes" 1 "$FIXED_HIER_SIZE" 0
    run_one "$LIVEHD_BIN" "$LIVEHD_CSV" traverse_forward_hier default nodes "$nodes" 1 "$FIXED_HIER_SIZE" 0
  done
  for hier in $HIER_SIZES; do
    run_one "$LIVEHD_BIN" "$LIVEHD_CSV" traverse_hier_range default hier "$HIER_RANGE_NODE_COUNT" 1 "$hier" 0
  done
}

echo "Launching three libraries in parallel..."
run_hhds_all  > "$LOG_DIR/hhds.log"   2>&1 &
HHDS_PID=$!
run_boost_all > "$LOG_DIR/boost.log"  2>&1 &
BOOST_PID=$!
LIVEHD_PID=""
if [[ "$LIVEHD_AVAILABLE" == "1" ]]; then
  run_livehd_all > "$LOG_DIR/livehd.log" 2>&1 &
  LIVEHD_PID=$!
else
  echo "Note: LiveHD binary not found at $LIVEHD_BIN -- livehd/results.csv stays header-only."
fi

echo "PIDs: HHDS=$HHDS_PID Boost=$BOOST_PID${LIVEHD_PID:+ LiveHD=$LIVEHD_PID}"
echo ""
echo "Per-library progress:"
echo "  tail -f $LOG_DIR/hhds.log"
echo "  tail -f $LOG_DIR/boost.log"
[[ -n "$LIVEHD_PID" ]] && echo "  tail -f $LOG_DIR/livehd.log"
echo ""
echo "Waiting for all libraries to finish..."
wait
echo "All libraries done."

echo ""
echo "Building comparison.csv and plots..."
"$PYTHON" "$BENCH_DIR/scripts/make_comparison.py" \
  --hhds "$HHDS_CSV" \
  --livehd "$LIVEHD_CSV" \
  --boost "$BOOST_CSV" \
  --out "$COMPARISON_CSV"

"$PYTHON" "$BENCH_DIR/scripts/plot_results.py" \
  --csv "$COMPARISON_CSV" \
  --out-dir "$PLOT_DIR"

echo ""
echo "Done."
echo "  Output folder:   $SMALL_DIR"
echo "  Comparison CSV:  $COMPARISON_CSV"
echo "  Plots:           $PLOT_DIR"
echo "  Per-library logs:$LOG_DIR/{hhds,boost,livehd}.log"
