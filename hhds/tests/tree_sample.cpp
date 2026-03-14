// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <iostream>
#include <string>

#include "absl/container/flat_hash_map.h"
#include "tree.hpp"

int main() {
  hhds::Tree tree;
  absl::flat_hash_map<hhds::Tree::Node_class, std::string> names;

  const auto root   = tree.add_root_node();
  const auto lhs    = tree.add_child(root);
  const auto rhs    = tree.add_child(root);
  const auto nested = tree.add_child(lhs);

  names[root]   = "add";
  names[lhs]    = "lhs";
  names[rhs]    = "rhs";
  names[nested] = "literal";

  tree.set_type(root, 1);
  tree.set_type(nested, 2);

  std::cout << "Pre-order traversal\n";
  for (auto node : tree.pre_order()) {
    std::cout << "  tid=" << node.get_raw_tid() << " name=" << names[node] << " type=" << tree.get_type(node) << "\n";
  }

  std::cout << "\nSibling order under root\n";
  for (auto node : tree.sibling_order(lhs)) {
    std::cout << "  tid=" << node.get_raw_tid() << " name=" << names[node] << "\n";
  }

  hhds::Forest forest;
  const auto   top_tid = forest.create_tree();
  const auto   sub_tid = forest.create_tree();

  auto& top = forest.get_tree(top_tid);
  auto& sub = forest.get_tree(sub_tid);

  const auto top_root = top.add_root_node();
  const auto callsite = top.add_child(top_root);
  const auto sub_root = sub.add_root_node();
  const auto sub_leaf = sub.add_child(sub_root);

  top.add_subtree_ref(callsite, sub_tid);

  std::cout << "\nForest subtree references\n";
  std::cout << "  top tree id=" << top_tid << " root tid=" << top_root.get_raw_tid() << "\n";
  std::cout << "  callsite tid=" << callsite.get_raw_tid() << " subtree_ref=" << top.get_subtree_ref(callsite) << "\n";
  std::cout << "  sub tree id=" << sub_tid << " root tid=" << sub_root.get_raw_tid() << " leaf tid=" << sub_leaf.get_raw_tid()
            << "\n";

  const auto flat = tree.as_flat(lhs.get_raw_tid(), /*current_tid=*/17, /*root_tid=*/11);
  std::cout << "\nFlat wrapper example: root_tid=" << flat.get_root_tid() << " current_tid=" << flat.get_current_tid()
            << " raw_tid=" << flat.get_raw_tid() << "\n";

  return 0;
}
