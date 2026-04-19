// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <iostream>
#include <memory>
#include <string>

#include "absl/container/flat_hash_map.h"
#include "tree.hpp"

int main() {
  auto                                                     tree = hhds::Tree::create();
  absl::flat_hash_map<hhds::Tree::Node_class, std::string> names;

  auto root   = tree->add_root_node();
  auto lhs    = root.add_child();
  auto rhs    = root.add_child();
  auto nested = lhs.add_child();

  names[root]   = "add";
  names[lhs]    = "lhs";
  names[rhs]    = "rhs";
  names[nested] = "literal";

  root.set_type(1);
  nested.set_type(2);

  std::cout << "Pre-order traversal\n";
  for (auto node : tree->pre_order()) {
    std::cout << "  pos=" << node.get_debug_nid() << " name=" << names[node] << " type=" << node.get_type() << "\n";
  }

  std::cout << "\nSibling order under root\n";
  for (auto node : lhs.sibling_order()) {
    std::cout << "  pos=" << node.get_debug_nid() << " name=" << names[node] << "\n";
  }

  hhds::Tree::PrintOptions print_options;
  const hhds::Type_entry   type_table[] = {
      {"unknown", hhds::Statement_class::Node},
      {"add", hhds::Statement_class::Node},
      {"literal", hhds::Statement_class::Node},
  };
  print_options.type_table = type_table;
  print_options.node_text  = [&names](const hhds::Tree::Node_class& node) {
    auto it = names.find(node);
    return it == names.end() ? std::string("node") : it->second;
  };
  print_options.attributes = {
      {"type_id",
       [](const hhds::Tree::Node_class& node) -> std::optional<std::string> { return std::to_string(node.get_type()); }},
  };

  std::cout << "\nLLVM-like tree print\n";
  tree->print(std::cout, print_options);

  auto       forest  = hhds::Forest::create();
  auto       top_tio = forest->create_io("top");
  auto       sub_tio = forest->create_io("sub");
  auto       top     = top_tio->create_tree();
  auto       sub     = sub_tio->create_tree();
  const auto top_tid = top_tio->get_tid();
  const auto sub_tid = sub_tio->get_tid();

  auto top_root = top->add_root_node();
  auto callsite = top_root.add_child();
  auto sub_root = sub->add_root_node();
  auto sub_leaf = sub_root.add_child();

  callsite.set_subnode(sub_tio);

  std::cout << "\nForest subnode references\n";
  std::cout << "  top tree id=" << top_tid << " root pos=" << top_root.get_debug_nid() << "\n";
  std::cout << "  callsite pos=" << callsite.get_debug_nid() << " subnode=" << top->get_subnode(callsite) << "\n";
  std::cout << "  sub tree id=" << sub_tid << " root pos=" << sub_root.get_debug_nid()
            << " leaf pos=" << sub_leaf.get_debug_nid() << "\n";

  auto single_cursor = tree->create_cursor(root);
  std::cout << "\nTree cursor example\n";
  std::cout << "  start: pos=" << single_cursor.get_current_pos() << " depth=" << single_cursor.depth() << "\n";
  single_cursor.goto_first_child();
  std::cout << "  first child: pos=" << single_cursor.get_current_pos() << " depth=" << single_cursor.depth() << "\n";
  single_cursor.goto_first_child();
  std::cout << "  nested child: pos=" << single_cursor.get_current_pos() << " depth=" << single_cursor.depth() << "\n";
  single_cursor.goto_parent();
  single_cursor.goto_next_sibling();
  std::cout << "  next sibling: pos=" << single_cursor.get_current_pos() << " depth=" << single_cursor.depth() << "\n";

  auto hier_cursor = forest->create_cursor(top_tid);
  hier_cursor.goto_first_child();
  hier_cursor.goto_first_child();
  std::cout << "Forest cursor example: current_tid=" << hier_cursor.get_current_tid()
            << " current_pos=" << hier_cursor.get_current_pos() << " depth=" << hier_cursor.depth() << "\n";

  return 0;
}
