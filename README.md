
# HHDS: Hardware Hierarchical Dynamic Structure

> [!Note]
> Currently Iassert, tree-sitter, tree-sitter-cpp are hard-coded. They are required repos to be installed in order for certain tests and builds to complete.

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

-Nodes have several "pins" and the edges are bi-directional

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

### Key Features

- Efficient sibling traversal with next/previous pointers
- Optimized for append operations rather than random insertions
- Support for subtree references and sharing between trees via Forest container
- Three traversal modes: pre-order, post-order, and sibling-order
- Memory-efficient storage using chunks of 8 nodes
- Delta-compressed child pointers for common cases
- Tombstone deletion (IDs are not reused)
- Leaf-focused operations (delete_leaf is optimized)

### Data Structure Design

The tree uses a chunked storage approach where nodes are stored in fixed-size chunks of 8 entries. Each chunk contains:

- Long pointers (49-bit) for first/last child when needed
- Delta-compressed short pointers (18-bit) for child references within nearby chunks
- Next/Previous sibling pointers for efficient sibling traversal
- Parent pointers for upward traversal
- Leaf flag for quick leaf checks
- Support for subtree references

The structure is particularly optimized for:
1. Append operations (adding siblings or children at the end)
2. Leaf deletion
3. Sibling traversal
4. Child traversal for small families (< 8 children)

### Traversal Support

The tree provides three iterator types:
```cpp
// Pre-order traversal (parent then children)
for(auto node : tree.pre_order(start_pos)) { ... }

// Post-order traversal (children then parent) 
for(auto node : tree.post_order(start_pos)) { ... }

// Sibling-order traversal (iterate through siblings)
for(auto node : tree.sibling_order(start_pos)) { ... }
```

Each traversal mode also supports following subtree references via an optional flag:
```cpp
// Follow subtree references during traversal
for(auto node : tree.pre_order(start_pos, true)) { ... }
```

### Forest Support

Trees can share subtrees via the Forest container:
```cpp
Forest<int> forest;
Tree_pos subtree = forest.create_tree(root_data);
tree.add_subtree_ref(node_pos, subtree);
```

The Forest manages reference counting and cleanup of shared subtrees.

### Related

Not same, but similar idea and different representation:

Meyerovich, Leo A., Todd Mytkowicz, and Wolfram Schulte. "Data parallel programming for irregular tree computations." (2011).

Vollmer, Michael, Sarah Spall, Buddhika Chamith, Laith Sakka, Chaitanya Koparkar, Milind Kulkarni, Sam Tobin-Hochstadt, and Ryan R. Newton. "Compiling tree transforms to operate on packed representations." (2017): 26.
