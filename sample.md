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
#include "hhds/attrs/name.hpp"

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

// Concat: fixed output "y", variable numbered inputs (like mux sink pins).
// No add_input declarations — sink pins are created with numeric names.
auto cat_gio = glib->create_graphio("concat");
cat_gio->add_output("y", 0);

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

// Node names use the built-in attribute — storage is lazy, no registration needed
using hhds::attrs::name;

auto xor1 = fa->create_node();  xor1.attr(name).set("xor1");
auto xor2 = fa->create_node();  xor2.attr(name).set("xor2");
auto and1 = fa->create_node();  and1.attr(name).set("and1");
auto and2 = fa->create_node();  and2.attr(name).set("and2");
auto and3 = fa->create_node();  and3.attr(name).set("and3");
auto or1  = fa->create_node();  or1.attr(name).set("or1");
auto or2  = fa->create_node();  or2.attr(name).set("or2");

assert(xor1.attr(name).has());
assert(xor1.attr(name).get() == "xor1");

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
auto sel_a0 = top->create_node();  sel_a0.attr(name).set("sel_a[0]");
auto sel_a1 = top->create_node();  sel_a1.attr(name).set("sel_a[1]");
auto sel_a2 = top->create_node();  sel_a2.attr(name).set("sel_a[2]");
auto sel_a3 = top->create_node();  sel_a3.attr(name).set("sel_a[3]");
auto sel_b0 = top->create_node();  sel_b0.attr(name).set("sel_b[0]");
auto sel_b1 = top->create_node();  sel_b1.attr(name).set("sel_b[1]");
auto sel_b2 = top->create_node();  sel_b2.attr(name).set("sel_b[2]");
auto sel_b3 = top->create_node();  sel_b3.attr(name).set("sel_b[3]");

// Connect top input a/b → each bit-select sink
for (auto sel : {sel_a0, sel_a1, sel_a2, sel_a3}) {
  top->create_sink_pin(sel, 1).connect_driver(top_a);
}
for (auto sel : {sel_b0, sel_b1, sel_b2, sel_b3}) {
  top->create_sink_pin(sel, 1).connect_driver(top_b);
}

// Constant zero node for the first carry-in (driver pin 0, no sinks)
auto const0 = top->create_node();  const0.attr(name).set("1'b0");

// Concatenation node: variable numbered sink pins, output "y"
auto cat = top->create_node();  cat.attr(name).set("concat_sum");
top->set_subnode(cat, cat_gio);

// FullAdder instances — set_subnode links to the GraphIO declaration,
// which means the node's pins are named by the declaration ports.
auto fa0 = top->create_node();  fa0.attr(name).set("fa0");
auto fa1 = top->create_node();  fa1.attr(name).set("fa1");
auto fa2 = top->create_node();  fa2.attr(name).set("fa2");
auto fa3 = top->create_node();  fa3.attr(name).set("fa3");

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
assert(fa0.attr(name).get() == "fa0");
assert(fa0.attr(name).has());

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

// FA sum outputs → concat numbered sink pins, concat "y" → top output
top->create_sink_pin(cat, "0").connect_driver(top->create_driver_pin(fa0, "out"));
top->create_sink_pin(cat, "1").connect_driver(top->create_driver_pin(fa1, "out"));
top->create_sink_pin(cat, "2").connect_driver(top->create_driver_pin(fa2, "out"));
top->create_sink_pin(cat, "3").connect_driver(top->create_driver_pin(fa3, "out"));
top->create_driver_pin(cat, "y").connect_sink(top_sum);

// fa3.cout → top cout
top->create_driver_pin(fa3, "cout").connect_sink(top_cout);

// ---------------------------------------------------------------
// Forward topological traversal of the top-level graph
// ---------------------------------------------------------------
std::cout << "Topological order (adder top-level):\n";
for (auto node : top->forward_class()) {
  std::cout << "  " << (node.attr(name).has() ? node.attr(name).get() : "?") << "\n";
}
// Expected order respects dataflow:
//   const0, sel_a[0..3], sel_b[0..3] (inputs, any order among them)
//   fa0, fa1, fa2, fa3 (ripple carry chain)
//   concat_sum
```

## Wrapper types and compact conversions

```cpp
auto g    = gio->create_graph();
auto node = g->create_node();
auto pin  = g->create_sink_pin(node, 3);

// Node_class / Pin_class — lightweight keys within a single graph
hhds::Node_class nc = node.nclass();
hhds::Pin_class  pc = pin.pclass();

// Flat conversions — for cross-graph use
auto nf = node.flat();
auto pf = pin.flat();

// All lightweight ID types are hashable — usable as attribute storage keys
// Attributes use these internally (each tag declares its key_type)
```

## Downstream attributes

Downstream projects add attributes without editing HHDS. Each attribute is a
tag object with ADL overloads in one header file. See
[`api_attribute.md`](/Users/renau/projs/hhds/api_attribute.md) for the full
design.

### Defining a downstream attribute

```cpp
// livehd/attrs/bits.hpp
#pragma once
#include "hhds/attr.hpp"

namespace livehd::attrs {

struct bits_t {
  using value_type = int;
  using key_type   = hhds::Node_flat;  // keyed globally across hierarchy
};
inline constexpr bits_t bits{};

inline int hhds_attr_get(bits_t, const hhds::Node& n) {
  return n.glib().attr_store(bits).at(n.flat());
}
inline void hhds_attr_set(bits_t, const hhds::Node& n, int v) {
  n.glib().attr_store(bits)[n.flat()] = v;
}
inline bool hhds_attr_has(bits_t, const hhds::Node& n) {
  return n.glib().attr_store(bits).contains(n.flat());
}
inline void hhds_attr_del(bits_t, const hhds::Node& n) {
  n.glib().attr_store(bits).erase(n.flat());
}

}  // namespace livehd::attrs
```

### Using downstream attributes

```cpp
#include "livehd/attrs/bits.hpp"

node.attr(livehd::attrs::bits).set(32);
int b = node.attr(livehd::attrs::bits).get();   // int by value

if (node.attr(livehd::attrs::bits).has()) {
  node.attr(livehd::attrs::bits).del();          // delete single entry
}

// Clear all bits data across the library
glib->attr_clear(livehd::attrs::bits);

// Clear all attribute data across all tags
glib->attr_clear();
```

## Persistence and lazy loading

```cpp
#include "hhds/tree.hpp"
#include "hhds/attrs/name.hpp"

using hhds::attrs::name;

// Create and populate
{
  auto forest = std::make_shared<hhds::Forest>("/tmp/persistent_trees");

  auto tio = forest->create_treeio("big_ast");
  auto t   = tio->create_tree();
  auto root = t->add_root_node();
  root.attr(name).set("program");

  // Declarations and attribute data are saved to /tmp/persistent_trees/
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
#include "hhds/tree.hpp"
#include "hhds/attrs/name.hpp"

using hhds::attrs::name;

auto forest = std::make_shared<hhds::Forest>("/tmp/debug_trees");
auto tio = forest->create_treeio("example");
auto t   = tio->create_tree();

auto root  = t->add_root_node();  root.attr(name).set("add");
auto child = t->add_child(root);  child.attr(name).set("literal");
t->set_type(root, 1);

// print uses built-in name attribute automatically
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
