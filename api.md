# HHDS API Direction

This document captures the intended public API after the current cleanup and
refactor work. It is not a promise that every item is implemented today. It is
the target public model that the implementation should move toward.

The companion roadmap lives in [`TODO.md`](/Users/renau/projs/hhds/TODO.md).

## Principles

- Names are required.
- Names are immutable.
- `TreeIO` and `GraphIO` are the primary named declarations.
- `Tree` and `Graph` are optional implementation bodies attached to those
  declarations.
- Bodies are created only through declarations.
- Metadata is external to the core structure.
- Assertions are used for invalid usage; the API should not throw exceptions.
- Internal IDs remain stable and use tombstone semantics.

## Main object model

### Forest / TreeIO / Tree

- `Forest` owns all tree declarations and their optional bodies.
- `TreeIO` is the named declaration object for one tree entry.
- `Tree` is the implementation body for one `TreeIO`.
- A `TreeIO` and its `Tree` body share the same internal ID.
- A `TreeIO` can exist without a `Tree` body loaded.
- A `TreeIO` can have at most one body attached at a time.

### GraphLibrary / GraphIO / Graph

- `GraphLibrary` owns all graph declarations and their optional bodies.
- `GraphIO` is the named declaration object for one graph entry.
- `Graph` is the implementation body for one `GraphIO`.
- A `GraphIO` and its `Graph` body share the same internal ID.
- A `GraphIO` can exist without a `Graph` body loaded.
- A `GraphIO` can have at most one body attached at a time.
- `GraphIO` owns ordered graph input/output declarations.

## Ownership and lifetime

- `Forest`, `GraphLibrary`, `TreeIO`, `GraphIO`, `Tree`, and `Graph` should be
  public concrete types.
- Public ownership should use `std::shared_ptr`.
- When the last `std::shared_ptr<Tree>` or `std::shared_ptr<Graph>` disappears,
  the body may be unloaded automatically.
- A later lookup may reload the body automatically.
- Declarations remain resident even when bodies are unloaded.

## Public creation flow

### Tree declarations and bodies

Target shape:

```cpp
auto forest = std::make_shared<hhds::Forest>();

auto tio = forest->create_treeio("parser");
auto t   = tio->create_tree();
```

Expected public operations:

```cpp
class Forest {
public:
  [[nodiscard]] std::shared_ptr<TreeIO> create_treeio(std::string_view name);
  [[nodiscard]] std::shared_ptr<TreeIO> find_treeio(std::string_view name) const;
  [[nodiscard]] std::shared_ptr<Tree>   find_tree(std::string_view name);
  [[nodiscard]] std::shared_ptr<const Tree> find_tree(std::string_view name) const;

  void delete_treeio(std::string_view name);
  void delete_treeio(const std::shared_ptr<TreeIO>& tio);
  void delete_tree(const std::shared_ptr<Tree>& tree);
};

class TreeIO {
public:
  [[nodiscard]] std::string_view        get_name() const noexcept;
  [[nodiscard]] std::shared_ptr<Forest> get_forest() const;
  [[nodiscard]] std::shared_ptr<Tree>   get_tree();
  [[nodiscard]] std::shared_ptr<const Tree> get_tree() const;
  [[nodiscard]] std::shared_ptr<Tree>   create_tree();
  [[nodiscard]] bool                    has_tree() const;
};
```

Notes:

- `create_treeio(name)` requires a unique name.
- `TreeIO::create_tree()` creates an empty tree body.
- There is no standalone `Tree` creation API.

### Graph declarations and bodies

Target shape:

```cpp
auto glib = std::make_shared<hhds::GraphLibrary>();

auto gio = glib->create_graphio("alu");
gio->add_input("a", 1);
gio->add_input("b", 2);
gio->add_output("y", 3);

auto g = gio->create_graph();
```

Expected public operations:

```cpp
class GraphLibrary {
public:
  [[nodiscard]] std::shared_ptr<GraphIO> create_graphio(std::string_view name);
  [[nodiscard]] std::shared_ptr<GraphIO> find_graphio(std::string_view name) const;
  [[nodiscard]] std::shared_ptr<Graph>   find_graph(std::string_view name);
  [[nodiscard]] std::shared_ptr<const Graph> find_graph(std::string_view name) const;

  void delete_graphio(std::string_view name);
  void delete_graphio(const std::shared_ptr<GraphIO>& gio);
  void delete_graph(const std::shared_ptr<Graph>& graph);
};

class GraphIO {
public:
  [[nodiscard]] std::string_view             get_name() const noexcept;
  [[nodiscard]] std::shared_ptr<GraphLibrary> get_library() const;
  [[nodiscard]] std::shared_ptr<Graph>       get_graph();
  [[nodiscard]] std::shared_ptr<const Graph> get_graph() const;
  [[nodiscard]] std::shared_ptr<Graph>       create_graph();
  [[nodiscard]] bool                         has_graph() const;

  void add_input(std::string_view name, Port_ID port_id);
  void add_output(std::string_view name, Port_ID port_id);
  void delete_input(std::string_view name);
  void delete_output(std::string_view name);

  [[nodiscard]] bool has_input(std::string_view name) const;
  [[nodiscard]] bool has_output(std::string_view name) const;
  [[nodiscard]] Port_ID get_input_port_id(std::string_view name) const;
  [[nodiscard]] Port_ID get_output_port_id(std::string_view name) const;
};
```

Notes:

- `GraphIO` owns graph interface declaration.
- There is no standalone `Graph` creation API.
- Direct graph IO mutation APIs on `Graph` should be removed.
- Creating a `Graph` body should auto-materialize the IO structure described by
  `GraphIO`.
- Editing `GraphIO` while a body exists should update the body consistently.

## Lookup and find semantics

- `find_treeio(name)` / `find_graphio(name)` return the declaration object or
  `nullptr`.
- `find_tree(name)` / `find_graph(name)` return the body object or `nullptr`.
- `find_tree(name)` / `find_graph(name)` may trigger lazy body load.
- Find operations ignore tombstoned declarations.
- Missing lookup is not an assertion failure; returning `nullptr` is fine.

## Deletion semantics

### Body deletion

```cpp
forest->delete_tree(tree);
glib->delete_graph(graph);
```

Meaning:

- delete only the implementation body
- keep the declaration object
- keep the declaration name
- keep the internal ID mapping

Special graph rule:

- `delete_graph(graph)` deletes implementation state, not `GraphIO` port
  declarations

### Declaration deletion

```cpp
forest->delete_treeio(tio);
glib->delete_graphio(gio);
```

Meaning:

- delete declaration and body
- keep the internal tombstone mapping
- hide the deleted declaration from normal `find_*`

## Tree API direction

### Tree creation and emptiness

- `TreeIO::create_tree()` creates an empty tree body.
- Empty trees are valid.
- Tree remains generic and does not gain a built-in input/output model.

### Structure operations

The existing structural tree API remains the direction:

```cpp
auto root  = tree->add_root_node();
auto child = tree->add_child(root);
tree->set_subnode(node, subtree);
```

Expected shape:

- keep structural creation and mutation
- keep subtree references
- keep traversal helpers
- keep cursor support
- keep inline `Type`

### Tree wrappers

The wrapper types remain plain value-key objects:

- `Tree::Node_class`
- `Tree::Node_flat`
- `Tree::Node_hier`

They should:

- be hashable
- be comparable
- stay pointer-free
- remain useful as metadata keys

## Graph API direction

### Graph structure operations

`Graph` remains the implementation body and still owns:

- internal nodes
- internal pins
- edges
- traversal
- hierarchy references to other graphs
- inline `Type`

`Graph` should no longer own declaration-side graph IO editing.

### Graph wrappers

The wrapper/key types remain public and useful for metadata:

- `Node_class`
- `Node_flat`
- `Node_hier`
- `Pin_class`
- `Pin_flat`
- `Pin_hier`

Direction:

- lightweight key/context objects should avoid shared ownership
- they remain usable to construct richer operations
- they remain valid metadata keys

### Validity checks

Node and pin references may go stale after deletes. The API should add:

```cpp
[[nodiscard]] bool is_valid() const noexcept;
[[nodiscard]] bool is_invalid() const noexcept;
```

Normal operation methods may assert in debug mode if called when invalid.

## Hierarchy APIs

### Declaration/body references

- Public hierarchy links should accept either declaration/body name or
  `std::shared_ptr` where appropriate.
- Internally those links may still use stable internal IDs.

### Tree hierarchy

- Tree subtree references remain part of the body API.
- The top of a hierarchical traversal is selected by the caller, not stored as
  a permanent property of `Forest`.

### Graph hierarchy

- Graph hierarchy remains body-level structure.
- The top of a hierarchical traversal is selected by the caller, not stored as
  a permanent property of `GraphLibrary`.

## Metadata registration direction

Metadata remains external, but the long-term API should support registration on
`Forest` and `GraphLibrary`.

Expected direction:

```cpp
forest->add_map(node_names);
forest->add_map(node_attrs);
glib->add_map(pin_widths);
glib->add_map(node_names);
```

Requirements:

- support wrapper-keyed maps
- support `_class`, `_flat`, and `_hier` keys
- cleanup should happen on delete eventually
- deleted objects should not print or save

This registration is a later phase and should not distort the core structure
API now.

## Persistence direction

### Formats

- binary is the primary format
- text load/save is for debugging and manual intervention
- text format can change over time

### Storage split

- declarations and bodies should be saved separately
- name/ID mapping should be stable
- large graphs/trees should not require eager body load

### Lazy load/unload

- declarations are loaded first
- bodies are loaded on demand
- bodies can unload automatically when the last `shared_ptr` disappears

## APIs to remove or restrict

- standalone public `Tree` creation
- standalone public `Graph` creation
- public explicit-ID creation APIs
- direct graph IO mutation on `Graph`
- payload-owning tree APIs
- exception-based public error handling
