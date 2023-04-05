
# HHDS: Hardware Hierarchical Dynamic Structure


This repository contains a highly optimized graph and tree data structure. The
structure is optimized for handling graphs/trees structures typically found in
hardware tools. 


## Graph

Some of the key characteristics:

-Graph mutates (add/remove edges and nodes)

-At most 32 bit ID for node (graph would be partitioned if larger is needed)

-Once an ID is created, even if node is deleted, it can not be reused. This
sometimes called a "tombstone deletion"

-Delete node is frequent (code optimization)

-2 main types of traversal (any order or topological sort)

-Add edges is not so frequent after the 1st phase of graph creation

-Nodes has several "pins" and the edges are bi-directional

-In practice (topological sort), most edges are "short" no need to keep large
32 bit integer. Delta optimization to fit more edges.

-Meta information (attributes) are kept separate with the exception of type

-Most nodes have under 8 edges, but some (reset/clk) have LOTS of edges

-The graph operations do NOT need to be multithreaded with updates. The
parallelism is extracted from parallel thread operations. (parallel read-only
may happen).

### Related works

Some related (but different) graph representations:

Pandey, Prashant, et al. "Terrace: A hierarchical graph container for skewed
dynamic graphs." Proceedings of the 2021 International Conference on
Management of Data. 2021.

Winter, Martin, et al. "faimGraph: high performance management of
fully-dynamic graphs under tight memory constraints on the GPU." SC18:
International Conference for High Performance Computing, Networking, Storage
and Analysis. IEEE, 2018.

Bader, David A., et al. "Stinger: Spatio-temporal interaction networks and
graphs (sting) extensible representation." Georgia Institute of Technology,
Tech. Rep (2009).

Feng, Guanyu, et al. "Risgraph: A real-time streaming system for evolving
graphs to support sub-millisecond per-update analysis at millions ops/s."
Proceedings of the 2021 International Conference on Management of Data. 2021.

### Overall data structure:

Vector with 4 types of nodes: Free, Node, Pin, Overflow

Node/Pin are 16 byte, Overflow 32 byte

Node is the master node and also the output pin

Pin is either of the other node pins (input 0, output 1....)

Node/Pin have a very small amount of edges. If more are needed, the overflow
is used. If more are needed an index to emhash8::HashSet is used (overflow is
deleted and moved to the set)

The overflow are just a continuous (not sorted) array. Scanning is needed if
overflow is used.


## Tree

The tree data structure is optimized for typical AST traversal and operations.
Once a tree is created, the common operation is to delete nodes, and at most
insert phi nodes (SSA). This means that the insertion is for a few entries, not
large subtrees.


The traversal is a topological sort equivalent where parents of leafs are
visited first, and then to process them the leafs are accessed. In a way, the
following is a typical AST traversal:


```
Index: first Child, parent

01: 00,03 │   +── 1.1.1
02: 00,03 │   |── 1.1.2
03: 01,32 -── 1.1
04: 00,06 │           +── 1.2.1.1.1
05: 00,06 │           |── 1.2.1.1.2
06: 04,13 │       +── 1.2.1.1
07: 00,08 │           +── 1.2.1.2.1
08: 07,13 │       |── 1.2.1.2
09: 00,12 │           +── 1.2.1.3.1
10: 00,12 │           |── 1.2.1.3.2
11: 00,12 │           |── 1.2.1.3.3
12: 09,13 │       |── 1.2.1.3
13: 04,27 │   +── 1.2.1
14: 00,16 │       +── 1.2.2.1
15: 00,16 │       |── 1.2.2.2
16: 14,27 │   ├── 1.2.2
17: 00,19 │           +── 1.2.3.1.1
18: 00,19 │           |── 1.2.3.1.2
19: 17,23 │       +── 1.2.3.1
20: 00,22 │           +── 1.2.3.2.1
21: 00,22 │           |── 1.2.3.2.2
22: 20,23 │       +── 1.2.3.2
23: 19,27 │   |── 1.2.3
24: 00,26 │       +── 1.2.4.1
25: 00,26 │       |── 1.2.4.2
26: 24,27 │   |── 1.2.4
27: 13,32 ├── 1.2
28: 00,30 │   |── 1.3.1
29: 00,30 │   |── 1.3.2
30: 00,32 ├── 1.3
31: 00,32 ├── 1.4
32: 00,00 | 1
```

### Related

Not same, but similar idea and different representation:

Meyerovich, Leo A., Todd Mytkowicz, and Wolfram Schulte. "Data parallel programming for irregular tree computations." (2011).


