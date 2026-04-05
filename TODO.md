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
- `Forest` and `GraphLibrary` constructors take a directory path for persistence.
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
- Ports have an optional `loop_last` property, set per-port at `add_input` / `add_output` time.
- `loop_last` marks ports as loop-breaking points for the forward topological iterator (e.g. register outputs).
- Creating a `Graph` from `GraphIO` should auto-materialize the graph IOs in the body.
- Direct graph IO mutation on `Graph` should be removed.
- IO mutation should happen through `GraphIO` only.
- If `GraphIO` changes while a body exists, the `Graph` body should be updated to match.
- See `api.md`:
  - `Graph declarations and bodies`
  - `Graph API direction`
  - `APIs to remove or restrict`

### Pin direction and edge creation

- Pins have explicit direction: `create_driver_pin` / `create_sink_pin` replace `create_pin`.
- Overloads: `(node)` for default port 0, `(node, port_id)`, `(node, "name")` for sub-node port resolution via `GraphIO`.
- Edges are created via `connect_driver` / `connect_sink` on `Node_class` and `Pin_class`, not `add_edge`.
- `connect_driver` on a sink means "attach a driver to me."
- `connect_sink` on a driver means "attach a sink to me."
- When called on a `Node_class`, the default pin 0 is used.
- `add_edge` and `create_pin` are removed entirely.
- Graph IO pins are accessed via `Graph::get_input_pin("name")` / `get_output_pin("name")`.
- `set_subnode` accepts `TreeIO` / `GraphIO` shared_ptr directly (no raw ID extraction needed).
- `set_subnode` stores the `GraphIO` ID in the node's 16-bit type field when the ID fits (< 2^16).
  This keeps the compact node representation for common EDA cells.
  IDs >= 2^16 fall back to the overflow edge representation.
  After `set_subnode`, `get_type(node)` returns the `GraphIO` ID.
- See `api.md`:
  - `Pin direction split`
  - `Connect API`
  - `Graph IO pin access`

### Built-in node and pin names

- Node and pin instance names are built-in via `set_name` / `get_name` / `has_name`.
- Internally backed by `absl::flat_hash_map`, auto-registered via `add_map` at `Graph`/`Tree` construction.
- `Pin_class::get_pin_name` returns the port name from the node's `GraphIO` declaration.
- See `api.md`:
  - `Built-in node and pin names`

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

Suggested steps:

1. Rewrite `README.md` to describe the declaration-centric direction instead of the old payload-era API.
2. Keep a single planning entry point in `TODO.md`.
3. Add and maintain `api.md` as the target public contract.
4. Remove stale planning docs that point to superseded migration strategies.
5. Remove old examples that show payload-bearing trees or direct body creation.
6. Remove or simplify dependency notes that are no longer part of the intended default build.

Exit criteria:

- top-level docs are internally consistent
- old API examples are gone from the main docs
- there is one roadmap doc and one API target doc
- stale tracked planning files are removed

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

Suggested steps:

1. Add declaration object types:
   - `TreeIO`
   - `GraphIO`
2. Add declaration ownership inside:
   - `Forest`
   - `GraphLibrary`
3. Move required immutable name ownership from body-centric APIs into declaration objects.
4. Add public creation entry points:
   - `Forest::create_treeio(name)`
   - `GraphLibrary::create_graphio(name)`
5. Add public lookup entry points:
   - `find_treeio(name)`
   - `find_graphio(name)`
   - `find_tree(name)`
   - `find_graph(name)`
6. Add body accessors from declarations:
   - `TreeIO::get_tree()`
   - `GraphIO::get_graph()`
7. Add body creation from declarations:
   - `TreeIO::create_tree()`
   - `GraphIO::create_graph()`
8. Remove or deprecate direct public body creation paths from:
   - `Tree`
   - `Graph`
   - `Forest`
   - `GraphLibrary`
9. Remove public explicit-ID creation APIs and migrate tests to name-based declaration creation.
10. Ensure declaration and body share the same internal ID.

Implementation notes:

- keep the old internals only as temporary bridges if they reduce churn
- avoid adding compatibility layers that survive beyond this phase
- update tests as soon as the new declaration creation path exists

Exit criteria:

- bodies cannot be created publicly without a declaration
- names are required at declaration creation
- name-based lookup is the normal public path
- explicit-ID public creation is gone

### Phase 3: Graph IO ownership and pin/connect refactor

- Move graph IO declaration into `GraphIO`. [DONE 2026-04-04]
- Remove direct graph IO mutation from `Graph`.
- Auto-create graph IO structures in a newly created `Graph` body from `GraphIO`. [DONE 2026-04-04]
- Keep `GraphIO` and `Graph` synchronized when IOs change. [DONE 2026-04-04]
- Replace `create_pin` with direction-aware `create_driver_pin` / `create_sink_pin`.
- Replace `add_edge` with `connect_driver` / `connect_sink` on nodes and pins.
- Add `get_input_pin` / `get_output_pin` on `Graph` for accessing auto-materialized IO pins. [DONE 2026-04-04]
- Add built-in node and pin instance names (`set_name` / `get_name` / `has_name`).
- Add `get_pin_name` for resolving port names from the `GraphIO` declaration.
- Accept `TreeIO` / `GraphIO` objects directly in `set_subnode` (no raw ID extraction).

Detailed scope:

- add `GraphIO::add_input`, `add_output`, `delete_input`, and `delete_output` [DONE 2026-04-04]
- move graph port ordering ownership to `GraphIO` [DONE 2026-04-04]
- define how `Graph` auto-materializes graph IO nodes/pins from `GraphIO` [DONE 2026-04-04]
- make body reload and body recreation preserve the declaration-side interface [DONE 2026-04-04]
- remove old graph-side public APIs that mutate declaration IO directly
- replace `create_pin` with `create_driver_pin` and `create_sink_pin`
- add overloads: `(node)` for default pin 0, `(node, port_id)`, `(node, "name")` for sub-node port resolution
- replace `add_edge` with `connect_driver` / `connect_sink` on `Node_class` and `Pin_class`
- add `Graph::get_input_pin("name")` and `Graph::get_output_pin("name")` [DONE 2026-04-04]
- add `Node_class::set_name` / `get_name` / `has_name` (backed by internal `absl::flat_hash_map`, auto-registered via `add_map`)
- add `Pin_class::set_name` / `get_name` / `has_name`
- add `Pin_class::get_pin_name` to return the port name from the node's `GraphIO` declaration
- make `set_subnode` accept `TreeIO` / `GraphIO` shared_ptr directly (library extracts IDs internally)

Primary API references:

- `api.md` `Graph declarations and bodies`
- `api.md` `Graph API direction`
- `api.md` `Pin direction split`
- `api.md` `Connect API`
- `api.md` `Graph IO pin access`
- `api.md` `Built-in node and pin names`
- `api.md` `Deletion semantics`

Suggested steps:

1. Define the internal storage model for `GraphIO` ports: [DONE 2026-04-04]
   - port name
   - direction
   - stable port identifier / order
   - `loop_last` flag (per-port, default false)
   - any minimal built-in declaration metadata that must stay in-core
2. Add `GraphIO` public mutation APIs: [DONE 2026-04-04]
   - `add_input(name, port_id, loop_last = false)`
   - `add_output(name, port_id, loop_last = false)`
   - `delete_input`
   - `delete_output`
3. Add `GraphIO` public query APIs: [DONE 2026-04-04]
   - `has_input`
   - `has_output`
   - `is_loop_last`
   - port-id lookup
   - ordered iteration helpers if needed
4. Define how a newly created `Graph` body auto-materializes declaration IOs. [DONE 2026-04-04]
5. Add `Graph::get_input_pin("name")` and `Graph::get_output_pin("name")`. [DONE 2026-04-04]
6. Remove direct graph-side IO mutation from `Graph`.
7. Redirect any remaining graph IO creation paths through `GraphIO`.
8. Define body synchronization when `GraphIO` changes after a body already exists. [DONE 2026-04-04]
9. Replace `create_pin` with `create_driver_pin` / `create_sink_pin`:
   - overloads: `(node)`, `(node, port_id)`, `(node, "name")`
   - string overload resolves port name through the sub-node's `GraphIO`
   - remove `create_pin` entirely
10. Replace `add_edge` with `connect_driver` / `connect_sink`:
    - on `Node_class`: uses default pin 0
    - on `Pin_class`: uses the specific pin
    - `connect_driver` on a sink means "attach a driver to me"
    - `connect_sink` on a driver means "attach a sink to me"
    - remove `add_edge` entirely
11. Add built-in node and pin names:
    - `set_name` / `get_name` / `has_name` on `Node_class` and `Pin_class`
    - backed by internal `absl::flat_hash_map`, auto-registered via `add_map` at construction
12. Add `Pin_class::get_pin_name` to return the port name from the node's `GraphIO` declaration.
13. Make `set_subnode` accept `TreeIO` / `GraphIO` shared_ptr directly.
14. Store `GraphIO` ID in the node's 16-bit type field when ID < 2^16; overflow to edge representation otherwise. `get_type(node)` returns the `GraphIO` ID after `set_subnode`.
15. Update tests to validate:
    - declaration-only graphs
    - body creation from declaration
    - adding/removing ports through `GraphIO`
    - body staying consistent with `GraphIO`
    - driver/sink pin creation and connect
    - get_input_pin / get_output_pin on graph body
    - node/pin set_name / get_name / has_name / get_pin_name
    - sub-node pin name resolution from GraphIO
    - RCA adder example as integration test

Implementation notes:

- this phase should happen early because it changes graph identity, deletion, and persistence shape
- preserve the current `Port_ID` ordering model
- allow declaration-only entries with no body for library-style use cases
- no backwards compatibility with old `create_pin` / `add_edge` APIs

Exit criteria:

- `GraphIO` is the only public place to mutate graph interface ports
- `Graph` body creation reflects declaration ports automatically
- declaration-only graphs are first-class
- all pins are created with explicit direction (`create_driver_pin` / `create_sink_pin`)
- all edges are created via `connect_driver` / `connect_sink`
- `create_pin` and `add_edge` are removed
- node/pin names are built-in and queryable
- `get_pin_name` resolves port names from `GraphIO` declarations
- `set_subnode` accepts declaration objects directly
- the `sample.md` RCA adder example works end-to-end

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

Suggested steps:

1. Split body deletion from declaration deletion.
2. Implement body-only deletion semantics:
   - `delete_tree(tree)` clears only the tree body
   - `delete_graph(graph)` clears only the graph body
3. Implement declaration deletion semantics:
   - `delete_treeio(tio)` deletes declaration plus body
   - `delete_graphio(gio)` deletes declaration plus body
4. Preserve internal tombstone identity for deleted declarations.
5. Make normal `find_*` APIs ignore deleted declarations and return `nullptr`.
6. Add node/pin validity checks:
   - `is_valid()`
   - `is_invalid()`
7. Audit graph/tree operations to assert in debug mode when called on invalid references.
8. Update tests to cover:
   - body clear while declaration persists
   - declaration delete while tombstone remains internal
   - missing lookups returning `nullptr`
   - stale node/pin reference validity behavior

Implementation notes:

- the graph special case is important: declaration IO must survive body deletion
- declaration deletion should not expose tombstones through normal APIs
- keep invalid-reference checks cheap in release builds

Exit criteria:

- deletion semantics are unambiguous and test-covered
- stale handles can be queried for validity
- lookups skip tombstoned declarations

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

Suggested steps:

1. Define the registration API shape on:
   - `Forest`
   - `GraphLibrary`
2. Define which key families are supported first:
   - `_class`
   - `_flat`
   - `_hier`
3. Define which object families are supported first:
   - tree nodes
   - graph nodes
   - graph pins
4. Decide the minimum adapter or concept requirements for external maps.
5. Implement map registration without forcing a single map type.
6. Make save/print skip deleted objects even before eager metadata cleanup exists.
7. Add automatic cleanup hooks on delete when practical.
8. Add tests for:
   - external map registration
   - wrapper-keyed metadata lookup
   - deleted entries being ignored by save/print
   - optional cleanup on delete

Implementation notes:

- do not let metadata registration distort the structural core
- start with the minimum registration API needed by later save/print phases
- cleanup can be incremental as long as deleted data does not leak into output

Exit criteria:

- metadata registration exists at container level
- multiple external map types can participate
- deleted metadata does not appear in output paths

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

Suggested steps:

1. Define declaration persistence format:
   - names
   - stable internal IDs
   - tombstone information if needed
   - graph declaration IO data
2. Define body persistence format:
   - tree structure
   - graph structure
   - stable internal IDs
   - deleted-body behavior
3. Implement declaration save/load first.
4. Implement tree body save/load.
5. Implement graph body save/load.
6. Split declaration and body storage cleanly so bodies can stay unloaded.
7. Add lazy load on:
   - `find_tree(name)`
   - `find_graph(name)`
8. Add automatic body unload when no body `shared_ptr` remains.
9. Add debug-oriented text import/export after binary paths are stable.
10. Add tests for:
   - declaration-only load
   - lazy body load
   - unload/reload via `shared_ptr` lifetime
   - stable name/ID mapping
   - tree round-trip
   - graph round-trip

Implementation notes:

- declarations should be much cheaper to load than bodies
- text format is for debugging, not compatibility
- stable IDs matter more than compacting away tombstones

Exit criteria:

- declarations can load without bodies
- body loading is demand-driven
- body unload/reload is automatic
- binary round-trips preserve stable identity

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

Suggested steps:

1. Preserve and adapt tree printing to work with declaration-first ownership.
2. Decide whether print entry points live on:
   - `Tree`
   - `TreeIO`
   - both, with different roles
3. Add graph printing only after the declaration/body split is stable.
4. Keep graph printing intentionally simpler than persistence.
5. Add optional metadata printing hooks through registered maps when available.
6. Ensure deleted nodes/pins/declarations do not appear in standard output paths.
7. Add tests for:
   - tree debug print after declaration/body split
   - graph print on declaration-only and body-loaded cases
   - output skipping deleted data

Implementation notes:

- tree printing is more important early because it helps during refactor/debug work
- graph printing should avoid driving unnecessary format commitments

Exit criteria:

- tree printing remains practical during development
- graph printing exists in a simple useful form
- deleted data is consistently hidden from printed output

## Suggested sequencing inside implementation work

### Sequence A: API skeleton

1. land `TreeIO` / `GraphIO` types
2. land declaration-first creation and lookup
3. remove direct body creation APIs

### Sequence B: Graph declaration ownership and pin/connect refactor

1. move graph IO declaration into `GraphIO`
2. auto-materialize `Graph` bodies from `GraphIO`
3. add `get_input_pin` / `get_output_pin` on `Graph`
4. replace `create_pin` with `create_driver_pin` / `create_sink_pin`
5. replace `add_edge` with `connect_driver` / `connect_sink`
6. add built-in node/pin names (`set_name` / `get_name` / `has_name` / `get_pin_name`)
7. make `set_subnode` accept declaration objects directly
8. remove direct graph IO mutation from `Graph`
9. remove `create_pin` and `add_edge`

### Sequence C: Lifecycle

1. split body delete from declaration delete
2. add tombstone-hiding lookups
3. add validity checks for stale node/pin references

### Sequence D: Output and state

1. metadata registration minimum API
2. declaration persistence
3. body persistence
4. lazy load/unload
5. tree print
6. graph print

## Suggested future task extraction format

When turning a phase into implementation tasks later, create tasks with:

1. API surface to add/remove
2. internal data model changes
3. migration of current tests
4. new tests for the new semantics
5. cleanup/removal of superseded APIs

## Immediate Tasks

- Update docs before implementation work.
- Delete stale tracked planning docs.
- Remove compatibility-oriented dead code instead of preserving it.
- Start implementation with `TreeIO` / `GraphIO` because they drive many later decisions.
- Use `api.md` as the target public surface while implementation catches up.
