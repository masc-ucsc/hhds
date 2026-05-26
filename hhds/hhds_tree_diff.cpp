#include <cstdint>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include <unistd.h>

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

// ─── Semantic canonicalization helpers ────────────────────────────────
// These functions reorder independent statement groups under `stmts`
// so that semantically equivalent programs produce identical trees.

int compute_depth(const std::string& line) {
  int    depth = 0;
  size_t pos   = 0;
  const size_t len = line.size();
  while (pos < len) {
    if (pos + 5 < len && static_cast<unsigned char>(line[pos]) == 0xe2
        && static_cast<unsigned char>(line[pos + 1]) == 0x94
        && static_cast<unsigned char>(line[pos + 2]) == 0x82) {
      ++depth;
      pos += 6;
      continue;
    }
    if (pos + 3 < len && line[pos] == ' ' && line[pos + 1] == ' '
        && line[pos + 2] == ' ' && line[pos + 3] == ' ') {
      ++depth;
      pos += 4;
      continue;
    }
    break;
  }
  return depth;
}

std::string extract_ref_name(const std::string& content) {
  auto pos = content.find('\'');
  if (pos == std::string::npos) return {};
  auto end = content.find('\'', pos + 1);
  if (end == std::string::npos) return {};
  return content.substr(pos + 1, end - pos - 1);
}

struct StmtGroup {
  std::vector<int>       line_indices;
  std::string            write_var;
  std::set<std::string>  read_vars;
  std::string            sort_key;
};


void extract_refs(const std::vector<std::string>& lines, const std::vector<int>& indices,
                  std::string& write_var, std::set<std::string>& read_vars) {
  bool first_ref = true;
  for (int idx : indices) {
    auto type = extract_type_name(lines[idx]);
    if (type == "ref") {
      auto name = extract_ref_name(extract_full_content(lines[idx]));
      if (name.empty()) continue;
      if (first_ref) {
        write_var = name;
        first_ref = false;
      } else if (name != write_var) {
        read_vars.insert(name);
      }
    }
  }
}

std::string canonicalize_dump(const std::string& filename) {
  std::ifstream ifs(filename);
  if (!ifs.is_open()) return filename;
  std::vector<std::string> lines;
  std::string line;
  while (std::getline(ifs, line)) {
    lines.push_back(line);
  }
  ifs.close();

  int stmts_idx   = -1;
  int stmts_depth = -1;
  for (int i = 1; i < static_cast<int>(lines.size()); ++i) {
    if (extract_type_name(lines[i]) == "stmts") {
      stmts_idx   = i;
      stmts_depth = compute_depth(lines[i]);
      break;
    }
  }
  if (stmts_idx < 0) return filename;

  int child_depth = stmts_depth + 1;

  struct Subtree { int start; int end; };
  std::vector<Subtree> subtrees;

  for (int i = stmts_idx + 1; i < static_cast<int>(lines.size()); ++i) {
    int d = compute_depth(lines[i]);
    if (d < child_depth) break;
    if (d == child_depth) {
      if (!subtrees.empty()) subtrees.back().end = i;
      subtrees.push_back({i, static_cast<int>(lines.size())});
    }
  }
  if (!subtrees.empty()) {
    for (int i = subtrees.back().start + 1; i < static_cast<int>(lines.size()); ++i) {
      if (compute_depth(lines[i]) <= stmts_depth) {
        subtrees.back().end = i;
        break;
      }
    }
  }
  if (subtrees.empty()) return filename;

  std::vector<StmtGroup> groups;
  int si = 0;
  while (si < static_cast<int>(subtrees.size())) {
    auto type_si = extract_type_name(lines[subtrees[si].start]);

    std::vector<int> indices_si;
    for (int li = subtrees[si].start; li < subtrees[si].end; ++li) {
      indices_si.push_back(li);
    }

    std::string ref_si;
    for (int li = subtrees[si].start + 1; li < subtrees[si].end; ++li) {
      if (extract_type_name(lines[li]) == "ref") {
        ref_si = extract_ref_name(extract_full_content(lines[li]));
        break;
      }
    }

    if (type_si == "attr_set" && si + 1 < static_cast<int>(subtrees.size())) {
      auto type_next = extract_type_name(lines[subtrees[si + 1].start]);
      std::string ref_next;
      for (int li = subtrees[si + 1].start + 1; li < subtrees[si + 1].end; ++li) {
        if (extract_type_name(lines[li]) == "ref") {
          ref_next = extract_ref_name(extract_full_content(lines[li]));
          break;
        }
      }

      if (type_next == "assign" && ref_si == ref_next) {
        StmtGroup g;
        for (int li = subtrees[si].start; li < subtrees[si].end; ++li)
          g.line_indices.push_back(li);
        for (int li = subtrees[si + 1].start; li < subtrees[si + 1].end; ++li)
          g.line_indices.push_back(li);

        extract_refs(lines, g.line_indices, g.write_var, g.read_vars);
        g.sort_key = g.write_var.empty() ? ref_si : g.write_var;
        groups.push_back(std::move(g));
        si += 2;
        continue;
      }
    }

    StmtGroup g;
    g.line_indices = std::move(indices_si);
    extract_refs(lines, g.line_indices, g.write_var, g.read_vars);
    g.sort_key = g.write_var.empty() ? ref_si : g.write_var;
    groups.push_back(std::move(g));
    ++si;
  }

  int ng = static_cast<int>(groups.size());
  std::vector<std::vector<int>> adj(ng);
  std::vector<int>              in_degree(ng, 0);

  for (int a = 0; a < ng; ++a) {
    for (int b = a + 1; b < ng; ++b) {
      bool dep = false;
      if (!groups[a].write_var.empty() && groups[b].read_vars.count(groups[a].write_var)) dep = true;
      if (!groups[b].write_var.empty() && groups[a].read_vars.count(groups[b].write_var)) dep = true;
      if (!groups[a].write_var.empty() && groups[a].write_var == groups[b].write_var) dep = true;

      if (dep) {
        adj[a].push_back(b);
        ++in_degree[b];
      }
    }
  }

  auto cmp = [&groups](int a, int b) {
    if (groups[a].sort_key != groups[b].sort_key)
      return groups[a].sort_key < groups[b].sort_key;
    return a < b;
  };
  std::set<int, decltype(cmp)> ready(cmp);
  for (int i = 0; i < ng; ++i) {
    if (in_degree[i] == 0) ready.insert(i);
  }

  std::vector<int> sorted_order;
  sorted_order.reserve(ng);
  while (!ready.empty()) {
    int u = *ready.begin();
    ready.erase(ready.begin());
    sorted_order.push_back(u);
    for (int v : adj[u]) {
      if (--in_degree[v] == 0) ready.insert(v);
    }
  }

  std::string result;
  for (int i = 0; i <= stmts_idx; ++i) {
    result += lines[i];
    result += '\n';
  }
  for (int gi : sorted_order) {
    for (int li : groups[gi].line_indices) {
      result += lines[li];
      result += '\n';
    }
  }
  int after_stmts = subtrees.back().end;
  for (int i = after_stmts; i < static_cast<int>(lines.size()); ++i) {
    result += lines[i];
    result += '\n';
  }

  std::string tmpl = "/tmp/hhds_semantic_XXXXXX";
  int fd = mkstemp(tmpl.data());
  if (fd == -1) return filename;
  close(fd);
  std::ofstream ofs(tmpl);
  if (!ofs.is_open()) return filename;
  ofs << result;
  ofs.close();

  return tmpl;
}

struct DumpData {
  DumpData() = default;
  DumpData(DumpData&&) = default;
  DumpData& operator=(DumpData&&) = default;
  DumpData(const DumpData&) = delete;
  DumpData& operator=(const DumpData&) = delete;
  
  std::shared_ptr<hhds::Tree>                     tree;
  std::vector<hhds::Tree::NodeData>               nodes;
  std::vector<std::string>                        type_names;
  std::vector<hhds::Type_entry>                   type_table;
  std::unordered_map<hhds::Tree_pos, std::string> node_texts;
  std::unordered_map<hhds::Tree_pos, std::string> full_labels;
};

DumpData load_dump(const std::string& filename) {
  DumpData data;

  {
    std::unordered_map<std::string, size_t> type_idx;
    std::ifstream                           ifs(filename);
    if (!ifs.is_open()) {
      std::cerr << "hhds_tree_diff: cannot open " << filename << "\n";
      return data;
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
        type_idx[name] = data.type_names.size();
        data.type_names.push_back(std::move(name));
      }
    }
  }

  data.type_table.reserve(data.type_names.size());
  for (const auto& n : data.type_names) {
    data.type_table.push_back({n, hhds::Statement_class::Node});
  }

  auto [tree, nodes] = hhds::Tree::read_dump(filename, data.type_table);
  if (!tree) {
    std::cerr << "hhds_tree_diff: failed to parse " << filename << "\n";
    return data;
  }
  data.tree  = std::move(tree);
  data.nodes = std::move(nodes);

  for (const auto& nd : data.nodes) {
    data.node_texts[nd.pos] = nd.node_text;
  }

  {
    std::vector<std::string> contents;
    std::ifstream            ifs(filename);
    if (!ifs.is_open()) {
      std::cerr << "hhds_tree_diff: cannot reopen " << filename << " for label extraction\n";
      data.tree.reset();
      return data;
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
    for (size_t i = 0; i < data.nodes.size() && i < contents.size(); ++i) {
      data.full_labels[data.nodes[i].pos] = contents[i];
    }
    if (contents.size() != data.nodes.size()) {
      std::cerr << "hhds_tree_diff: label count mismatch in " << filename
                << " (expected " << data.nodes.size() << ", got " << contents.size() << ")\n";
      data.tree.reset();
      return data;
    }
  }

  return data;
}

void print_tree(const DumpData& data, const std::string& filename) {
  std::unordered_map<hhds::Tree_pos, std::vector<std::pair<std::string, std::string>>> node_attrs;
  std::vector<std::string>                                                             attr_keys;
  std::unordered_map<std::string, bool>                                                seen_keys;
  for (const auto& nd : data.nodes) {
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
  opts.type_table = data.type_table;
  opts.node_text  = [&data](const hhds::Tree::Node_class& node) {
    auto it = data.node_texts.find(node.get_debug_nid());
    return it == data.node_texts.end() ? std::string() : it->second;
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

  std::cout << "=== " << filename << " ===\n";
  data.tree->dump(std::cout, opts);
  std::cout << "\n";
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 3 || argc > 4) {
    std::cerr << "Usage: " << argv[0] << " <dump_file_1> <dump_file_2> [--semantic]\n";
    return 1;
  }

  const std::string file1 = argv[1];
  const std::string file2 = argv[2];
  const bool semantic = (argc == 4 && std::string(argv[3]) == "--semantic");

  std::string load_file1 = file1;
  std::string load_file2 = file2;
  if (semantic) {
    load_file1 = canonicalize_dump(file1);
    load_file2 = canonicalize_dump(file2);
  }

  auto data1 = load_dump(load_file1);
  if (!data1.tree) {
    std::cerr << "hhds_tree_diff: failed to parse " << file1 << "\n";
    return 1;
  }

  auto data2 = load_dump(load_file2);
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
  if (semantic) {
    std::cout << "Mode: semantic (independent statements canonicalized)\n";
  }
  std::cout << "Distance: " << result.distance << "\n";

  if (semantic) {
    if (load_file1 != file1) std::remove(load_file1.c_str());
    if (load_file2 != file2) std::remove(load_file2.c_str());
  }

  return 0;
}
