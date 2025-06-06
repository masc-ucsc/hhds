#!/usr/bin/bash

nodes=(10 100 1000 10000 100000 1000000)

trees=("deep" "wide" "representative")

for tree in "${trees[@]}"; do
	echo "Traversal Benchmark - $tree"
	for i in "${nodes[@]}"; do
		echo -n $i
		NODES=$i cargo bench --bench "${tree}_traversal_bench" 2>/dev/null | grep 'time:' | sed -E 's/time:[[:space:]]+\[[0-9.]+[[:space:]]+[a-z]+[[:space:]]+([0-9.]+[[:space:]]+[a-z]+)[[:space:]]+[0-9.]+[[:space:]]+[a-z]+\]/time:   \1/'
	done
done
