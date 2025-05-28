#!/bin/bash

CRITERION_DIR="target/criterion"

if [ ! -d "$CRITERION_DIR" ]; then
  echo "Criterion output directory not found: $CRITERION_DIR"
  exit 1
fi

echo "Benchmark Results (mean in ns):"
echo "-------------------------------"

find "$CRITERION_DIR" -mindepth 2 -maxdepth 2 -type d | while read -r bench_dir; do
  name=$(basename "$bench_dir")
  json_file="$bench_dir/new/estimates.json"

  if [ -f "$json_file" ]; then
    mean_ns=$(jq '.mean.point_estimate' "$json_file")
    printf "%-30s %s ns\n" "$name" "$mean_ns"
  else
    echo "No estimates.json for $name"
  fi
done

