# TODO — hhds follow-ups

Design decisions from recent sessions are captured in `hhds/CLAUDE.md` and
`~/.claude/projects/-Users-renau-projs-hhds/memory/`. Treat this file as the
implementation queue.

## 1. Debug-only cycle check in `set_subnode`

**What:** In debug builds, when `set_subnode(node, gid)` is called,
verify no cycle would be created in the structure tree forest.

**Why:** Cycles are explicitly disallowed (user decision). Without a
check, a runtime cycle causes infinite loops in hier traversal. Debug-
only keeps the production path O(1).

**How:** Breadth-first from `gid` through structure-tree subnode refs,
asserting we never reach `self_gid_`. Visit set keeps the check linear
in subnode count, which is bounded by hierarchy size (≪ graph size).

---

## 2. Optional: eliminate `context_` field entirely

**What:** Drop `Context context_` and discriminate by field nullity:
- `root_gid_ == Gid_invalid` → Class
- `owner_gid_ == Gid_invalid` → Flat
- else → Hier

**Saves:** 8 more bytes (48 → 40 bytes per Node/Pin).

**Risk:** Users calling `is_flat()`, `is_hier()`, `get_context()` get a
*derived* answer rather than a stored one. If any code path builds a
Node with valid `root_gid_` but unset `owner_gid_` and expects
`is_hier()` true, it breaks. Requires auditing every constructor and
assignment.

**Recommendation:** defer unless memory becomes a concern.

---

## 3. Optional: `root_graph_` as `Graph*` instead of `Gid`

**What:** Store the root as a pointer rather than an id. Parallels how
`graph_` works.

**Pro:** Callers wanting the full root graph (e.g. for cross-graph
queries) skip a `GraphLibrary::get_graph(root_gid_)` lookup.

**Con:** `get_root_gid()` gains one pointer indirection.

**Recommendation:** defer unless a hot path shows up that repeatedly
resolves root gid back to a `Graph*`.
