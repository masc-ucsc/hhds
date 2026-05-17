LiveHD benchmark slot
=====================

`run_all.sh` can include LiveHD when a sibling LiveHD checkout exists and
provides this binary:

```text
//lgraph:livehd_final_bench
```

Default expected path:

```text
/Users/hamidsadjadpour/Desktop/masc_ucsc/livehd
```

Override it with:

```bash
LIVEHD_ROOT=/path/to/livehd bash hhds/benchmarks_final/scripts/run_all.sh
```

The LiveHD binary must emit the same raw CSV schema as the HHDS and Boost
binaries. Backward traversal rows remain `N/A` for LiveHD because LiveHD's
`backward()` iterator is currently a stub with a FIXME/assert.
