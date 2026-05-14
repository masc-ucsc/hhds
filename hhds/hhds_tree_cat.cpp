// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// hhds_tree_cat: read a tree dump file (produced by Tree::write_dump),
// reconstruct the tree, and print it to stdout via Tree::dump.

#include <cstdint>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "tree.hpp"

namespace {

// Skip past the tree-drawing prefix ("│   " / "    ") and the final connector
// ("├── " or "└── ") and return the offset where the node content begins.
// Returns std::string::npos if the line has no connector (e.g. blank line or
// the tree-name header line).
size_t find_content_start(const std::string& line) {
  size_t       pos = 0;
  const size_t len = line.size();

  while (pos < len) {
    // Connector "├── " or "└── ": 3-byte UTF-8 char + "── " = 10 bytes.
    if (pos + 9 < len && static_cast<unsigned char>(line[pos]) == 0xe2) {
      const auto b1 = static_cast<unsigned char>(line[pos + 1]);
      const auto b2 = static_cast<unsigned char>(line[pos + 2]);
      if ((b1 == 0x94 && b2 == 0x9c) || (b1 == 0x94 && b2 == 0x94)) {
        return pos + 10;
      }
    }
    // "│   ": 3-byte UTF-8 char + 3 spaces = 6 bytes.
    if (pos + 5 < len && static_cast<unsigned char>(line[pos]) == 0xe2 && static_cast<unsigned char>(line[pos + 1]) == 0x94
        && static_cast<unsigned char>(line[pos + 2]) == 0x82) {
      pos += 6;
      continue;
    }
    // "    " (4 spaces, last-child gap).
    if (pos + 3 < len && line[pos] == ' ' && line[pos + 1] == ' ' && line[pos + 2] == ' ' && line[pos + 3] == ' ') {
      pos += 4;
      continue;
    }
    break;
  }
  return std::string::npos;
}

std::string extract_type_name(const std::string& line) {
  const size_t s = find_content_start(line);
  if (s == std::string::npos) {
    return {};
  }
  size_t e = s;
  while (e < line.size() && line[e] != ' ' && line[e] != '\n' && line[e] != '\r') {
    ++e;
  }
  return line.substr(s, e - s);
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "Usage: " << argv[0] << " <tree_dump_file>\n";
    return 1;
  }
  const std::string filename = argv[1];

  // Pass 1: collect unique type names so we can build a Type_entry table.
  // Without it, read_dump would fall back to type 0 and the re-emitted dump
  // would lose every type name.
  std::vector<std::string>                type_names;
  std::unordered_map<std::string, size_t> type_idx;
  {
    std::ifstream ifs(filename);
    if (!ifs.is_open()) {
      std::cerr << "hhds_tree_cat: cannot open " << filename << "\n";
      return 1;
    }
    std::string line;
    bool        first_line = true;
    while (std::getline(ifs, line)) {
      if (first_line) {
        first_line = false;  // tree name header
        continue;
      }
      auto name = extract_type_name(line);
      if (name.empty()) {
        continue;
      }
      if (type_idx.find(name) == type_idx.end()) {
        type_idx[name] = type_names.size();
        type_names.push_back(std::move(name));
      }
    }
  }

  std::vector<hhds::Type_entry> type_table;
  type_table.reserve(type_names.size());
  for (const auto& n : type_names) {
    type_table.push_back({n, hhds::Statement_class::Node});
  }

  // Pass 2: reconstruct the tree.
  auto [tree, nodes] = hhds::Tree::read_dump(filename, type_table);
  if (!tree) {
    std::cerr << "hhds_tree_cat: failed to parse " << filename << "\n";
    return 1;
  }

  // Capture per-node text + attributes recovered by read_dump (NodeData).
  std::unordered_map<hhds::Tree_pos, std::string>                                      node_texts;
  std::unordered_map<hhds::Tree_pos, std::vector<std::pair<std::string, std::string>>> node_attrs;
  std::vector<std::string>                                                             attr_keys;
  std::unordered_map<std::string, bool>                                                seen_keys;
  for (const auto& nd : nodes) {
    node_texts[nd.pos] = nd.node_text;
    if (!nd.attributes.empty()) {
      node_attrs[nd.pos] = nd.attributes;
      for (const auto& [k, _] : nd.attributes) {
        if (!seen_keys[k]) {
          seen_keys[k] = true;
          attr_keys.push_back(k);
        }
      }
    }
  }

  hhds::Tree::PrintOptions opts;
  opts.type_table = type_table;
  opts.node_text  = [&node_texts](const hhds::Tree::Node_class& node) {
    auto it = node_texts.find(node.get_debug_nid());
    return it == node_texts.end() ? std::string() : it->second;
  };
  for (const auto& key : attr_keys) {
    opts.attributes.emplace_back(key, [&node_attrs, key](const hhds::Tree::Node_class& node) -> std::optional<std::string> {
      auto it = node_attrs.find(node.get_debug_nid());
      if (it == node_attrs.end()) {
        return std::nullopt;
      }
      for (const auto& [k, v] : it->second) {
        if (k == key) {
          return v;
        }
      }
      return std::nullopt;
    });
  }

  tree->dump(std::cout, opts);
  return 0;
}
