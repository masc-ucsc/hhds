#include <cstdint>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "tree.hpp"
#include "tree_edit_distance.hpp"

namespace {

size_t find_content_start(const std::string& line) {
  size_t       pos = 0;
  const size_t len = line.size();

  while (pos < len) {
    if (pos + 9 < len && static_cast<unsigned char>(line[pos]) == 0xe2) {
      const auto b1 = static_cast<unsigned char>(line[pos + 1]);
      const auto b2 = static_cast<unsigned char>(line[pos + 2]);
      if ((b1 == 0x94 && b2 == 0x9c) || (b1 == 0x94 && b2 == 0x94)) {
        return pos + 10;
      }
    }
    if (pos + 5 < len && static_cast<unsigned char>(line[pos]) == 0xe2 && static_cast<unsigned char>(line[pos + 1]) == 0x94
        && static_cast<unsigned char>(line[pos + 2]) == 0x82) {
      pos += 6;
      continue;
    }
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

// Extract the full content of a dump line (type + node_text), e.g. "ref 'x'"
std::string extract_full_content(const std::string& line) {
  const size_t s = find_content_start(line);
  if (s == std::string::npos) {
    return {};
  }
  size_t e = line.size();
  while (e > s && (line[e - 1] == '\n' || line[e - 1] == '\r' || line[e - 1] == ' ')) {
    --e;
  }
  return line.substr(s, e - s);
}

struct DumpData {
  std::shared_ptr<hhds::Tree>                     tree;
  std::vector<hhds::Tree::NodeData>               nodes;
  std::vector<hhds::Type_entry>                   type_table;
  std::unordered_map<hhds::Tree_pos, std::string> node_texts;
  std::unordered_map<hhds::Tree_pos, std::string> full_labels;
};

std::vector<hhds::Type_entry> build_type_table(const std::string& filename) {
  std::vector<std::string>                type_names;
  std::unordered_map<std::string, size_t> type_idx;

  std::ifstream ifs(filename);
  if (!ifs.is_open()) {
    std::cerr << "hhds_tree_diff: cannot open " << filename << "\n";
    return {};
  }

  std::string line;
  bool        first_line = true;
  while (std::getline(ifs, line)) {
    if (first_line) {
      first_line = false;
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

  std::vector<hhds::Type_entry> table;
  table.reserve(type_names.size());
  for (const auto& n : type_names) {
    table.push_back({n, hhds::Statement_class::Node});
  }
  return table;
}

std::unordered_map<hhds::Tree_pos, std::string> build_full_labels(const std::string&                       filename,
                                                                  const std::vector<hhds::Tree::NodeData>& nodes) {
  std::vector<std::string> contents;
  {
    std::ifstream ifs(filename);
    if (!ifs.is_open()) {
      return {};
    }
    std::string line;
    bool        first_line = true;
    while (std::getline(ifs, line)) {
      if (first_line) {
        first_line = false;
        continue;
      }
      auto c = extract_full_content(line);
      if (!c.empty()) {
        contents.push_back(std::move(c));
      }
    }
  }

  std::unordered_map<hhds::Tree_pos, std::string> labels;
  for (size_t i = 0; i < nodes.size() && i < contents.size(); ++i) {
    labels[nodes[i].pos] = contents[i];
  }
  return labels;
}

DumpData load_dump(const std::string& filename) {
  DumpData data;
  data.type_table = build_type_table(filename);
  if (data.type_table.empty()) {
    return data;
  }

  auto [tree, nodes] = hhds::Tree::read_dump(filename, data.type_table);
  data.tree          = std::move(tree);
  data.nodes         = std::move(nodes);

  for (const auto& nd : data.nodes) {
    data.node_texts[nd.pos] = nd.node_text;
  }

  data.full_labels = build_full_labels(filename, data.nodes);

  return data;
}

void print_tree(const DumpData& data, const std::string& filename) {
  hhds::Tree::PrintOptions opts;
  opts.type_table = data.type_table;
  opts.node_text  = [&data](const hhds::Tree::Node_class& node) {
    auto it = data.node_texts.find(node.get_debug_nid());
    return it == data.node_texts.end() ? std::string() : it->second;
  };
  std::cout << "=== " << filename << " ===\n";
  data.tree->dump(std::cout, opts);
  std::cout << "\n";
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 3) {
    std::cerr << "Usage: " << argv[0] << " <dump_file_1> <dump_file_2>\n";
    return 1;
  }

  const std::string file1 = argv[1];
  const std::string file2 = argv[2];

  auto data1 = load_dump(file1);
  if (!data1.tree) {
    std::cerr << "hhds_tree_diff: failed to parse " << file1 << "\n";
    return 1;
  }

  auto data2 = load_dump(file2);
  if (!data2.tree) {
    std::cerr << "hhds_tree_diff: failed to parse " << file2 << "\n";
    return 1;
  }

  print_tree(data1, file1);
  print_tree(data2, file2);

  auto cost_fn = [&data1, &data2](const hhds::Tree::Node_class& a, const hhds::Tree::Node_class& b) -> double {
    std::string label_a, label_b;

    auto it1 = data1.full_labels.find(a.get_debug_nid());
    if (it1 != data1.full_labels.end()) {
      label_a = it1->second;
    }

    auto it2 = data2.full_labels.find(b.get_debug_nid());
    if (it2 != data2.full_labels.end()) {
      label_b = it2->second;
    }

    return (label_a == label_b) ? 0.0 : 1.0;
  };

  auto result = hhds::TreeEditDistance::compute(data1.tree, data2.tree, hhds::EditCosts{}, cost_fn);

  std::cout << "=== Tree Edit Distance ===\n";
  std::cout << "Distance: " << result.distance << "\n";

  return 0;
}