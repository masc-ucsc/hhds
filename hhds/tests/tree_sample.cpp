// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <iostream>
#include <memory>
#include <string>

#include "absl/container/flat_hash_map.h"
#include "tree.hpp"

int main() {
  auto tree = hhds::Tree::create();
  absl::flat_hash_map<hhds::Tree::Node_class, std::string> names;

  const auto root   = tree->add_root_node();
  const auto lhs    = tree->add_child(root);
  const auto rhs    = tree->add_child(root);
  const auto nested = tree->add_child(lhs);

  names[root]   = "add";
  names[lhs]    = "lhs";
  names[rhs]    = "rhs";
  names[nested] = "literal";

  tree->set_type(root, 1);
  tree->set_type(nested, 2);

  std::cout << "Pre-order traversal\n";
  for (auto node : tree->pre_order()) {
    std::cout << "  pos=" << node.get_current_pos() << " name=" << names[node] << " type=" << tree->get_type(node) << "\n";
  }

  std::cout << "\nSibling order under root\n";
  for (auto node : tree->sibling_order(lhs)) {
    std::cout << "  pos=" << node.get_current_pos() << " name=" << names[node] << "\n";
  }

  hhds::Tree::PrintOptions print_options;
  const hhds::Type_entry   type_table[] = {
    {"unknown", hhds::Statement_class::Node},
    {"add",     hhds::Statement_class::Node},
    {"literal", hhds::Statement_class::Node},
  };
  print_options.type_table = type_table;
  print_options.node_text  = [&names](const hhds::Tree::Node_class& node) {
    auto it = names.find(node);
    return it == names.end() ? std::string("node") : it->second;
  };
  print_options.attributes = {
    {"type_id", [&tree](const hhds::Tree::Node_class& node) -> std::optional<std::string> {
      return std::to_string(tree->get_type(node));
    }},
  };

  std::cout << "\nLLVM-like tree print\n";
  tree->print(std::cout, print_options);

  auto forest = hhds::Forest::create();
  const auto top_tid = forest->create_tree();
  const auto sub_tid = forest->create_tree();

  auto& top = forest->get_tree(top_tid);
  auto& sub = forest->get_tree(sub_tid);

  const auto top_root = top.add_root_node();
  const auto callsite = top.add_child(top_root);
  const auto sub_root = sub.add_root_node();
  const auto sub_leaf = sub.add_child(sub_root);

  top.set_subnode(callsite, sub_tid);

  std::cout << "\nForest subnode references\n";
  std::cout << "  top tree id=" << top_tid << " root pos=" << top_root.get_current_pos() << "\n";
  std::cout << "  callsite pos=" << callsite.get_current_pos() << " subnode=" << top.get_subnode(callsite) << "\n";
  std::cout << "  sub tree id=" << sub_tid << " root pos=" << sub_root.get_current_pos() << " leaf pos=" << sub_leaf.get_current_pos()
            << "\n";

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
  std::cout << "Forest cursor example: current_tid=" << hier_cursor.get_current_tid() << " current_pos=" << hier_cursor.get_current_pos()
            << " depth=" << hier_cursor.depth() << "\n";

  const auto flat = tree->as_flat(lhs.get_current_pos(), /*current_tid=*/17, /*root_tid=*/11);
  std::cout << "\nFlat wrapper example: root_tid=" << flat.get_root_tid() << " current_tid=" << flat.get_current_tid()
            << " current_pos=" << flat.get_current_pos() << "\n";

  return 0;
}
