#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
BENCH_DIR="$REPO_ROOT/hhds/benchmarks_final"

HHDS_BIN="$REPO_ROOT/bazel-bin/hhds/benchmarks_final/hhds/hhds_final_bench"
BOOST_BIN="$REPO_ROOT/bazel-bin/hhds/benchmarks_final/boost/boost_final_bench"
LIVEHD_ROOT="${LIVEHD_ROOT:-$(cd "$REPO_ROOT/.." && pwd)/livehd}"
LIVEHD_BIN="$LIVEHD_ROOT/bazel-bin/lgraph/livehd_final_bench"

HHDS_CSV="$BENCH_DIR/hhds/results.csv"
BOOST_CSV="$BENCH_DIR/boost/results.csv"
LIVEHD_CSV="$BENCH_DIR/livehd/results.csv"
COMPARISON_CSV="$BENCH_DIR/comparisons/comparison.csv"
PLOT_DIR="$BENCH_DIR/comparisons/plot"

RUNS="${RUNS:-5}"
NODE_SIZES="${NODE_SIZES:-100 1000 10000}"
PIN_SIZES="${PIN_SIZES:-2 4 8 16 32}"
HIER_NODE_SIZES="${HIER_NODE_SIZES:-100 1000}"
HIER_SIZES="${HIER_SIZES:-10 100 1000}"
PIN_NODE_COUNT="${PIN_NODE_COUNT:-10000}"
FIXED_HIER_SIZE="${FIXED_HIER_SIZE:-100}"
HIER_RANGE_NODE_COUNT="${HIER_RANGE_NODE_COUNT:-1000}"

if [[ -x "$REPO_ROOT/hhds/bench/.venv/bin/python" ]]; then
  PYTHON="${PYTHON:-$REPO_ROOT/hhds/bench/.venv/bin/python}"
else
  PYTHON="${PYTHON:-python3}"
fi

cd "$REPO_ROOT"
bazel build //hhds/benchmarks_final/hhds:hhds_final_bench //hhds/benchmarks_final/boost:boost_final_bench

LIVEHD_AVAILABLE=0
if [[ -f "$LIVEHD_ROOT/lgraph/tests/livehd_final_bench.cpp" ]]; then
  echo "Building LiveHD benchmark..."
  (cd "$LIVEHD_ROOT" && bazel build //lgraph:livehd_final_bench)
  LIVEHD_AVAILABLE=1
fi

mkdir -p "$BENCH_DIR/hhds" "$BENCH_DIR/boost" "$BENCH_DIR/livehd" "$BENCH_DIR/comparisons" "$PLOT_DIR"
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

  "$bin" \
    --op="$op" \
    --scenario="$scenario" \
    --x-axis="$x_axis" \
    --nodes="$nodes" \
    --pins="$pins" \
    --hier="$hier" \
    --k="$k" \
    --runs="$RUNS" \
    --no-header >> "$out"
}

echo "Running HHDS benchmarks..."
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

echo "Running Boost benchmarks..."
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

if [[ "$LIVEHD_AVAILABLE" == "1" ]]; then
  echo "Running LiveHD benchmarks..."
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
      # LiveHD currently declares backward(), but its implementation still
      # contains a FIXME/assert. Leave backward LiveHD rows absent => N/A.
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
else
  echo "LiveHD benchmark source not found at $LIVEHD_ROOT; livehd/results.csv is header-only."
fi

"$PYTHON" "$BENCH_DIR/scripts/make_comparison.py" \
  --hhds "$HHDS_CSV" \
  --livehd "$LIVEHD_CSV" \
  --boost "$BOOST_CSV" \
  --out "$COMPARISON_CSV"

"$PYTHON" "$BENCH_DIR/scripts/plot_results.py" \
  --csv "$COMPARISON_CSV" \
  --out-dir "$PLOT_DIR"

echo "Done."
echo "Raw HHDS CSV:      $HHDS_CSV"
echo "Raw Boost CSV:     $BOOST_CSV"
echo "Raw LiveHD CSV:    $LIVEHD_CSV"
echo "Comparison CSV:    $COMPARISON_CSV"
echo "Plots folder:      $PLOT_DIR"
