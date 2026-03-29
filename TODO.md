# HHDS TODO

This roadmap follows the target public model in
[`api.md`](/Users/renau/projs/hhds/api.md). When there is a conflict between old
code and this roadmap, follow `api.md`.

## Goals

- Make HHDS a clean standalone C++ library first.
- Prefer a simple, consistent API over compatibility with old code.
- Keep `Tree` generic and structural.
- Keep `Graph` structural, but make module/interface declaration explicit.
- Make `Forest` and `GraphLibrary` look similar where it is natural.
- Keep user metadata external, with future registration at `Forest` / `GraphLibrary`.
- Use assertions for invalid usage; do not use exceptions.

## Stable Direction

### Identity and naming

- Names are required.
- Names are immutable.
- Internal IDs stay stable and use tombstone semantics.
- Internal IDs are not the primary public identity.

### Declaration and body split

- Add first-class `TreeIO` and `GraphIO` objects.
- `TreeIO` / `GraphIO` are the primary named declarations.
- `Tree` / `Graph` are optional implementation bodies attached to a declaration.
- A declaration and its body share the same internal ID.
- At most one body exists for a given declaration at a time.
- See `api.md`:
  - `Main object model`
  - `Public creation flow`
  - `Deletion semantics`

### Creation model

- `Tree` bodies are created only through `TreeIO`.
- `Graph` bodies are created only through `GraphIO`.
- `Tree` and `Graph` remain public concrete types.
- `std::shared_ptr` is the intended ownership model for declarations and bodies.
- See `api.md`:
  - `Public creation flow`
  - `Ownership and lifetime`
  - `APIs to remove or restrict`

### Tree side

- Keep the tree generic.
- `TreeIO` is required for symmetry and naming, even if it starts with only immutable name data.
- Creating a `Tree` from `TreeIO` yields an empty tree body.
- Tree does not get a built-in input/output model.

### Graph side

- `GraphIO` owns ordered graph input/output declarations.
- Port order follows the existing `Port_ID` model.
- Creating a `Graph` from `GraphIO` should auto-materialize the graph IOs in the body.
- Direct graph IO mutation on `Graph` should be removed.
- IO mutation should happen through `GraphIO` only.
- If `GraphIO` changes while a body exists, the `Graph` body should be updated to match.
- See `api.md`:
  - `Graph declarations and bodies`
  - `Graph API direction`
  - `APIs to remove or restrict`

### Deletion semantics

- Deleting a `Tree` or `Graph` body clears only the implementation.
- Deleting a `TreeIO` or `GraphIO` deletes both declaration and implementation.
- Deleted declarations keep their internal tombstone mapping.
- Normal `find_*` calls ignore tombstones and return `nullptr`.
- Graph declaration IO survives body deletion.
- See `api.md`:
  - `Lookup and find semantics`
  - `Deletion semantics`

### Metadata

- HHDS stays structural.
- User metadata should live in external maps keyed by wrapper objects.
- Future map registration belongs at `Forest` / `GraphLibrary`, not at `Tree` / `Graph`.
- Valid future map keys include `Node_class`, `Node_flat`, `Node_hier`, `Pin_class`, `Pin_flat`, and `Pin_hier`.
- Deleted objects should not be saved or printed.
- Automatic metadata cleanup on delete is desirable, but does not block the first cleanup phase.
- See `api.md`:
  - `Metadata registration direction`
  - `Tree wrappers`
  - `Graph wrappers`

### Handles and validity

- Wrapper objects remain plain value-key objects.
- Graph/tree bodies are the main operation objects, passed around through `std::shared_ptr`.
- Add `is_valid()` / `is_invalid()` style checks for node/pin references where stale handles are possible.
- Operations may assert in debug mode if called on invalid references.
- See `api.md`:
  - `Graph wrappers`
  - `Validity checks`

### Persistence

- Binary is the normal persistence format.
- Text read/write is useful for debugging and manual intervention.
- Text format does not need long-term stability.
- Save declarations and bodies separately.
- Support lazy loading of `Tree` / `Graph` bodies.
- When no body `shared_ptr` remains, unloading and later reloading should be automatic.
- See `api.md`:
  - `Persistence direction`
  - `Ownership and lifetime`

### Documentation and cleanup

- `README.md` should describe the clean standalone direction.
- Old planning markdowns should be removed.
- Rust support and `lhtree` comparison code should be removed from the repo.
- Default build/test dependencies should be simplified.

## High-Level Phases

### Phase 1: Documentation and cleanup

- Rewrite `README.md`.
- Consolidate planning into this file.
- Add a concrete API target document in `api.md`.
- Remove stale docs and obsolete TODO files.
- Remove Rust and `lhtree` comparison code.
- Simplify build and dependency surface.

Detailed scope:

- make `README.md`, `TODO.md`, and `api.md` consistent
- stop documenting payload-bearing tree APIs
- stop documenting direct body creation as the long-term API
- remove stale notes that point to old migration plans

Primary API references:

- `api.md` `Principles`
- `api.md` `Public creation flow`
- `api.md` `APIs to remove or restrict`

### Phase 2: Declaration-centric API

- Add `TreeIO` and `GraphIO`.
- Move public creation to declaration-first flows.
- Require names at declaration creation.
- Remove direct public body creation APIs.
- Remove explicit-ID public creation paths.

Detailed scope:

- add `Forest::create_treeio(name)` and `GraphLibrary::create_graphio(name)`
- add `find_treeio(name)` / `find_graphio(name)`
- add `TreeIO::create_tree()` and `GraphIO::create_graph()`
- add `TreeIO::get_tree()` and `GraphIO::get_graph()`
- make body creation impossible without an existing declaration
- remove or hide explicit-ID public creation entry points

Primary API references:

- `api.md` `Public creation flow`
- `api.md` `Lookup and find semantics`
- `api.md` `APIs to remove or restrict`

### Phase 3: Graph IO ownership refactor

- Move graph IO declaration into `GraphIO`.
- Remove direct graph IO mutation from `Graph`.
- Auto-create graph IO structures in a newly created `Graph` body from `GraphIO`.
- Keep `GraphIO` and `Graph` synchronized when IOs change.

Detailed scope:

- add `GraphIO::add_input`, `add_output`, `delete_input`, and `delete_output`
- move graph port ordering ownership to `GraphIO`
- define how `Graph` auto-materializes graph IO nodes/pins from `GraphIO`
- make body reload and body recreation preserve the declaration-side interface
- remove old graph-side public APIs that mutate declaration IO directly

Primary API references:

- `api.md` `Graph declarations and bodies`
- `api.md` `Graph API direction`
- `api.md` `Deletion semantics`

### Phase 4: Lifetime and deletion semantics

- Make body deletion clear only implementation state.
- Make declaration deletion remove declaration plus body while preserving tombstones.
- Make lookups skip tombstones.
- Add validity checks for stale node/pin references.

Detailed scope:

- define `delete_tree(tree)` / `delete_graph(graph)` as body-only clear
- define `delete_treeio(tio)` / `delete_graphio(gio)` as declaration plus body delete
- keep name/ID tombstones internal
- make `find_*` return `nullptr` for deleted declarations
- add `is_valid()` / `is_invalid()` checks where stale node/pin references can exist

Primary API references:

- `api.md` `Lookup and find semantics`
- `api.md` `Deletion semantics`
- `api.md` `Validity checks`

### Phase 5: Metadata registration

- Add map registration on `Forest` / `GraphLibrary`.
- Support wrapper-keyed metadata consistently.
- Make delete/save/print interact correctly with registered metadata.

Detailed scope:

- allow registration of external metadata maps at `Forest` / `GraphLibrary`
- support `_class`, `_flat`, and `_hier` wrapper-keyed maps
- define cleanup behavior for deleted nodes/pins/trees/graphs
- ensure deleted metadata is ignored by save/print even before eager cleanup exists

Primary API references:

- `api.md` `Metadata registration direction`
- `api.md` `Tree wrappers`
- `api.md` `Graph wrappers`

### Phase 6: Persistence and lazy bodies

- Save/load declarations separately from bodies.
- Add lazy body loading and automatic body unloading.
- Keep ID and name mapping stable across round-trips.
- Add debug-oriented text import/export.

Detailed scope:

- save `TreeIO` / `GraphIO` declarations separately from `Tree` / `Graph` bodies
- keep stable name-to-ID mapping data for reload
- allow declaration-first load without loading every body
- automatically unload bodies when no `shared_ptr` remains
- make `find_tree(name)` / `find_graph(name)` able to reload a body on demand
- keep text I/O as a debug format, not as a compatibility contract

Primary API references:

- `api.md` `Persistence direction`
- `api.md` `Ownership and lifetime`
- `api.md` `Lookup and find semantics`

### Phase 7: Debugging and printing

- Keep tree printing useful early.
- Add simpler graph printing later.
- Ensure printed output ignores deleted data.

Detailed scope:

- preserve practical tree debug printing while tree APIs move to declaration-first ownership
- define print entry points in a way that fits declaration/body split
- make graph printing useful but deliberately simpler than persistence
- ensure deleted nodes/pins/declarations do not appear in normal printed output

Primary API references:

- `api.md` `Persistence direction`
- `api.md` `Deletion semantics`
- `api.md` `Metadata registration direction`

## Immediate Tasks

- Update docs before implementation work.
- Delete stale tracked planning docs.
- Remove compatibility-oriented dead code instead of preserving it.
- Start implementation with `TreeIO` / `GraphIO` because they drive many later decisions.
- Use `api.md` as the target public surface while implementation catches up.
