#include <cstdio>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "gtest/gtest.h"
#include "tree.hpp"
#include "tree_edit_distance.hpp"

namespace {

std::string write_temp_dump(const std::string& name, const std::string& content) {
  std::string tmpl = "/tmp/hhds_test_" + name + "_XXXXXX";
  int         fd   = mkstemp(tmpl.data());
  EXPECT_NE(fd, -1) << "Cannot create temp file: " << tmpl;
  if (fd == -1) {
    return {};
  }
  close(fd);
  std::ofstream ofs(tmpl);
  EXPECT_TRUE(ofs.is_open()) << "Cannot open temp file: " << tmpl;
  if (!ofs.is_open()) {
    return {};
  }
  ofs << content;
  return tmpl;
}

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

struct TestDumpData {
  std::shared_ptr<hhds::Tree>                     tree;
  std::vector<hhds::Tree::NodeData>               nodes;
  std::vector<std::string>                        type_names;
  std::vector<hhds::Type_entry>                   type_table;
  std::unordered_map<hhds::Tree_pos, std::string> full_labels;
};

TestDumpData load_test_dump(const std::string& filename) {
  TestDumpData data;

  {
    std::unordered_map<std::string, size_t> type_idx;
    std::ifstream                           ifs(filename);
    if (!ifs.is_open()) {
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
  data.tree          = std::move(tree);
  data.nodes         = std::move(nodes);

  {
    std::vector<std::string> contents;
    std::ifstream            ifs(filename);
    if (!ifs.is_open()) {
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
  }

  return data;
}

double compute_diff(const TestDumpData& d1, const TestDumpData& d2) {
  auto cost_fn = [&d1, &d2](const hhds::Tree::Node_class& a, const hhds::Tree::Node_class& b) -> double {
    std::string la, lb;
    auto        it1 = d1.full_labels.find(a.get_debug_nid());
    if (it1 != d1.full_labels.end()) {
      la = it1->second;
    }
    auto it2 = d2.full_labels.find(b.get_debug_nid());
    if (it2 != d2.full_labels.end()) {
      lb = it2->second;
    }
    return (la == lb) ? 0.0 : 1.0;
  };
  auto result = hhds::TreeEditDistance::compute(d1.tree, d2.tree, hhds::EditCosts{}, cost_fn);
  return result.distance;
}

const std::string add_trivial_dump
    = "add_trivial\n"
      "└── top\n"
      "    └── stmts\n"
      "        ├── attr_set\n"
      "        │   ├── ref 'x'\n"
      "        │   ├── const 'type'\n"
      "        │   └── const\n"
      "        ├── assign\n"
      "        │   ├── ref 'x'\n"
      "        │   └── const '1'\n"
      "        ├── attr_set\n"
      "        │   ├── ref 'y'\n"
      "        │   ├── const 'type'\n"
      "        │   └── const\n"
      "        ├── assign\n"
      "        │   ├── ref 'y'\n"
      "        │   └── const '2'\n"
      "        ├── plus\n"
      "        │   ├── ref '___1'\n"
      "        │   ├── ref 'x'\n"
      "        │   └── ref 'y'\n"
      "        ├── attr_set\n"
      "        │   ├── ref 'c'\n"
      "        │   ├── const 'type'\n"
      "        │   └── const 'mut'\n"
      "        ├── assign\n"
      "        │   ├── ref 'c'\n"
      "        │   └── ref '___1'\n"
      "        ├── eq\n"
      "        │   ├── ref '___2'\n"
      "        │   ├── ref 'c'\n"
      "        │   └── const '3'\n"
      "        └── cassert\n"
      "            └── ref '___2'\n";

const std::string add_trivial_swapped_dump
    = "add_trivial_swapped\n"
      "└── top\n"
      "    └── stmts\n"
      "        ├── attr_set\n"
      "        │   ├── ref 'y'\n"
      "        │   ├── const 'type'\n"
      "        │   └── const\n"
      "        ├── assign\n"
      "        │   ├── ref 'y'\n"
      "        │   └── const '2'\n"
      "        ├── attr_set\n"
      "        │   ├── ref 'x'\n"
      "        │   ├── const 'type'\n"
      "        │   └── const\n"
      "        ├── assign\n"
      "        │   ├── ref 'x'\n"
      "        │   └── const '1'\n"
      "        ├── plus\n"
      "        │   ├── ref '___1'\n"
      "        │   ├── ref 'x'\n"
      "        │   └── ref 'y'\n"
      "        ├── attr_set\n"
      "        │   ├── ref 'c'\n"
      "        │   ├── const 'type'\n"
      "        │   └── const 'mut'\n"
      "        ├── assign\n"
      "        │   ├── ref 'c'\n"
      "        │   └── ref '___1'\n"
      "        ├── eq\n"
      "        │   ├── ref '___2'\n"
      "        │   ├── ref 'c'\n"
      "        │   └── const '3'\n"
      "        └── cassert\n"
      "            └── ref '___2'\n";

const std::string simple_tree_dump
    = "simple\n"
      "└── top\n"
      "    ├── assign\n"
      "    │   ├── ref 'a'\n"
      "    │   └── const '1'\n"
      "    └── assign\n"
      "        ├── ref 'b'\n"
      "        └── const '2'\n";

const std::string simple_tree_extra_node_dump
    = "simple_extra\n"
      "└── top\n"
      "    ├── assign\n"
      "    │   ├── ref 'a'\n"
      "    │   └── const '1'\n"
      "    ├── assign\n"
      "    │   ├── ref 'b'\n"
      "    │   └── const '2'\n"
      "    └── assign\n"
      "        ├── ref 'c'\n"
      "        └── const '3'\n";

}  // namespace

// ─── Test cases ──────────────────────────────────────────────────────

TEST(TreeDiff, IdenticalTrees) {
  auto path1 = write_temp_dump("identical_a", add_trivial_dump);
  auto path2 = write_temp_dump("identical_b", add_trivial_dump);

  auto d1 = load_test_dump(path1);
  auto d2 = load_test_dump(path2);

  ASSERT_NE(d1.tree, nullptr);
  ASSERT_NE(d2.tree, nullptr);

  double dist = compute_diff(d1, d2);
  EXPECT_EQ(dist, 0.0) << "Identical dumps should have distance 0";

  std::remove(path1.c_str());
  std::remove(path2.c_str());
}

TEST(TreeDiff, SwappedDeclarations) {
  auto path1 = write_temp_dump("swap_a", add_trivial_dump);
  auto path2 = write_temp_dump("swap_b", add_trivial_swapped_dump);

  auto d1 = load_test_dump(path1);
  auto d2 = load_test_dump(path2);

  ASSERT_NE(d1.tree, nullptr);
  ASSERT_NE(d2.tree, nullptr);

  double dist = compute_diff(d1, d2);
  EXPECT_EQ(dist, 6.0) << "Swapping x/y declarations should give distance 6";

  std::remove(path1.c_str());
  std::remove(path2.c_str());
}

TEST(TreeDiff, SingleInsertion) {
  auto path1 = write_temp_dump("insert_a", simple_tree_dump);
  auto path2 = write_temp_dump("insert_b", simple_tree_extra_node_dump);

  auto d1 = load_test_dump(path1);
  auto d2 = load_test_dump(path2);

  ASSERT_NE(d1.tree, nullptr);
  ASSERT_NE(d2.tree, nullptr);

  double dist = compute_diff(d1, d2);
  // Inserting one assign subtree (assign + ref 'c' + const '3') = 3 insertions
  EXPECT_EQ(dist, 3.0) << "Inserting one assign subtree should cost 3";

  std::remove(path1.c_str());
  std::remove(path2.c_str());
}

TEST(TreeDiff, SymmetricDistance) {
  auto path1 = write_temp_dump("sym_a", add_trivial_dump);
  auto path2 = write_temp_dump("sym_b", add_trivial_swapped_dump);

  auto d1 = load_test_dump(path1);
  auto d2 = load_test_dump(path2);

  ASSERT_NE(d1.tree, nullptr);
  ASSERT_NE(d2.tree, nullptr);

  double dist_ab = compute_diff(d1, d2);
  double dist_ba = compute_diff(d2, d1);
  EXPECT_EQ(dist_ab, dist_ba) << "TED should be symmetric";

  std::remove(path1.c_str());
  std::remove(path2.c_str());
}

TEST(TreeDiff, SelfDistance) {
  auto path = write_temp_dump("self", simple_tree_dump);

  auto d = load_test_dump(path);
  ASSERT_NE(d.tree, nullptr);

  double dist = compute_diff(d, d);
  EXPECT_EQ(dist, 0.0) << "Distance to self should be 0";

  std::remove(path.c_str());
}

TEST(TreeDiff, MissingFile) {
  std::string tmpl = "/tmp/hhds_test_missing_XXXXXX";
  int         fd   = mkstemp(tmpl.data());
  ASSERT_NE(fd, -1);
  close(fd);
  std::remove(tmpl.c_str());
  auto d = load_test_dump(tmpl);
  EXPECT_EQ(d.tree, nullptr) << "Loading missing file should return null tree";
}

TEST(TreeDiff, TypeTableCorrectness) {
  auto path = write_temp_dump("types", add_trivial_dump);
  auto d    = load_test_dump(path);

  std::vector<std::string> expected_types = {"top", "stmts", "attr_set", "ref", "const", "assign", "plus", "eq", "cassert"};
  ASSERT_EQ(d.type_names.size(), expected_types.size());
  for (size_t i = 0; i < expected_types.size(); ++i) {
    EXPECT_EQ(d.type_names[i], expected_types[i]) << "Type at index " << i << " mismatch";
  }

  std::remove(path.c_str());
}

TEST(TreeDiff, NodeCount) {
  auto path = write_temp_dump("count", add_trivial_dump);
  auto d    = load_test_dump(path);

  ASSERT_NE(d.tree, nullptr);
  // add_trivial has 33 nodes: top, stmts, 3*attr_set + 9 children,
  // 3*assign + 6 children, plus + 3 children, eq + 3 children, cassert + 1 child
  EXPECT_EQ(d.nodes.size(), 33U) << "add_trivial should have 33 nodes";

  std::remove(path.c_str());
}