// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <iostream>
#include <memory>
#include <string>

#include "absl/container/flat_hash_map.h"
#include "tree.hpp"

int main() {
  auto tree = hhds::Tree::create();
  tree->set_name("expr");

  const auto root   = tree->add_root_node();
  const auto lhs    = tree->add_child(root);
  const auto rhs    = tree->add_child(root);
  const auto nested = tree->add_child(lhs);

  tree->set_type(root, 1);
  tree->set_type(lhs, 3);
  tree->set_type(rhs, 3);
  tree->set_type(nested, 2);

  const hhds::Type_entry type_table[] = {
      {"unknown", hhds::Statement_class::Node},
      {"add", hhds::Statement_class::Node},
      {"literal", hhds::Statement_class::Node},
      {"value", hhds::Statement_class::Node},
  };

  std::cout << "Print with type table only\n";
  hhds::Tree::PrintOptions type_only;
  type_only.type_table = type_table;
  tree->print(std::cout, type_only);

  absl::flat_hash_map<hhds::Tree::Node_class, std::string> names;
  names[root]   = "add";
  names[lhs]    = "%lhs";
  names[rhs]    = "%rhs";
  names[nested] = "42";

  std::cout << "\nPrint with names and attributes\n";
  hhds::Tree::PrintOptions with_attrs;
  with_attrs.type_table = type_table;
  with_attrs.node_text  = [&names](const hhds::Tree::Node_class& node) {
    auto it = names.find(node);
    return it == names.end() ? std::string("node") : it->second;
  };
  with_attrs.attributes = {
      {"pos",
       [](const hhds::Tree::Node_class& node) -> std::optional<std::string> { return std::to_string(node.get_current_pos()); }},
      {"type_id",
       [&tree](const hhds::Tree::Node_class& node) -> std::optional<std::string> { return std::to_string(tree->get_type(node)); }},
  };
  tree->print(std::cout, with_attrs);

  std::cout << "\nPrint as string\n";
  std::cout << tree->print(with_attrs);

  // Demonstrate scope types
  auto tree2 = hhds::Tree::create();
  tree2->set_name("scoped");

  const auto r2  = tree2->add_root_node();
  const auto c2a = tree2->add_child(r2);
  const auto c2b = tree2->add_child(r2);
  const auto g2  = tree2->add_child(c2a);

  tree2->set_type(r2, 1);
  tree2->set_type(c2a, 2);
  tree2->set_type(c2b, 1);
  tree2->set_type(g2, 3);

  const hhds::Type_entry scope_types[] = {
      {"unknown", hhds::Statement_class::Node},
      {"add", hhds::Statement_class::Node},
      {"if_taken", hhds::Statement_class::Open_call},
      {"sub", hhds::Statement_class::Node},
  };

  std::cout << "\nPrint with scope types\n";
  hhds::Tree::PrintOptions scope_opts;
  scope_opts.type_table = scope_types;
  tree2->print(std::cout, scope_opts);

  // Demonstrate dump (tree-command style)
  std::cout << "\nDump with type table only\n";
  tree->dump(std::cout, type_only);

  std::cout << "\nDump with names and attributes\n";
  tree->dump(std::cout, with_attrs);

  std::cout << "\nDump scoped tree\n";
  tree2->dump(std::cout, scope_opts);

  // Demonstrate custom format_node callback (LNAST-style)
  auto tree3 = hhds::Tree::create();
  tree3->set_name("lnast_demo");

  // Build: plus(dest, a, b) and if(cond, body_stmt)
  const auto r3      = tree3->add_root_node();  // top-level stmts
  const auto plus_n  = tree3->add_child(r3);
  const auto dest    = tree3->add_child(plus_n);
  const auto arg_a   = tree3->add_child(plus_n);
  const auto arg_b   = tree3->add_child(plus_n);
  const auto if_n    = tree3->add_child(r3);
  const auto cond    = tree3->add_child(if_n);
  const auto body_st = tree3->add_child(if_n);

  // Type IDs: 0=stmts, 1=plus, 2=if, 3=ref
  tree3->set_type(r3, 0);
  tree3->set_type(plus_n, 1);
  tree3->set_type(dest, 3);
  tree3->set_type(arg_a, 3);
  tree3->set_type(arg_b, 3);
  tree3->set_type(if_n, 2);
  tree3->set_type(cond, 3);
  tree3->set_type(body_st, 3);

  const hhds::Type_entry lnast_types[] = {
      {"stmts", hhds::Statement_class::Node},
      {"plus", hhds::Statement_class::Node},
      {"if", hhds::Statement_class::Node},
      {"ref", hhds::Statement_class::Node},
  };

  absl::flat_hash_map<hhds::Tree_pos, std::string> ref_names;
  ref_names[r3.get_current_pos()]      = "top";
  ref_names[dest.get_current_pos()]    = "result";
  ref_names[arg_a.get_current_pos()]   = "a";
  ref_names[arg_b.get_current_pos()]   = "b";
  ref_names[cond.get_current_pos()]    = "flag";
  ref_names[body_st.get_current_pos()] = "do_something";

  std::cout << "\nDefault print of LNAST tree\n";
  hhds::Tree::PrintOptions lnast_default;
  lnast_default.type_table = lnast_types;
  lnast_default.node_text  = [&ref_names](const hhds::Tree::Node_class& node) {
    auto it = ref_names.find(node.get_current_pos());
    return it == ref_names.end() ? std::string("?") : it->second;
  };
  tree3->print(std::cout, lnast_default);

  std::cout << "\nCustom LNAST-style format_node\n";
  hhds::Tree::PrintOptions lnast_custom;
  lnast_custom.type_table = lnast_types;
  lnast_custom.node_text  = lnast_default.node_text;

  auto get_ref = [&ref_names](hhds::Tree_pos pos) -> std::string {
    auto it = ref_names.find(pos);
    return it == ref_names.end() ? "?" : it->second;
  };

  lnast_custom.format_node = [&](std::ostream& os, hhds::Tree_pos pos, const hhds::Tree::PrintContext& ctx) -> bool {
    auto type     = ctx.tree.get_type(pos);
    auto children = ctx.get_children();

    if (type == 1) {  // plus
      ctx.emit_indent(os);
      os << get_ref(children[0]) << " = add(";
      for (size_t i = 1; i < children.size(); ++i) {
        if (i > 1) {
          os << ", ";
        }
        os << get_ref(children[i]);
      }
      os << ")\n";
      return true;
    }

    if (type == 2) {  // if
      ctx.emit_indent(os);
      os << "if (" << get_ref(children[0]) << ") {\n";
      for (size_t i = 1; i < children.size(); ++i) {
        ctx.print_child_default(os, children[i]);
      }
      ctx.emit_indent(os);
      os << "}\n";
      return true;
    }

    return false;  // fall back to default formatting
  };
  tree3->print(std::cout, lnast_custom);

  std::cout << "\nDump scoped tree for LNAST tree\n";
  tree3->dump(std::cout, lnast_default);

  // Demonstrate write_dump / read_dump round-trip
  std::cout << "\n--- write_dump / read_dump round-trip ---\n";

  // Write tree to file
  const std::string filename = "/tmp/test_tree.hhds";
  tree->write_dump(filename, with_attrs);
  std::cout << "Wrote tree to " << filename << "\n";

  // Read it back
  auto [loaded_tree, loaded_nodes] = hhds::Tree::read_dump(filename, type_table);

  std::cout << "\nLoaded tree dump:\n";
  hhds::Tree::PrintOptions loaded_opts;
  loaded_opts.type_table = type_table;

  // Build node_text from loaded NodeData
  absl::flat_hash_map<hhds::Tree_pos, std::string> loaded_names;
  for (const auto& nd : loaded_nodes) {
    loaded_names[nd.pos] = nd.node_text;
  }
  loaded_opts.node_text = [&loaded_names](const hhds::Tree::Node_class& node) {
    auto it = loaded_names.find(node.get_current_pos());
    return it == loaded_names.end() ? std::string("?") : it->second;
  };

  // Rebuild attributes from loaded NodeData
  absl::flat_hash_map<hhds::Tree_pos, std::vector<std::pair<std::string, std::string>>> loaded_attrs;
  for (const auto& nd : loaded_nodes) {
    if (!nd.attributes.empty()) {
      loaded_attrs[nd.pos] = nd.attributes;
    }
  }
  loaded_opts.attributes = {
      {"pos",
       [&loaded_attrs](const hhds::Tree::Node_class& node) -> std::optional<std::string> {
         auto it = loaded_attrs.find(node.get_current_pos());
         if (it != loaded_attrs.end()) {
           for (const auto& [k, v] : it->second) {
             if (k == "pos") {
               return v;
             }
           }
         }
         return std::nullopt;
       }},
      {"type_id",
       [&loaded_attrs](const hhds::Tree::Node_class& node) -> std::optional<std::string> {
         auto it = loaded_attrs.find(node.get_current_pos());
         if (it != loaded_attrs.end()) {
           for (const auto& [k, v] : it->second) {
             if (k == "type_id") {
               return v;
             }
           }
         }
         return std::nullopt;
       }},
  };
  loaded_tree->dump(std::cout, loaded_opts);

  return 0;
}
