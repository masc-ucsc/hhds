// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <vector>

#include "hhds/tree.hpp"

namespace hhds_test {

using IntNode = hhds::Tree::Node_class;

inline void ensure_size(std::vector<int>& values, hhds::Tid tid) {
  if (tid >= static_cast<hhds::Tid>(values.size())) {
    values.resize(static_cast<size_t>(tid + 1));
  }
}

inline void set_value(std::vector<int>& values, IntNode node, int value) {
  ensure_size(values, node.get_raw_tid());
  values[static_cast<size_t>(node.get_raw_tid())] = value;
}

inline int get_value(const std::vector<int>& values, IntNode node) {
  return values[static_cast<size_t>(node.get_raw_tid())];
}

inline IntNode add_root(hhds::Tree& tree, std::vector<int>& values, int value) {
  const auto node = tree.add_root_node();
  set_value(values, node, value);
  return node;
}

inline IntNode add_child(hhds::Tree& tree, std::vector<int>& values, IntNode parent, int value) {
  const auto node = tree.add_child(parent);
  set_value(values, node, value);
  return node;
}

inline IntNode append_sibling(hhds::Tree& tree, std::vector<int>& values, IntNode sibling, int value) {
  const auto node = tree.append_sibling(sibling);
  set_value(values, node, value);
  return node;
}

inline IntNode insert_next_sibling(hhds::Tree& tree, std::vector<int>& values, IntNode sibling, int value) {
  const auto node = tree.insert_next_sibling(sibling);
  set_value(values, node, value);
  return node;
}

inline void preorder_values(const hhds::Tree& tree, const std::vector<int>& values, std::vector<int>& out) {
  for (auto node : tree.pre_order()) {
    out.push_back(get_value(values, node));
  }
}

inline void postorder_values(hhds::Tree& tree, const std::vector<int>& values, std::vector<int>& out) {
  for (auto node : tree.post_order()) {
    out.push_back(get_value(values, node));
  }
}

}  // namespace hhds_test
