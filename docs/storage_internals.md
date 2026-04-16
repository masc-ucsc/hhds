# HHDS Storage Internals

Internal developer reference for how the Graph and Tree data structures lay out
data in memory and how storage grows as elements are added.

---

## 1. ID Encoding

All IDs share a common encoding scheme defined in `graph_sizing.hpp`:

```
Nid / Pid / Vid  — uint64_t
```

The low 2 bits of every ID carry type tags:

| Bit 1 (driver) | Bit 0 (pin) | Meaning                  |
|:--------------:|:-----------:|--------------------------|
|       0        |      0      | Node / Pin(0) sink role  |
|       1        |      0      | Node / Pin(0) driver role|
|       0        |      1      | Pin (sink role)          |
|       1        |      1      | Pin (driver role)        |

- **Bit 0** distinguishes Node (0) vs Pin (1). A NodeEntry with port_id=0
  acts as both node and pin — its ID keeps bit 0 = 0.
- **Bit 1** distinguishes **local** edge (0, "I drive this target") vs
  **back** edge (1, "this target drives me"). Stored per-edge, not per-entry.
- The actual table index is always `id >> 2`.

This means `node.raw_nid == (table_index << 2)` and
`pin.pin_pid == (table_index << 2) | 1` for port_id > 0.
For port_id == 0, `pin.pin_pid == node.raw_nid` (bit 0 = 0).

### Local vs Back Edges

Every `add_edge(driver, sink)` stores **two** entries:

1. In driver's entry: `sink_vid` with bit 1 = 0 → **local** (forward) edge.
2. In sink's entry: `driver_vid` with bit 1 = 1 → **back** (reverse) edge.

Iteration uses bit 1 to filter:
- `out_edges()` / `out_sinks()`: keep edges where `!(vid & 2)` (local).
- `inp_edges()` / `inp_drivers()`: keep edges where `(vid & 2)` (back).

There is no per-pin direction vector. Direction is entirely per-edge.

---

## 2. Graph Storage

The graph uses per-node/per-pin adjacency storage. Each NodeEntry or PinEntry
has room for up to 6 inline adjacent endpoint references: 4 nearby delta-
compressed references and 2 full references. If that particular entry needs
more than 6 references, it moves its edge set to overflow storage. Edges are
stored bidirectionally by inserting one reference in the driver endpoint and
one reference in the sink endpoint. A direction bit on each stored reference
tells whether that reference is an outgoing/local edge or an incoming/back
edge, so incoming and outgoing traversals can share the same per-entry edge
storage.

### 2.1 Top-Level Vectors

A `Graph` object owns two flat vectors:

| Vector            | Element      | Size (bytes) | Indexed by      |
|-------------------|--------------|:------------:|-----------------|
| `node_table`      | `NodeEntry`  |      32      | `raw_nid >> 2`  |
| `pin_table`       | `PinEntry`   |      32      | `pin_pid >> 2`  |

On `clear_graph()`, `node_table` is pre-populated with 4 entries:

| Index | Role                |
|-------|---------------------|
|   0   | Invalid (sentinel)  |
|   1   | INPUT node          |
|   2   | OUTPUT node         |
|   3   | CONSTANT node       |

`pin_table` starts with 1 sentinel entry (index 0).

### 2.2 Node-as-Pin(0) Optimization

A `NodeEntry` doubles as the pin with port_id=0. This means:

- **No PinEntry is allocated** for the most common pin (port 0).
- `create_driver_pin(0)` / `create_sink_pin(0)` return the node itself
  wrapped as a `Pin_class` (pin_pid = node's raw_nid, bit 0 = 0).
- `PinEntry` objects exist **only for port_id > 0**.
- Driver vs sink for port 0 is distinguished purely by bit 1 of the
  stored edge Vids.

For an OR gate with one input and one output (both port 0), the single
32-byte `NodeEntry` holds all four edges (2 local + 2 back).

### 2.3 NodeEntry Layout (32 bytes, packed)

```
 Bits   Field         Width   Purpose
 ─────────────────────────────────────────────────────────────
  0-41  nid           42b     Self node-table index
 42-57  type          16b     User-defined type tag
 58-99  next_pin_id   42b     Head of pin linked list (Pid-encoded)
100-141 ledge0        42b     Long edge slot 0 / subnode Gid (when overflow)
142-183 ledge1        42b     Long edge slot 1
184     use_overflow   1b     0 = inline edges, 1 = hash-set mode
185-191 padding        7b
192-255 sedges union   64b     EITHER 4×14-bit packed short edges
                              OR     uint32_t overflow_idx into Graph::overflow_sets_
 ─────────────────────────────────────────────────────────────
 Total: 256 bits = 32 bytes
```

### 2.4 PinEntry Layout (32 bytes, packed)

```
 Bits   Field         Width   Purpose
 ─────────────────────────────────────────────────────────────
  0-41  master_nid    42b     Owning node's nid
 42-63  port_id       22b     Port identifier (always > 0)
 64-105 next_pin_id   42b     Next pin in node's linked list
106-147 ledge0        42b     Long edge slot 0
148-189 ledge1        42b     Long edge slot 1
190     use_overflow   1b     0 = inline edges, 1 = hash-set mode
        (implicit pad to byte)
192-255 sedges union   64b     EITHER 4×14-bit packed short edges
                              OR     uint32_t overflow_idx into Graph::overflow_sets_
 ─────────────────────────────────────────────────────────────
 Total: 256 bits = 32 bytes
```

One `PinEntry` per port_id serves **both** driver and sink roles for that
port. There is no separate driver and sink PinEntry for the same port_id.

### 2.5 Edge Storage: Three-Tier Strategy

Edges are stored **bidirectionally** — `add_edge(A,B)` inserts into both A's
and B's edge storage. Each NodeEntry/PinEntry stores its own edges through
three escalating tiers:

#### Tier 1: Short Edges (`sedges`, 4 slots × 14 bits = 56 bits used)

Each 14-bit slot encodes a **delta-compressed** reference to a nearby node/pin:

```
 Bit 13    sign       (1 = target is at a lower index)
 Bits 12-2 magnitude  (11 bits, max delta = 2047)
 Bit  1    driver bit (local=0, back=1)
 Bit  0    pin bit    (node=0, pin=1)
```

The target is reconstructed as: `target = (self >> 2) - delta`, then
re-tagged with driver/pin bits. This works only when `|self_index - target_index| <= 2047`.

**Cost: 0 extra bytes** (packed inside the entry).

#### Tier 2: Long Edges (`ledge0`, `ledge1`)

If the delta exceeds 11 bits, the raw Vid is stored in one of two 42-bit
long-edge fields. These are full absolute IDs (not delta-compressed).

**Cost: 0 extra bytes** (fields already exist in the entry).

#### Tier 3: Overflow Hash Set

When all 6 inline slots (4 short + 2 long) are full, the entry transitions
to overflow mode:

1. A new `ankerl::unordered_dense::set<Vid>` is appended to
   `Graph::overflow_sets_`.
2. All existing edges (from sedges + ledge0 + ledge1) are decoded and
   inserted into the set.
3. `use_overflow = 1`, `sedges_.overflow_idx` stores the index into
   `overflow_sets_`, and `ledge0`/`ledge1` are zeroed (except `ledge0`
   may hold a subnode Gid for NodeEntry).
4. All future edges go directly into the hash set.

**This transition is one-way** — once in overflow mode, the entry never
returns to inline mode. The `overflow_idx` field is a `uint32_t` stored
in the `sedges_` union; no pointers exist inside the entry, making the
`node_table` and `pin_table` vectors pointer-free and suitable for bulk
binary serialization.

**Cost:** ~56+ bytes per hash set (object overhead + load factor), growing
with edge count. The sets are owned by `Graph::overflow_sets_`, not by
the individual entries.

#### Subnode Storage (NodeEntry only)

When a node links to a sub-graph (`set_subnode`), the entry is **forced into
overflow mode** so that `ledge0` can be repurposed to store the `Gid` of the
child graph. This means any node with a subnode always pays the hash-set cost
for its edges.

### 2.6 Pin Linked List

Each node's pins (port_id > 0) form a singly-linked list:

```
NodeEntry.next_pin_id -> PinEntry[a].next_pin_id -> PinEntry[b].next_pin_id -> 0
```

All pin IDs in the list are Pid-encoded (`(index << 2) | 1`). Walking the
list to find a specific port_id is O(pins-per-node). Port_id=0 is never
in this list — it is the node itself.

### 2.7 Growth Pattern

| Action                | Storage effect                                       |
|-----------------------|------------------------------------------------------|
| `create_node()`       | Appends 1 `NodeEntry` (32B) to `node_table`         |
| `create_driver_pin(0)` / `create_sink_pin(0)` | **0 bytes** — returns node-as-pin |
| `create_driver_pin(p)` or `create_sink_pin(p)` (p>0) | First call: appends 1 `PinEntry` (32B). Subsequent calls with same port_id: **0 bytes** (reuses existing entry) |
| `add_edge(A,B)`       | Inserts into both A and B; may trigger overflow on either side |
| `delete_node()`       | Tombstones the entry (slot is never reused)           |
| `delete_pin()`        | Unlinks from list, frees overflow set if present      |

Per-node storage cost summary (single graph, no hierarchy overhead):

| Scenario                                | Bytes per node |
|-----------------------------------------|:--------------:|
| Isolated node, no pins, no edges        |       32       |
| Node with port-0 edges only (common)    |       32       |
| Node + 1 pin (port>0), ≤6 edges each   |       64       |
| Node + 1 pin (port>0), overflow on both |   ~64 + 2×56+  |
| Node with subnode link                  |   32 + ~56+    |

The `std::vector` backing store grows by the standard doubling strategy,
so actual memory consumption includes the typical vector slack.

### 2.8 GraphLibrary / GraphIO

```
GraphLibrary
  ├─ graph_ios_: vector<shared_ptr<GraphIO>>   // indexed by Gid
  ├─ graphs_:    vector<shared_ptr<Graph>>      // indexed by Gid; slot 0 = sentinel
  └─ graph_name_to_id_: HashMap<string, Gid>
```

- Gid 0 is reserved (invalid). First real graph gets Gid 1.
- `delete_graph()` tombstones the slot (sets it to nullptr); Gid is never reused.
- `GraphIO` holds declared pin metadata (names, port_ids). The concrete
  `PinEntry` objects are only created when `create_graph()` materializes the body.

---

## 3. Tree Storage

### 3.1 Top-Level Vectors (`Tree` class)

| Vector            | Element           | Size (bytes) | Indexed by           |
|-------------------|-------------------|:------------:|----------------------|
| `pointers_stack`  | `Tree_pointers`   |     192      | `node_pos >> 3` (chunk index) |
| `validity_stack`  | `std::bitset<64>` |       8      | `node_pos >> 6`      |
| `subnode_refs`    | `Tree_pos`        |       8      | `node_pos`           |

> **Note:** The legacy templated `tree<X>` class also carries a
> `data_stack` (`std::vector<X>`) for per-node payload. This will be
> removed once the attribute system (see `todo_attr.md`) is in place.
> New code should use `Tree` + attributes, not `tree<X>`.

### 3.2 Tree_pointers Layout (192 bytes, 64-byte aligned)

Each `Tree_pointers` struct represents a **chunk of 8 sibling slots**:

```
 Field              Type                         Bytes  Purpose
 ────────────────────────────────────────────────────────────────
 parent             Tree_pos (int64_t)             8    Parent node_pos (shared by all 8 slots)
 next_sibling       Tree_pos                       8    Next sibling chunk (linked list of chunks)
 prev_sibling       Tree_pos                       8    Previous sibling chunk
 first_child_ptrs   array<Tree_pos, 8>            64    Per-slot first-child pointer
 last_child_ptrs    array<Tree_pos, 8>            64    Per-slot last-child pointer
 types              array<Type(uint16_t), 8>      16    Per-slot type tag
 num_short_del_occ  uint16_t                       2    Highest occupied offset in this chunk (0-7)
 is_leaf            bool                           1    True if no slot in this chunk has children
 (alignment pad)                                  21    Pad to 192 bytes (3 × 64-byte cache lines)
 ────────────────────────────────────────────────────────────────
 Total: 192 bytes
```

`static_assert(sizeof(Tree_pointers) == 192)` is enforced in `tree.cpp`.

### 3.3 Node Addressing

A `Tree_pos` (int64_t) encodes both chunk and offset:

```
 chunk_index  = node_pos >> 3     (CHUNK_SHIFT = 3)
 chunk_offset = node_pos & 0x7    (CHUNK_MASK = 7)
```

Special values:
- `INVALID = 0` — null pointer
- `ROOT = 8` (= `1 << 3`) — the root always lives at chunk index 1, offset 0

Chunk index 0 is allocated as a dummy during `add_root()` (the first
`emplace_back`), making slot 0 through 7 unusable. The root occupies
chunk 1 offset 0 = `Tree_pos(8)`.

### 3.4 Chunk-Based Sibling Storage

Siblings are packed consecutively within a chunk (offsets 0..7). When a
chunk fills (offset reaches 7), a **new chunk is linked** via the
`next_sibling`/`prev_sibling` fields, forming a doubly-linked list of
sibling chunks.

Within a single chunk, `num_short_del_occ` tracks the highest occupied
offset, so iterating siblings within a chunk is a simple offset increment
without checking validity bits.

### 3.5 Validity Tracking

A `std::bitset<64>` per 64 tree positions tracks which slots contain live
data. This is separate from the chunk structure — it covers all positions
globally. Used for:
- Checking if a position is occupied: `validity_stack[pos >> 6][pos & 63]`
- Tombstone deletion: clear the bit, compact remaining entries within the chunk

### 3.6 Growth Pattern

| Action             | Storage effect                                         |
|--------------------|--------------------------------------------------------|
| `add_root()`       | 2 chunks allocated (dummy + root) = 384B in `pointers_stack` |
| `add_child(P)`     | If P has no children: 1 new chunk (192B). Sets `first_child_ptrs[offset]` in parent |
| `append_sibling(S)`| If current chunk has room (offset < 7): **0 bytes** (just fill next slot). Otherwise: 1 new chunk (192B) |
| `delete_leaf()`    | Tombstone (bit cleared); remaining entries shift left within the chunk. Chunk is never freed |

Per-node amortized storage (Tree, no payload):

| Scenario                       | Bytes per node        |
|--------------------------------|-----------------------|
| Best case (8 siblings/chunk)   | 192/8 = **24 B/node** |
| Worst case (1 node per chunk)  | **192 B/node**        |
| Typical (partial chunks)       | ~32–48 B/node         |

### 3.7 Forest / TreeIO

```
Forest
  ├─ tree_ios_:         vector<shared_ptr<TreeIO>>   // indexed by (-tid - 1)
  ├─ trees:             vector<shared_ptr<Tree>>     // indexed by (-tid - 1)
  ├─ reference_counts:  vector<size_t>               // per-tree refcount
  └─ tree_name_to_tid_: HashMap<string, Tid>
```

- Tree IDs (`Tid`) are **negative** integers: `-1, -2, -3, ...`
  The vector index is `(-tid - 1)`.
- `delete_tree()` checks the reference count and tombstones the slot.
- Subnode references in `Tree_pointers` use the negative `parent` field
  (`parent < 0` means this chunk is a subnode link, not a parent pointer).

---

## 4. Comparative Summary

| Property             | Graph NodeEntry/PinEntry | Tree Tree_pointers      |
|----------------------|:------------------------:|:-----------------------:|
| Entry size           | 32 B                     | 192 B (covers 8 nodes)  |
| Per-node amortized   | 32 B (port-0 only)       | 24–192 B                |
| ID width             | 42-bit (Nid_bits)        | 64-bit (Tree_pos)       |
| Edge storage         | Inline (6) then hash set | N/A (parent/child/sibling pointers) |
| Deletion             | Tombstone                | Tombstone + intra-chunk compaction |
| Hierarchy            | Gid in ledge0 (overflow) | Negative parent = subnode Tid |
| Backing store        | `std::vector` (flat)     | `std::vector` (flat)    |

---

## 5. On-Disk Persistence

### 5.1 Design Principles

- **Binary body format**: `node_table`, `pin_table`, and `pointers_stack` are
  pointer-free POD arrays that are bulk-written/read in a single I/O operation.
  This makes save/load bandwidth-limited (~3 GB/s on NVMe).
- **Overflow sets serialized separately**: Each overflow hash set is written to
  its own file using the `values()` / `replace()` API from
  `ankerl::unordered_dense::set`. Only the contiguous values vector is
  serialized; the hash bucket table is rebuilt on load.
- **Text for declarations**: `GraphIO` and `TreeIO` metadata (names, port
  declarations) is small and human-readable. It uses a simple text format to
  allow manual inspection and cross-platform portability.
- **No cross-endian support**: Binary files are same-endian only. An endian
  marker in the header detects mismatches. For cross-platform exchange, use
  the text-based `write_dump` / `read_dump` on trees.

### 5.2 Directory Layout

```
<db_root>/
  library.txt                    # GraphLibrary declarations (text)
  forest.txt                     # Forest declarations (text)
  graph_<gid>/
    body.bin                     # node_table + pin_table (binary bulk)
    overflow_0.bin               # overflow set 0: count + Vid[]
    overflow_1.bin               # overflow set 1: count + Vid[]
    ...
  tree_<tid>/
    body.bin                     # pointers_stack + validity_stack + subnode_refs (binary bulk)
```

### 5.3 Graph Body Format (`body.bin`)

```
 Offset  Size     Field
 ──────────────────────────────────────────
 0       4B       magic: 0x48484742 ("HHGB")
 4       4B       version: 1
 8       4B       endian_check: 0x01020304
 12      8B       node_count (uint64_t)
 20      8B       pin_count (uint64_t)
 28      8B       overflow_count (uint64_t)
 36      N*32B    node_table (NodeEntry[node_count])
 36+N*32 M*32B    pin_table (PinEntry[pin_count])
```

NodeEntry and PinEntry are written as-is from memory. The `sedges_` union
contains either packed short edges (when `use_overflow == 0`) or an
`overflow_idx` (when `use_overflow == 1`). No pointers are stored on disk.

### 5.4 Overflow Set Format (`overflow_<idx>.bin`)

```
 Offset  Size     Field
 ──────────────────────────────────────────
 0       8B       count (uint64_t)
 8       K*8B     values (Vid[count])
```

Serialized via `set.values().data()`. Deserialized by reading into a
`std::vector<Vid>` and calling `set.replace(std::move(vec))`, which
rebuilds the hash bucket table from the values.

### 5.5 Tree Body Format (`body.bin`)

```
 Offset  Size     Field
 ──────────────────────────────────────────
 0       4B       magic: 0x48485442 ("HHTB")
 4       4B       version: 1
 8       4B       endian_check: 0x01020304
 12      8B       pointers_count (uint64_t)
 20      8B       validity_count (uint64_t)
 28      8B       subnode_count (uint64_t)
 36      P*192B   pointers_stack (Tree_pointers[pointers_count])
 ...     V*8B     validity_stack (bitset<64>[validity_count])
 ...     S*8B     subnode_refs (Tree_pos[subnode_count])
```

### 5.6 Lazy Body Loading

Body files are not loaded until `get_graph()` or `get_tree()` is called.
Declarations (names, IO pin metadata) load eagerly from the text files
so that `find_io()` works without loading bodies.

When the last `shared_ptr<Graph>` or `shared_ptr<Tree>` is released, the
body can be unloaded (the data is dropped and will be re-read from disk on
next access).

### 5.7 Future: Attribute Sections

Once the attribute system is in place, each attribute tag will be stored as
an additional file in the graph/tree directory:

```
  graph_<gid>/
    attr_<tag_hash>.bin          # flat: (Nid, T)[] pairs
                                 # hier: (Tree_pos, Nid, T)[] pairs
```

Trivially copyable `T` values are bulk-written. Non-trivial types (e.g.,
`std::string`) use length-prefixed encoding. Empty attribute maps are not
persisted.
