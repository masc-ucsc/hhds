Final benchmark suite
=====================

This folder is intentionally separate from `hhds/bench`.

Layout:

- `hhds/`: HHDS benchmark binary and raw `results.csv`.
- `boost/`: Boost.Graph benchmark binary and raw `results.csv`.
- `livehd/`: reserved LiveHD slot. It is `N/A` until LiveHD source is available.
- `comparisons/comparison.csv`: wide table with `operation`, `hhds`, `livehd`,
  and `boost` columns. Times are median wall time in milliseconds.
- `comparisons/plot/`: one PNG per operation/scenario.
- `scripts/run_all.sh`: builds, runs, aggregates, and plots.

Storage-tier edge scenarios:

- `sedges_inline`: ring-forward-2, about 4 refs per node, packed `sedges`.
- `ledge_inline`: ring-forward-4, about 8 refs per node, inline ledge path.
- `overflow`: ring-forward-16, about 32 refs per node, overflow hash set.

For `traverse_forward_class` and `traverse_backward_class`, the benchmark uses
a DAG version of the same forward-K shape so Boost topological traversal remains
valid. The edge creation/deletion and `traverse_fast_class` scenarios use the
ring shape to make every node land in the intended storage tier.

