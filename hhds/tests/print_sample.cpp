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
    {"add",     hhds::Statement_class::Node},
    {"literal", hhds::Statement_class::Node},
    {"value",   hhds::Statement_class::Node},
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
    {"pos", [](const hhds::Tree::Node_class& node) -> std::optional<std::string> {
      return std::to_string(node.get_current_pos());
    }},
    {"type_id", [&tree](const hhds::Tree::Node_class& node) -> std::optional<std::string> {
      return std::to_string(tree->get_type(node));
    }},
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
    {"unknown",  hhds::Statement_class::Node},
    {"add",      hhds::Statement_class::Node},
    {"if_taken", hhds::Statement_class::Open_call},
    {"sub",      hhds::Statement_class::Node},
  };

  std::cout << "\nPrint with scope types\n";
  hhds::Tree::PrintOptions scope_opts;
  scope_opts.type_table = scope_types;
  tree2->print(std::cout, scope_opts);

  return 0;
}
