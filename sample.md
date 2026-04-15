# HHDS API Samples

Target API examples. Not all of these are implemented today.

---

## Graph Examples

### Example 1: Graph basics, flat attributes, forward traversal

```cpp
#include "hhds/graph.hpp"
#include "hhds/attrs/name.hpp"

auto glib = std::make_shared<hhds::GraphLibrary>("/tmp/test");
auto gio  = glib->create_io("alu");
auto g    = gio->create_graph();

// create_node returns a Node (holds pointer to Graph + internal ID)
auto n1 = g->create_node();
auto n2 = g->create_node();

// Flat attribute — stored per-graph, keyed by internal raw_nid
using hhds::attrs::name;
n1.attr(name).set("adder");
n2.attr(name).set("mux");
assert(n1.attr(name).get() == "adder");

// Iterators return Node objects with .attr() support
for (auto node : g->forward_class()) {
  if (node.attr(name).has()) {
    std::cout << node.attr(name).get() << "\n";
  }
}

// For external storage keyed by node identity, get a hashable opaque index:
auto idx = n1.get_flat_index();
std::unordered_map<decltype(idx), int> my_external_data;
my_external_data[idx] = 42;
```

### Example 2: Three traversal modes

Three forward traversal modes — all return `Node`, but with different context
levels. The context determines which `get_*_index()` calls are valid.

```cpp
#include "hhds/graph.hpp"
#include "hhds/attrs/name.hpp"

using hhds::attrs::name;

auto glib = std::make_shared<hhds::GraphLibrary>("/tmp/test");

auto alu_gio = glib->create_io("alu");
alu_gio->add_input("a", 1);
alu_gio->add_output("y", 0);

auto top_gio = glib->create_io("top");
auto top = top_gio->create_graph();

// ALU body must exist for flat/hier traversals to enter it
auto alu = alu_gio->create_graph();
auto alu_add = alu->create_node();
alu_add.attr(name).set("alu_internal");

auto inst1 = top->create_node();  inst1.attr(name).set("alu_0");
auto inst2 = top->create_node();  inst2.attr(name).set("alu_1");
inst1.set_subnode(alu_gio);
inst2.set_subnode(alu_gio);

// -----------------------------------------------------------
// 1. forward_class: only nodes in THIS graph
//    visits: inst1, inst2 (not alu_internal)
// -----------------------------------------------------------
for (auto node : top->forward_class()) {
  auto ci = node.get_class_index();  // OK — node within this graph
  // node.get_flat_index();          // runtime error: no cross-graph context
  // node.get_hier_index();          // runtime error: no hierarchy context
}

// -----------------------------------------------------------
// 2. forward_flat: this graph + all unique subgraphs (each once)
//    visits: inst1, inst2, alu_internal (alu body entered once)
// -----------------------------------------------------------
for (auto node : top->forward_flat()) {
  auto ci = node.get_class_index();  // OK
  auto fi = node.get_flat_index();   // OK — identifies (graph, node)
  // node.get_hier_index();          // runtime error: no hierarchy context
}

// -----------------------------------------------------------
// 3. forward_hier: full hierarchy (same graph body may appear
//    at multiple tree positions)
//    visits: inst1, alu_internal (via inst1), inst2, alu_internal (via inst2)
// -----------------------------------------------------------
for (auto node : top->forward_hier(hier_tree)) {
  auto ci = node.get_class_index();  // OK
  auto fi = node.get_flat_index();   // OK
  auto hi = node.get_hier_index();   // OK — includes tree position
}

// -----------------------------------------------------------
// Attributes work on Node from any traversal mode
// -----------------------------------------------------------
for (auto node : top->forward_class()) {
  if (node.attr(name).has()) {
    std::cout << node.attr(name).get() << "\n";
  }
}

// External maps use the appropriate index level
std::unordered_map<hhds::Class_index, int> per_graph_data;
std::unordered_map<hhds::Flat_index, int>  cross_graph_data;
std::unordered_map<hhds::Hier_index, int>  per_instance_data;

for (auto node : top->forward_hier(hier_tree)) {
  per_graph_data[node.get_class_index()]  = 1;  // same key for both alu instances
  cross_graph_data[node.get_flat_index()] = 2;  // same key for both alu instances
  per_instance_data[node.get_hier_index()] = 3; // different key per instance
}
```

### Example 3: Declaring custom attributes

The attribute tag declares its `value_type` and `storage` level. The compiler
resolves which map to use at compile time via `if constexpr` — zero runtime
branching.

```cpp
// hhds/attr.hpp — storage level markers
namespace hhds {
  struct flat_storage {};  // per-graph, map<Nid, T>
  struct hier_storage {};  // per-graph, map<(Tree_pos, Nid), T> — keyed by hierarchy position
}

// ---------------------------------------------------------------
// Built-in: name is flat (same name regardless of hierarchy view)
// ---------------------------------------------------------------
// hhds/attrs/name.hpp
namespace hhds::attrs {
struct name_t {
  using value_type = std::string;
  using storage    = hhds::flat_storage;
};
inline constexpr name_t name{};
}

// ---------------------------------------------------------------
// Downstream: cell_type is flat (gate type doesn't change per instance)
// ---------------------------------------------------------------
// livehd/attrs/cell_type.hpp
namespace livehd::attrs {
struct cell_type_t {
  using value_type = uint16_t;
  using storage    = hhds::flat_storage;
};
inline constexpr cell_type_t cell_type{};
}

// ---------------------------------------------------------------
// Downstream: bits is flat (bitwidth per node, same across hierarchy)
// ---------------------------------------------------------------
// livehd/attrs/bits.hpp
namespace livehd::attrs {
struct bits_t {
  using value_type = int;
  using storage    = hhds::flat_storage;
};
inline constexpr bits_t bits{};
}

// ---------------------------------------------------------------
// Downstream: hbits is hier (bitwidth can differ per hierarchy instance)
// ---------------------------------------------------------------
// livehd/attrs/hbits.hpp
namespace livehd::attrs {
struct hbits_t {
  using value_type = int;
  using storage    = hhds::hier_storage;
};
inline constexpr hbits_t hbits{};
}

// ---------------------------------------------------------------
// Usage — compiler picks the right map, zero runtime branching
// ---------------------------------------------------------------
using hhds::attrs::name;
using livehd::attrs::bits;
using livehd::attrs::hbits;
using livehd::attrs::cell_type;

// flat attrs work from any traversal
for (auto node : g->forward_class()) {
  node.attr(name).set("adder");           // flat: graph's map<Nid, string>
  node.attr(cell_type).set(0x0042);       // flat: graph's map<Nid, uint16_t>
  node.attr(bits).set(32);                // flat: graph's map<Nid, int>
}

// hier attrs require hier traversal (runtime error otherwise)
for (auto node : g->forward_hier(htree)) {
  node.attr(name).set("adder");           // flat: still works
  node.attr(bits).set(32);                // flat: still works
  node.attr(hbits).set(16);              // hier: graph's map<(Tree_pos,Nid), int>
}
```

### Example 4: Pins, pin attributes, edge iteration

Pins have the same `.attr()` API as nodes. `Nid` encodes pin-vs-node, so both
share the same storage maps.

```cpp
#include "hhds/graph.hpp"
#include "hhds/attrs/name.hpp"

// Downstream: timing delay per pin
namespace livehd::attrs {
struct delay_t {
  using value_type = float;
  using storage    = hhds::flat_storage;
};
inline constexpr delay_t delay{};
}

using hhds::attrs::name;
using livehd::attrs::delay;

auto glib = std::make_shared<hhds::GraphLibrary>("/tmp/test");

auto and_gio = glib->create_io("and");
and_gio->add_input("a", 0);
and_gio->add_output("y", 0);

auto top_gio = glib->create_io("top");
top_gio->add_input("x", 1);
top_gio->add_input("y", 2);
top_gio->add_output("z", 0);

auto g = top_gio->create_graph();

// Node linked to and_gio — pins resolve names through the declaration
auto and1 = g->create_node();
and1.set_subnode(and_gio);
and1.attr(name).set("and1");

// Create pins with direction — string form resolves through and_gio
auto and1_in  = and1.create_sink_pin("a");    // or and1.create_sink_pin() since single input
auto and1_out = and1.create_driver_pin("y");   // or and1.create_driver_pin() since single output

// get vs create — get assumes the pin was already created
auto and1_in_again  = and1.get_sink_pin("a");  // OK: was created above
auto and1_out_again = and1.get_driver_pin("y"); // OK: was created above
// and1.get_sink_pin("b");                      // ERROR: "b" was never created

// get_pin_name returns the port name from the GraphIO declaration
assert(and1_in.get_pin_name() == "a");
assert(and1_out.get_pin_name() == "y");

// Pin attributes — same .attr() API as Node
and1_in.attr(delay).set(1.5f);
and1_out.attr(delay).set(0.3f);
assert(and1_in.attr(delay).get() == 1.5f);

// Connect: graph IO pins ↔ internal pins
auto g_x = g->get_input_pin("x");
auto g_y = g->get_input_pin("y");
auto g_z = g->get_output_pin("z");

// Commutative gate: AND has a single input port "a" — multiple drivers
// fan into the same sink pin (like LiveHD commutative gate model)
and1_in.connect_driver(g_x);    // x → and1.a
and1_in.connect_driver(g_y);    // y → and1.a
and1_out.connect_sink(g_z);     // and1.y → z

// -----------------------------------------------------------
// Edge iteration — from a node
// -----------------------------------------------------------
for (auto edge : and1.inp_edges()) {
  auto dp = edge.driver_pin();   // Pin with .attr() support
  auto sp = edge.sink_pin();     // Pin with .attr() support

  std::cout << dp.get_pin_name() << " → " << sp.get_pin_name() << "\n";
  // prints: "x → a" and "y → a"
}

for (auto edge : and1.out_edges()) {
  auto dp = edge.driver_pin();
  auto sp = edge.sink_pin();

  std::cout << dp.get_pin_name() << " → " << sp.get_pin_name() << "\n";
  // prints: "y → z"
}

// -----------------------------------------------------------
// Pin iteration — enumerate pins on a node directly
// -----------------------------------------------------------
for (auto pin : and1.inp_pins()) {
  std::cout << pin.get_pin_name();
  if (pin.attr(delay).has()) {
    std::cout << " delay=" << pin.attr(delay).get();
  }
  std::cout << "\n";
  // prints: "a delay=1.5"
}

for (auto pin : and1.out_pins()) {
  std::cout << pin.get_pin_name();
  if (pin.attr(delay).has()) {
    std::cout << " delay=" << pin.attr(delay).get();
  }
  std::cout << "\n";
  // prints: "y delay=0.3"
}

// -----------------------------------------------------------
// Edge iteration during traversal — pins inherit Node context
// -----------------------------------------------------------
for (auto node : g->forward_class()) {
  for (auto edge : node.inp_edges()) {
    auto dp = edge.driver_pin();
    auto sp = edge.sink_pin();

    // Both pins have the same context level as the node
    auto di = dp.get_class_index();
    auto si = sp.get_class_index();
  }
}
```

### Example 5: Graph persistence, load/save, clear, print

Attributes are saved and loaded alongside declarations and bodies.

```cpp
#include "hhds/graph.hpp"
#include "hhds/attrs/name.hpp"

using hhds::attrs::name;

namespace livehd::attrs {
struct bits_t {
  using value_type = int;
  using storage    = hhds::flat_storage;
};
inline constexpr bits_t bits{};
}
using livehd::attrs::bits;

// -----------------------------------------------------------
// Session 1: create, populate, save
// -----------------------------------------------------------
{
  auto glib = std::make_shared<hhds::GraphLibrary>("/tmp/mydb");

  auto gio = glib->create_io("alu");
  gio->add_input("a", 1);
  gio->add_input("b", 2);
  gio->add_output("y", 0);

  auto g = gio->create_graph();

  auto add = g->create_node();
  add.attr(name).set("adder");
  add.attr(bits).set(32);

  auto sub = g->create_node();
  sub.attr(name).set("subtractor");
  sub.attr(bits).set(16);

  add.create_driver_pin().connect_sink(g->get_output_pin("y"));

  // Print — built-in name attribute shown automatically
  g->print(std::cout);

  // Save declarations, bodies, and all attribute maps to disk
  glib->save();
}

// -----------------------------------------------------------
// Session 2: load and query
// -----------------------------------------------------------
{
  auto glib = std::make_shared<hhds::GraphLibrary>("/tmp/mydb");
  // Declarations loaded eagerly on construction

  auto gio = glib->find_io("alu");
  assert(gio != nullptr);
  assert(gio->has_input("a"));
  assert(gio->get_name() == "alu");

  // GraphIO → Graph (lazy load from disk, including attribute maps)
  auto g = gio->get_graph();
  assert(g != nullptr);

  // Graph → GraphIO (navigate back)
  assert(g->get_io()->get_name() == "alu");

  // Attributes survived the round-trip
  for (auto node : g->forward_class()) {
    assert(node.attr(name).has());
    std::cout << node.attr(name).get()
              << " bits=" << node.attr(bits).get() << "\n";
  }
  // prints: "adder bits=32" and "subtractor bits=16"

  g->print(std::cout);  // same output as session 1
}

// -----------------------------------------------------------
// Session 3: attr_clear and clear semantics
// -----------------------------------------------------------
{
  auto glib = std::make_shared<hhds::GraphLibrary>("/tmp/mydb");
  auto gio = glib->find_io("alu");
  auto g   = gio->get_graph();

  // attr_clear works like std::map::clear() — empties entries, map stays
  g->attr_clear(bits);
  assert(g->has_attr(bits));                       // map still registered
  for (auto node : g->forward_class()) {
    assert(!node.attr(bits).has());                // entries gone
  }
  // Can repopulate immediately
  for (auto node : g->forward_class()) {
    node.attr(bits).set(64);
  }

  // Clear the graph body + ALL associated attribute maps
  g->clear();
  // g is gone, but gio still valid:
  assert(gio->has_input("a"));    // declaration IO survives
  assert(!gio->has_graph());      // body is gone

  // Rebuild from scratch on the same declaration
  auto g2 = gio->create_graph();  // fresh body, IO auto-materialized
  auto n  = g2->create_node();
  n.attr(name).set("new_adder");

  // Clear the declaration entirely — body, IO, attributes, everything
  gio->clear();
  // tombstone remains internal
  assert(glib->find_io("alu") == nullptr);

  glib->save();
}
```

### Example 6: Deleting edges and nodes

Fine-grained deletion — edges, nodes, and subtrees. Pins cannot be deleted
individually; they live and die with their node.

```cpp
#include "hhds/graph.hpp"
#include "hhds/attrs/name.hpp"

using hhds::attrs::name;

auto glib = std::make_shared<hhds::GraphLibrary>("/tmp/test");

auto and_gio = glib->create_io("and");
and_gio->add_input("a", 0);
and_gio->add_output("y", 0);

auto or_gio = glib->create_io("or");
or_gio->add_input("a", 0);
or_gio->add_output("y", 0);

auto xor_gio = glib->create_io("xor");
xor_gio->add_input("a", 0);
xor_gio->add_output("y", 0);

auto top_gio = glib->create_io("top");
top_gio->add_input("a", 1);
top_gio->add_input("b", 2);
top_gio->add_input("c", 3);
top_gio->add_output("y1", 1);
top_gio->add_output("y2", 2);
auto g = top_gio->create_graph();

//   a ──┐
//       ├──→ [AND].a ──→ [AND].y ──┬──→ [OR].a  ──→ [OR].y  ──→ y1
//   b ──┘                          │
//                                  └──→ [XOR].a
//                            c ────────→ [XOR].a ──→ [XOR].y ──→ y2

auto and1 = g->create_node();  and1.set_subnode(and_gio);  and1.attr(name).set("and1");
auto or1  = g->create_node();  or1.set_subnode(or_gio);    or1.attr(name).set("or1");
auto xor1 = g->create_node();  xor1.set_subnode(xor_gio);  xor1.attr(name).set("xor1");

auto and1_in  = and1.create_sink_pin("a");
auto and1_out = and1.create_driver_pin("y");
auto or1_in   = or1.create_sink_pin("a");
auto or1_out  = or1.create_driver_pin("y");
auto xor1_in  = xor1.create_sink_pin("a");
auto xor1_out = xor1.create_driver_pin("y");

auto g_a  = g->get_input_pin("a");
auto g_b  = g->get_input_pin("b");
auto g_c  = g->get_input_pin("c");
auto g_y1 = g->get_output_pin("y1");
auto g_y2 = g->get_output_pin("y2");

// AND inputs: a and b both drive the single commutative input "a"
and1_in.connect_driver(g_a);
and1_in.connect_driver(g_b);

// AND output fans out to OR and XOR
and1_out.connect_sink(or1_in);
and1_out.connect_sink(xor1_in);

// XOR also driven by c (two drivers into XOR's "a": AND.y and c)
xor1_in.connect_driver(g_c);

// Outputs
or1_out.connect_sink(g_y1);
xor1_out.connect_sink(g_y2);

// -----------------------------------------------------------
// State check: AND.y drives two sinks (OR, XOR)
//              XOR.a has two drivers (AND.y, c)
// -----------------------------------------------------------
assert(and1_out.out_edges().size() == 2);  // AND.y → OR.a, AND.y → XOR.a
assert(xor1_in.inp_edges().size() == 2);   // AND.y → XOR.a, c → XOR.a

// -----------------------------------------------------------
// del_sink(specific_driver): remove ONE edge into a sink
//   XOR.a still keeps its other driver (c)
// -----------------------------------------------------------
xor1_in.del_sink(and1_out);                // remove only AND.y → XOR.a

assert(xor1_in.inp_edges().size() == 1);   // only c → XOR.a remains
assert(and1_out.out_edges().size() == 1);   // only AND.y → OR.a remains
assert(xor1.is_valid());                    // node survives
assert(xor1_in.is_valid());                 // pin survives

// -----------------------------------------------------------
// del_sink(): remove ALL edges into a sink
//   AND.a loses both its drivers (a, b)
// -----------------------------------------------------------
assert(and1_in.inp_edges().size() == 2);    // a → AND.a, b → AND.a
and1_in.del_sink();                         // remove all edges into AND.a

assert(and1_in.inp_edges().size() == 0);    // both drivers gone
assert(and1.is_valid());                    // node survives
assert(and1_in.is_valid());                 // pin survives

// -----------------------------------------------------------
// del_driver(): remove ALL edges from a driver
//   AND.y's remaining edge to OR is removed
// -----------------------------------------------------------
assert(and1_out.out_edges().size() == 1);   // AND.y → OR.a
and1_out.del_driver();                      // remove all edges from AND.y

assert(and1_out.out_edges().size() == 0);   // no more edges
assert(or1_in.inp_edges().size() == 0);     // OR.a lost its driver
assert(and1.is_valid());                    // node survives
assert(or1.is_valid());                     // node survives

// -----------------------------------------------------------
// del_node(): remove node + all pins + all connected edges
//   XOR still has c → XOR.a and XOR.y → y2
// -----------------------------------------------------------
assert(xor1.is_valid());
xor1.del_node();

assert(xor1.is_invalid());                 // tombstoned
assert(xor1_in.is_invalid());              // pins gone
assert(xor1_out.is_invalid());
assert(!xor1.attr(name).has());            // attributes gone

// c and y2 graph IO pins survive (they belong to the graph, not to xor1)
assert(g_c.is_valid());
assert(g_y2.is_valid());

// Iterators skip tombstoned nodes
for (auto node : g->forward_class()) {
  assert(node.is_valid());
  // visits: and1, or1 — xor1 is skipped
}
```

Delete API summary:

| Method | Removes | Survives |
|--------|---------|----------|
| `sink.del_sink(driver)` | One specific edge into the sink | Both pins, both nodes, other edges |
| `sink.del_sink()` | All edges into the sink | Pin, node, other pins' edges |
| `driver.del_driver()` | All edges from the driver | Pin, node, other pins' edges |
| `node.del_node()` | Node + all pins + all connected edges | Everything else; tombstone preserved |

### Example 7: Tree node deletion

Tree deletion removes the entire subtree rooted at the deleted node.

```cpp
#include "hhds/tree.hpp"
#include "hhds/attrs/name.hpp"

using hhds::attrs::name;

auto forest = std::make_shared<hhds::Forest>("/tmp/trees");
auto tio = forest->create_io("ast");
auto t   = tio->create_tree();

auto root   = t->get_root();       root.attr(name).set("program");
auto func   = root.add_child();    func.attr(name).set("func");
auto body   = func.add_child();    body.attr(name).set("body");
auto ret    = func.add_child();    ret.attr(name).set("return");
auto expr   = root.add_child();    expr.attr(name).set("expr");

// Delete a subtree — func and all its children are removed
func.del_node();

assert(func.is_invalid());
assert(body.is_invalid());     // children gone too
assert(ret.is_invalid());

// Siblings and parent survive
assert(root.is_valid());
assert(expr.is_valid());

// Attributes on deleted subtree are gone
assert(!func.attr(name).has());
assert(!body.attr(name).has());

// Iteration skips deleted nodes
for (auto node : root.pre_order_class()) {
  assert(node.is_valid());
  // visits: program, expr — func subtree is skipped
}

for (auto node : expr.sibling_order()) {
  // visits: expr only — func is gone
}
```

---

## Tree Examples

### Example 6: Tree basics, attributes, traversal

```cpp
#include "hhds/tree.hpp"
#include "hhds/attrs/name.hpp"

auto forest = std::make_shared<hhds::Forest>("/tmp/trees");

// Declare a tree (name is required and immutable)
auto tio = forest->create_io("parser");

// Create the implementation body from the declaration
auto t = tio->create_tree();

// Build structure — get_root and add_child return Node
using hhds::attrs::name;
auto root = t->get_root();
root.attr(name).set("program");
root.set_type(1);

auto lhs = root.add_child();
lhs.attr(name).set("assign");

auto rhs = root.add_child();
rhs.attr(name).set("expr");

auto leaf = lhs.add_child();
leaf.attr(name).set("literal");

// All traversals start from a Node
for (auto node : root.pre_order_class()) {
  std::cout << node.attr(name).get() << "\n";
}
// prints: program, assign, literal, expr

// Can start from any subtree
for (auto node : lhs.pre_order_class()) {
  std::cout << node.attr(name).get() << "\n";
}
// prints: assign, literal

// Post-order, sibling — same pattern
for (auto node : root.post_order_class()) {
  std::cout << node.attr(name).get() << "\n";
}
// prints: literal, assign, expr, program

for (auto node : lhs.sibling_order()) {
  std::cout << node.attr(name).get() << "\n";
}
// prints: assign, expr

// Empty tree — get_root() returns invalid Node, iteration is a no-op
auto t2 = forest->create_io("empty")->create_tree();
for (auto node : t2->get_root().pre_order_class()) {
  // never entered
}
```

### Example 7: Forest hierarchy, persistence, clear

```cpp
#include "hhds/tree.hpp"
#include "hhds/attrs/name.hpp"

using hhds::attrs::name;

// Downstream: source location per tree node
namespace frontend::attrs {
struct loc_t {
  using value_type = uint32_t;   // line number
  using storage    = hhds::flat_storage;
};
inline constexpr loc_t loc{};
}
using frontend::attrs::loc;

// -----------------------------------------------------------
// Session 1: create, populate, save
// -----------------------------------------------------------
{
  auto forest = std::make_shared<hhds::Forest>("/tmp/ast_db");

  // Declare two trees — a function body and a reusable expression pattern
  auto expr_tio = forest->create_io("common_expr");
  auto func_tio = forest->create_io("main_func");

  // Build the reusable expression subtree
  auto expr = expr_tio->create_tree();
  auto eroot = expr->get_root();
  eroot.attr(name).set("add");
  eroot.attr(loc).set(10);

  auto elhs = eroot.add_child();
  elhs.attr(name).set("var_a");
  elhs.attr(loc).set(10);

  auto erhs = eroot.add_child();
  erhs.attr(name).set("var_b");
  erhs.attr(loc).set(10);

  // Build the function body — references the expression via set_subnode
  auto func = func_tio->create_tree();
  auto froot = func->get_root();
  froot.attr(name).set("func_main");
  froot.attr(loc).set(1);

  auto assign = froot.add_child();
  assign.attr(name).set("assign");
  assign.attr(loc).set(10);

  // Cross-tree reference — same API as graph's set_subnode
  auto call = froot.add_child();
  call.set_subnode(expr_tio);
  call.attr(name).set("inline_expr");
  call.attr(loc).set(20);

  // Traversal within this tree only
  for (auto node : froot.pre_order_class()) {
    std::cout << node.attr(name).get()
              << " L" << node.attr(loc).get() << "\n";
  }
  // prints: func_main L1, assign L10, inline_expr L20

  // Hierarchical traversal — enters the expression subtree
  for (auto node : froot.pre_order_hier(hier_tree)) {
    std::cout << node.attr(name).get()
              << " L" << node.attr(loc).get() << "\n";
  }
  // prints: func_main L1, assign L10, inline_expr L20, add L10, var_a L10, var_b L10

  func->print(std::cout);

  // Save everything — declarations, bodies, attributes
  forest->save();
}

// -----------------------------------------------------------
// Session 2: load, query, navigate
// -----------------------------------------------------------
{
  auto forest = std::make_shared<hhds::Forest>("/tmp/ast_db");

  // Declarations loaded eagerly
  auto func_tio = forest->find_io("main_func");
  auto expr_tio = forest->find_io("common_expr");
  assert(func_tio != nullptr);
  assert(expr_tio != nullptr);

  // TreeIO → Tree (lazy load, including attribute maps)
  auto func = func_tio->get_tree();
  assert(func != nullptr);

  // Tree → TreeIO (navigate back)
  assert(func->get_io()->get_name() == "main_func");

  // Attributes survived the round-trip
  auto froot = func->get_root();
  assert(froot.attr(name).get() == "func_main");
  assert(froot.attr(loc).get() == 1);

  // Expression subtree also loaded
  auto expr = expr_tio->get_tree();
  auto eroot = expr->get_root();
  assert(eroot.attr(name).get() == "add");

  func->print(std::cout);
}

// -----------------------------------------------------------
// Session 3: clear semantics
// -----------------------------------------------------------
{
  auto forest = std::make_shared<hhds::Forest>("/tmp/ast_db");
  auto expr_tio = forest->find_io("common_expr");
  auto expr = expr_tio->get_tree();

  // attr_clear — empties entries, map stays registered
  expr->attr_clear(loc);
  assert(expr->has_attr(loc));
  for (auto node : expr->get_root().pre_order_class()) {
    assert(!node.attr(loc).has());
  }

  // Clear tree body + all attribute maps
  expr->clear();
  assert(!expr_tio->has_tree());                      // body gone
  assert(expr_tio->get_name() == "common_expr");      // declaration survives

  // Rebuild on same declaration
  auto expr2 = expr_tio->create_tree();
  auto r = expr2->get_root();
  r.attr(name).set("mul");

  // Clear declaration entirely
  expr_tio->clear();
  assert(forest->find_io("common_expr") == nullptr);

  forest->save();
}
```
