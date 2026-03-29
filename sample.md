# HHDS API Samples

Target API examples after the TODO/api.md cleanup is complete. Not all of these
are implemented today.

## Tree basics

```cpp
#include "hhds/tree.hpp"

// Forest owns all tree declarations and bodies.
// Path is the persistence directory for lazy load/save.
auto forest = std::make_shared<hhds::Forest>("/tmp/my_project");

// Declare a tree (name is required and immutable)
auto tio = forest->create_treeio("parser");

// Create the implementation body from the declaration
auto t = tio->create_tree();

// Build structure
auto root  = t->add_root_node();
auto lhs   = t->add_child(root);
auto rhs   = t->add_child(root);
auto leaf  = t->add_child(lhs);

// Inline type tag (small integer, not metadata)
t->set_type(root, 1);
t->set_type(leaf, 2);

// Traversal
for (auto node : t->pre_order()) {
  std::cout << node.get_current_pos() << "\n";
}
for (auto node : t->sibling_order(lhs)) {
  std::cout << node.get_current_pos() << "\n";
}

// Cursor-based navigation
auto cursor = t->create_cursor(root);
cursor.goto_first_child();
cursor.goto_next_sibling();
cursor.goto_parent();
```

## Tree lookup and deletion

```cpp
// Lookup by name — returns the same shared_ptr every time
auto tio2 = forest->find_treeio("parser");   // same object as tio
auto t2   = forest->find_tree("parser");      // same body as t
auto t3   = tio->get_tree();                  // also the same body

// Check existence
bool has_body = tio->has_tree();

// Delete body only — declaration survives
forest->delete_tree(t);
// tio is still valid, tio->has_tree() is now false

// Re-create a fresh body on the same declaration
auto t_new = tio->create_tree();

// Delete declaration and body — tombstone remains internal
forest->delete_treeio(tio);
// find_treeio("parser") now returns nullptr
```

## Forest hierarchy (subtree references)

```cpp
auto forest = std::make_shared<hhds::Forest>("/tmp/hier_project");

auto top_tio = forest->create_treeio("top");
auto sub_tio = forest->create_treeio("sub");

auto top = top_tio->create_tree();
auto sub = sub_tio->create_tree();

auto top_root = top->add_root_node();
auto callsite = top->add_child(top_root);
auto sub_root = sub->add_root_node();
auto sub_leaf = sub->add_child(sub_root);

// Cross-tree reference — pass the declaration directly
top->set_subnode(callsite, sub_tio);

// Hierarchical cursor walks across trees automatically
auto hcursor = forest->create_cursor(top_tio);
hcursor.goto_first_child();   // callsite in top
hcursor.goto_first_child();   // enters sub tree, lands on sub_root
```

## Graph basics

```cpp
#include "hhds/graph.hpp"

auto glib = std::make_shared<hhds::GraphLibrary>("/tmp/my_graphs");

// Declare a graph with its interface ports
auto gio = glib->create_graphio("alu");
gio->add_input("a", 1);
gio->add_input("b", 2);
gio->add_output("y", 3);

// Create the implementation body (auto-materializes IO structure)
auto g = gio->create_graph();

// Internal node with explicit sink pins
auto add_node = g->create_node();
auto add_in_a = g->create_sink_pin(add_node, 1);
auto add_in_b = g->create_sink_pin(add_node, 2);

// Connect: graph inputs → add_node sinks, add_node driver → graph output
// connect_driver on a sink pin means "connect a driver to me"
// connect_sink on a driver/node means "connect a sink to me"
add_in_a.connect_driver(g->get_input_pin("a"));
add_in_b.connect_driver(g->get_input_pin("b"));
add_node.connect_sink(g->get_output_pin("y"));

// Traversal
for (auto node : g->fast_hier()) {
  auto edges = g->out_edges(node);
  // ...
}
```

## Graph lookup and deletion

```cpp
auto gio2 = glib->find_graphio("alu");  // same object as gio
auto g2   = glib->find_graph("alu");    // same body as g

// GraphIO port queries
bool has_a = gio->has_input("a");
auto pid   = gio->get_input_port_id("a");

// Delete body — declaration and ports survive
glib->delete_graph(g);
// gio is still valid, gio->has_input("a") is still true

// Delete declaration, body, and port info — tombstone internal
glib->delete_graphio(gio);
// find_graphio("alu") now returns nullptr
```

## Graph hierarchy

```cpp
auto glib = std::make_shared<hhds::GraphLibrary>("/tmp/hier_graphs");

auto top_gio = glib->create_graphio("top");
auto alu_gio = glib->create_graphio("alu");
alu_gio->add_input("a", 1);
alu_gio->add_output("y", 2);

auto top = top_gio->create_graph();
auto alu = alu_gio->create_graph();

// Instantiate alu inside top — pass the declaration directly
auto inst = top->create_node();
top->set_subnode(inst, alu_gio);
```

## Ripple-carry adder (hierarchical graph example)

A 4-bit ripple-carry adder built from `FullAdder` sub-modules. Demonstrates
graph IO declarations, hierarchy via `set_subnode`, bit-select and concatenation
nodes, and topological traversal.

```cpp
#include "hhds/graph.hpp"
#include "absl/container/flat_hash_map.h"

auto glib = std::make_shared<hhds::GraphLibrary>("/tmp/rca");

// ---------------------------------------------------------------
// Declare FullAdder interface
// ---------------------------------------------------------------
// Declare primitive gate interfaces.
// Commutative gates: all inputs share port "a", output is "y".
// No need to distinguish inputs for xor/and/or.
auto xor_gio = glib->create_graphio("xor");
xor_gio->add_input("a", 0);
xor_gio->add_output("y", 0);

auto and_gio = glib->create_graphio("and");
and_gio->add_input("a", 0);
and_gio->add_output("y", 0);

auto or_gio = glib->create_graphio("or");
or_gio->add_input("a", 0);
or_gio->add_output("y", 0);

// Registers use loop_last to break cycles in topological traversal.
// All ports on a register are typically loop_last.
auto reg_gio = glib->create_graphio("register");
reg_gio->add_input("d", 0, /*loop_last=*/true);
reg_gio->add_output("q", 0, /*loop_last=*/true);
assert(reg_gio->is_loop_last("d"));
assert(reg_gio->is_loop_last("q"));

auto fa_gio = glib->create_graphio("FullAdder");
fa_gio->add_input("in1",  1);
fa_gio->add_input("in2",  2);
fa_gio->add_input("cin",  3);
fa_gio->add_output("out",  1);
fa_gio->add_output("cout", 5);

// Build the FullAdder body
//
//   in1 ─┬─→ [xor1] ──┬─→ [xor2] ──→ out
//   in2 ─┤             │
//         │    cin ─────┤
//         │             │
//   in1 ──┬─→ [and1] ──┬─→ [or1] ──┬─→ [or2] ──→ cout
//   in2 ──┘             │           │
//   in1 ──┬─→ [and2] ──┘           │
//   cin ──┘                         │
//   in2 ──┬─→ [and3] ──────────────┘
//   cin ──┘
//
auto fa = fa_gio->create_graph();

// Node names are built-in — no external map needed
auto xor1 = fa->create_node();  xor1.set_name("xor1");
auto xor2 = fa->create_node();  xor2.set_name("xor2");
auto and1 = fa->create_node();  and1.set_name("and1");
auto and2 = fa->create_node();  and2.set_name("and2");
auto and3 = fa->create_node();  and3.set_name("and3");
auto or1  = fa->create_node();  or1.set_name("or1");
auto or2  = fa->create_node();  or2.set_name("or2");

assert(xor1.has_name());
assert(xor1.get_name() == "xor1");

// Mark gate types — pins inherit from the GraphIO declaration.
// All inputs are "a" (commutative), output is "y" (pin 0 default).
// set_subnode stores the GraphIO ID in the node's 16-bit type field
// when the ID fits (< 2^16), keeping the compact representation.
fa->set_subnode(xor1, xor_gio);
fa->set_subnode(xor2, xor_gio);
fa->set_subnode(and1, and_gio);
fa->set_subnode(and2, and_gio);
fa->set_subnode(and3, and_gio);
fa->set_subnode(or1,  or_gio);
fa->set_subnode(or2,  or_gio);

// After set_subnode, get_type returns the GraphIO's ID
assert(fa->get_type(xor1) == xor_gio->get_gid());
assert(fa->get_type(and1) == and_gio->get_gid());

// Graph IO pins (auto-materialized from FullAdder's GraphIO declaration)
// get_input_pin returns a driver inside the body (data flows in)
// get_output_pin returns a sink inside the body (data flows out)
auto fa_in1  = fa->get_input_pin("in1");
auto fa_in2  = fa->get_input_pin("in2");
auto fa_cin  = fa->get_input_pin("cin");
auto fa_out  = fa->get_output_pin("out");
auto fa_cout = fa->get_output_pin("cout");

// get_pin_name returns the port name from the GraphIO declaration
assert(fa_in1.get_pin_name() == "in1");
assert(fa_cout.get_pin_name() == "cout");

// For commutative gates: connect_driver uses default sink "a",
// connect_sink uses default driver "y" (pin 0, implicit via node).
// Explicit pin names shown on the first few connections as examples.

// xor1 = in1 ^ in2
auto xor1_sink = fa->create_sink_pin(xor1, "a");   // named form
assert(xor1_sink.get_pin_name() == "a");            // from xor GraphIO
xor1_sink.connect_driver(fa_in1);
xor1.connect_driver(fa_in2);                        // short form, also "a"

// xor2 = xor1 ^ cin  →  out
xor2.connect_driver(xor1);        // default "a" ← default "y"
xor2.connect_driver(fa_cin);
xor2.connect_sink(fa_out);

// and1 = in1 & in2
and1.connect_driver(fa_in1);
and1.connect_driver(fa_in2);

// and2 = in1 & cin
and2.connect_driver(fa_in1);
and2.connect_driver(fa_cin);

// and3 = in2 & cin
and3.connect_driver(fa_in2);
and3.connect_driver(fa_cin);

// or1 = and1 | and2
or1.connect_driver(and1);
or1.connect_driver(and2);

// or2 = or1 | and3  →  cout
or2.connect_driver(or1);
or2.connect_driver(and3);
or2.connect_sink(fa_cout);

// ---------------------------------------------------------------
// Declare the 4-bit adder top-level
// ---------------------------------------------------------------
auto top_gio = glib->create_graphio("adder");
top_gio->add_input("a",    1);   // 4-bit input
top_gio->add_input("b",    2);   // 4-bit input
top_gio->add_output("sum",  0);   // 4-bit output
top_gio->add_output("cout", 1);   // 1-bit output

auto top = top_gio->create_graph();

auto top_a    = top->get_input_pin("a");
auto top_b    = top->get_input_pin("b");
auto top_sum  = top->get_output_pin("sum");
auto top_cout = top->get_output_pin("cout");

// Bit-select nodes: each has one sink (the bus) and driver pin 0 (selected bit)
auto sel_a0 = top->create_node();  sel_a0.set_name("sel_a[0]");
auto sel_a1 = top->create_node();  sel_a1.set_name("sel_a[1]");
auto sel_a2 = top->create_node();  sel_a2.set_name("sel_a[2]");
auto sel_a3 = top->create_node();  sel_a3.set_name("sel_a[3]");
auto sel_b0 = top->create_node();  sel_b0.set_name("sel_b[0]");
auto sel_b1 = top->create_node();  sel_b1.set_name("sel_b[1]");
auto sel_b2 = top->create_node();  sel_b2.set_name("sel_b[2]");
auto sel_b3 = top->create_node();  sel_b3.set_name("sel_b[3]");

// Connect top input a/b → each bit-select sink
for (auto sel : {sel_a0, sel_a1, sel_a2, sel_a3}) {
  top->create_sink_pin(sel, 1).connect_driver(top_a);
}
for (auto sel : {sel_b0, sel_b1, sel_b2, sel_b3}) {
  top->create_sink_pin(sel, 1).connect_driver(top_b);
}

// Constant zero node for the first carry-in (driver pin 0, no sinks)
auto const0 = top->create_node();  const0.set_name("1'b0");

// Concatenation node: sink pins 1..4 gather bits, driver pin 0 = result
auto cat = top->create_node();  cat.set_name("concat_sum");

// FullAdder instances — set_subnode links to the GraphIO declaration,
// which means the node's pins are named by the declaration ports.
auto fa0 = top->create_node();  fa0.set_name("fa0");
auto fa1 = top->create_node();  fa1.set_name("fa1");
auto fa2 = top->create_node();  fa2.set_name("fa2");
auto fa3 = top->create_node();  fa3.set_name("fa3");

top->set_subnode(fa0, fa_gio);
top->set_subnode(fa1, fa_gio);
top->set_subnode(fa2, fa_gio);
top->set_subnode(fa3, fa_gio);

// Sub-node pins use port names from the GraphIO declaration.
// create_sink_pin(node, "name") and create_driver_pin(node, "name")
// resolve the name through the sub-node's GraphIO to the correct port id.

// get_pin_name on sub-node pins returns the GraphIO port name
auto fa0_in1_pin = top->create_sink_pin(fa0, "in1");
assert(fa0_in1_pin.get_pin_name() == "in1");
assert(fa0.get_name() == "fa0");
assert(fa0.has_name());

// fa0: sel_a[0] → in1, sel_b[0] → in2, const0 → cin
fa0_in1_pin.connect_driver(sel_a0);
top->create_sink_pin(fa0, "in2").connect_driver(sel_b0);
top->create_sink_pin(fa0, "cin").connect_driver(const0);

// fa1: sel_a[1] → in1, sel_b[1] → in2, fa0.cout → cin
top->create_sink_pin(fa1, "in1").connect_driver(sel_a1);
top->create_sink_pin(fa1, "in2").connect_driver(sel_b1);
top->create_sink_pin(fa1, "cin").connect_driver(top->create_driver_pin(fa0, "cout"));

// fa2: sel_a[2] → in1, sel_b[2] → in2, fa1.cout → cin
top->create_sink_pin(fa2, "in1").connect_driver(sel_a2);
top->create_sink_pin(fa2, "in2").connect_driver(sel_b2);
top->create_sink_pin(fa2, "cin").connect_driver(top->create_driver_pin(fa1, "cout"));

// fa3: sel_a[3] → in1, sel_b[3] → in2, fa2.cout → cin
top->create_sink_pin(fa3, "in1").connect_driver(sel_a3);
top->create_sink_pin(fa3, "in2").connect_driver(sel_b3);
top->create_sink_pin(fa3, "cin").connect_driver(top->create_driver_pin(fa2, "cout"));

// FA sum outputs → concat sink pins
top->create_sink_pin(cat, 1).connect_driver(top->create_driver_pin(fa0, "out"));
top->create_sink_pin(cat, 2).connect_driver(top->create_driver_pin(fa1, "out"));
top->create_sink_pin(cat, 3).connect_driver(top->create_driver_pin(fa2, "out"));
top->create_sink_pin(cat, 4).connect_driver(top->create_driver_pin(fa3, "out"));
cat.connect_sink(top_sum);  // concat driver pin 0 → top output

// fa3.cout → top cout
top->create_driver_pin(fa3, "cout").connect_sink(top_cout);

// ---------------------------------------------------------------
// Forward topological traversal of the top-level graph
// ---------------------------------------------------------------
std::cout << "Topological order (adder top-level):\n";
for (auto node : top->forward_class()) {
  std::cout << "  " << (node.has_name() ? node.get_name() : "?") << "\n";
}
// Expected order respects dataflow:
//   const0, sel_a[0..3], sel_b[0..3] (inputs, any order among them)
//   fa0, fa1, fa2, fa3 (ripple carry chain)
//   concat_sum
```

## Wrapper types and compact conversions

```cpp
hhds::Graph g;
auto node = g.create_node();
auto pin  = g.create_sink_pin(node, 3);

// Node_class / Pin_class — lightweight keys within a single graph
hhds::Node_class nc = node;
hhds::Pin_class  pc = pin;

// Flat conversions — pass the graph directly for cross-graph use
auto node_flat = hhds::to_flat(node, g);
auto pin_flat  = hhds::to_flat(pin, g);

// Round-trip back to class
auto nc2 = hhds::to_class(node_flat);
auto pc2 = hhds::to_class(pin_flat);

// All wrapper types are hashable — usable as metadata keys
absl::flat_hash_map<hhds::Node_class, int>  node_attrs;
absl::flat_hash_map<hhds::Pin_flat, double>  pin_delays;
```

## Metadata registration

Metadata lives in external maps. Registering maps with `Forest` or
`GraphLibrary` enables automatic cleanup on delete, save/load with persistence,
and print integration.

### Common types (built-in serialization)

For `std::string`, integer types, `float`, and `double`, no custom serializer is
needed:

```cpp
auto forest = std::make_shared<hhds::Forest>("/tmp/my_project");

absl::flat_hash_map<hhds::Tree::Node_class, std::string> node_names;
absl::flat_hash_map<hhds::Tree::Node_class, int>         node_weights;

// Built-in serialization for common types
forest->add_map<hhds::Tree::Node_class>(node_names);
forest->add_map<hhds::Tree::Node_class>(node_weights);

auto tio = forest->create_treeio("ast");
auto t   = tio->create_tree();

auto root = t->add_root_node();
node_names[root]   = "top";
node_weights[root] = 42;

// When the forest saves, registered maps are saved too.
// When a node is deleted, registered maps clean up the entry.
```

### Custom types (user-provided adapter)

For structs or other types, inherit from `MapAdapterBase` to provide
erase/save/load:

```cpp
struct PinTiming {
  double rise;
  double fall;
};

class PinTimingAdapter : public hhds::MapAdapterBase {
  absl::flat_hash_map<hhds::Pin_class, PinTiming>& map_;
public:
  explicit PinTimingAdapter(absl::flat_hash_map<hhds::Pin_class, PinTiming>& m)
      : map_(m) {}

  void erase(uint64_t raw_id) override {
    map_.erase(hhds::Pin_class(raw_id));
  }
  void save(std::ostream& os) const override {
    // write map size, then key/value pairs
    uint64_t n = map_.size();
    os.write(reinterpret_cast<const char*>(&n), sizeof(n));
    for (const auto& [k, v] : map_) {
      uint64_t id = k.get_raw_id();
      os.write(reinterpret_cast<const char*>(&id), sizeof(id));
      os.write(reinterpret_cast<const char*>(&v), sizeof(v));
    }
  }
  void load(std::istream& is) override {
    uint64_t n;
    is.read(reinterpret_cast<char*>(&n), sizeof(n));
    for (uint64_t i = 0; i < n; ++i) {
      uint64_t id;
      PinTiming v;
      is.read(reinterpret_cast<char*>(&id), sizeof(id));
      is.read(reinterpret_cast<char*>(&v), sizeof(v));
      map_[hhds::Pin_class(id)] = v;
    }
  }
};

auto glib = std::make_shared<hhds::GraphLibrary>("/tmp/my_graphs");

absl::flat_hash_map<hhds::Pin_class, PinTiming> timing;
glib->add_map(std::make_unique<PinTimingAdapter>(timing));
```

## Persistence and lazy loading

```cpp
// Create and populate
{
  auto forest = std::make_shared<hhds::Forest>("/tmp/persistent_trees");

  absl::flat_hash_map<hhds::Tree::Node_class, std::string> names;
  forest->add_map<hhds::Tree::Node_class>(names);

  auto tio = forest->create_treeio("big_ast");
  auto t   = tio->create_tree();
  auto root = t->add_root_node();
  names[root] = "program";

  // Declarations and registered maps are saved to /tmp/persistent_trees/
  // When t (the shared_ptr<Tree>) goes out of scope, the body may be
  // unloaded automatically and saved to disk.
}

// Reload later — declarations load eagerly, bodies load lazily
{
  auto forest = std::make_shared<hhds::Forest>("/tmp/persistent_trees");

  // Declaration is already available
  auto tio = forest->find_treeio("big_ast");
  assert(tio != nullptr);

  // Body loads on demand
  auto t = forest->find_tree("big_ast");
  // t is now loaded from disk
}
```

## Validity checks

```cpp
auto g    = gio->create_graph();
auto node = g->create_node();
auto pin  = g->create_sink_pin(node, 1);

assert(node.is_valid());
assert(pin.is_valid());

g->delete_node(node);

assert(node.is_invalid());   // stale reference detected
assert(pin.is_invalid());    // pin is also invalid
```

## Debug printing

```cpp
auto forest = std::make_shared<hhds::Forest>("/tmp/debug_trees");
auto tio = forest->create_treeio("example");
auto t   = tio->create_tree();

auto root  = t->add_root_node();  root.set_name("add");
auto child = t->add_child(root); child.set_name("literal");
t->set_type(root, 1);

// print uses built-in node names automatically
t->print(std::cout);

// PrintOptions can still customize output with extra attributes
hhds::Tree::PrintOptions opts;
opts.attributes = {
  {"type_id", [&t](const hhds::Tree::Node_class& n) -> std::optional<std::string> {
    return std::to_string(t->get_type(n));
  }},
};
t->print(std::cout, opts);
```
