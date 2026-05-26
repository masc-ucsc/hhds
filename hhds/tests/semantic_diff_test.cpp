// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// Tests for semantic diff canonicalization in hhds_tree_diff.
// Verifies dependency analysis, statement grouping, and canonical reordering.

#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "gtest/gtest.h"
#include "tree.hpp"
#include "tree_edit_distance.hpp"

namespace {

std::string write_temp(const std::string& name, const std::string& content) {
  std::string tmpl = "/tmp/hhds_semtest_" + name + "_XXXXXX";
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
  return hhds::TreeEditDistance::compute(d1.tree, d2.tree, hhds::EditCosts{}, cost_fn).distance;
}

double run_diff(const std::string& file1, const std::string& file2, bool semantic = false) {
  std::string binary    = "hhds/hhds_tree_diff";
  const char* srcdir    = std::getenv("TEST_SRCDIR");
  const char* workspace = std::getenv("TEST_WORKSPACE");
  if (srcdir && workspace) {
    binary = std::string(srcdir) + "/" + std::string(workspace) + "/" + binary;
  }
  std::string cmd = binary + " " + file1 + " " + file2;
  if (semantic) {
    cmd += " --semantic";
  }
  cmd += " 2>/dev/null";

  FILE* pipe = popen(cmd.c_str(), "r");
  if (!pipe) {
    return -1;
  }

  double distance = -1;
  char   buf[256];
  while (fgets(buf, sizeof(buf), pipe)) {
    std::string line(buf);
    auto        pos = line.find("Distance: ");
    if (pos != std::string::npos) {
      distance = std::stod(line.substr(pos + 10));
    }
  }
  pclose(pipe);
  return distance;
}

// Original: const x = 1; const y = 2; mut c = x + y; cassert c == 3
const std::string add_trivial
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

// Swapped: const y = 2; const x = 1; mut c = x + y; cassert c == 3
const std::string add_trivial_swapped
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

// Three independent declarations: a=1, b=2, c=3 (no dependencies between them)
const std::string three_independent
    = "three_ind\n"
      "└── top\n"
      "    └── stmts\n"
      "        ├── attr_set\n"
      "        │   ├── ref 'a'\n"
      "        │   ├── const 'type'\n"
      "        │   └── const\n"
      "        ├── assign\n"
      "        │   ├── ref 'a'\n"
      "        │   └── const '1'\n"
      "        ├── attr_set\n"
      "        │   ├── ref 'b'\n"
      "        │   ├── const 'type'\n"
      "        │   └── const\n"
      "        ├── assign\n"
      "        │   ├── ref 'b'\n"
      "        │   └── const '2'\n"
      "        ├── attr_set\n"
      "        │   ├── ref 'c'\n"
      "        │   ├── const 'type'\n"
      "        │   └── const\n"
      "        └── assign\n"
      "            ├── ref 'c'\n"
      "            └── const '3'\n";

// Same three declarations in reverse order: c=3, b=2, a=1
const std::string three_independent_reversed
    = "three_rev\n"
      "└── top\n"
      "    └── stmts\n"
      "        ├── attr_set\n"
      "        │   ├── ref 'c'\n"
      "        │   ├── const 'type'\n"
      "        │   └── const\n"
      "        ├── assign\n"
      "        │   ├── ref 'c'\n"
      "        │   └── const '3'\n"
      "        ├── attr_set\n"
      "        │   ├── ref 'b'\n"
      "        │   ├── const 'type'\n"
      "        │   └── const\n"
      "        ├── assign\n"
      "        │   ├── ref 'b'\n"
      "        │   └── const '2'\n"
      "        ├── attr_set\n"
      "        │   ├── ref 'a'\n"
      "        │   ├── const 'type'\n"
      "        │   └── const\n"
      "        └── assign\n"
      "            ├── ref 'a'\n"
      "            └── const '1'\n";

// Dependent: a=1, b=a+1 (b depends on a, cannot reorder)
const std::string dependent_raw
    = "dep_raw\n"
      "└── top\n"
      "    └── stmts\n"
      "        ├── attr_set\n"
      "        │   ├── ref 'a'\n"
      "        │   ├── const 'type'\n"
      "        │   └── const\n"
      "        ├── assign\n"
      "        │   ├── ref 'a'\n"
      "        │   └── const '1'\n"
      "        ├── plus\n"
      "        │   ├── ref '___1'\n"
      "        │   ├── ref 'a'\n"
      "        │   └── const '1'\n"
      "        ├── attr_set\n"
      "        │   ├── ref 'b'\n"
      "        │   ├── const 'type'\n"
      "        │   └── const\n"
      "        └── assign\n"
      "            ├── ref 'b'\n"
      "            └── ref '___1'\n";

// Same as above but INCORRECTLY reordered: b=a+1, a=1
// Semantic diff should NOT make these equal (b depends on a)
const std::string dependent_raw_swapped
    = "dep_raw_swap\n"
      "└── top\n"
      "    └── stmts\n"
      "        ├── plus\n"
      "        │   ├── ref '___1'\n"
      "        │   ├── ref 'a'\n"
      "        │   └── const '1'\n"
      "        ├── attr_set\n"
      "        │   ├── ref 'b'\n"
      "        │   ├── const 'type'\n"
      "        │   └── const\n"
      "        ├── assign\n"
      "        │   ├── ref 'b'\n"
      "        │   └── ref '___1'\n"
      "        ├── attr_set\n"
      "        │   ├── ref 'a'\n"
      "        │   ├── const 'type'\n"
      "        │   └── const\n"
      "        └── assign\n"
      "            ├── ref 'a'\n"
      "            └── const '1'\n";

// Single statement: just a = 1
const std::string single_stmt
    = "single\n"
      "└── top\n"
      "    └── stmts\n"
      "        ├── attr_set\n"
      "        │   ├── ref 'a'\n"
      "        │   ├── const 'type'\n"
      "        │   └── const\n"
      "        └── assign\n"
      "            ├── ref 'a'\n"
      "            └── const '1'\n";

// a=100, b=100
const std::string ab_hundred
    = "ab_hundred\n"
      "└── top\n"
      "    └── stmts\n"
      "        ├── attr_set\n"
      "        │   ├── ref 'a'\n"
      "        │   ├── const 'type'\n"
      "        │   └── const\n"
      "        ├── assign\n"
      "        │   ├── ref 'a'\n"
      "        │   └── const '100'\n"
      "        ├── attr_set\n"
      "        │   ├── ref 'b'\n"
      "        │   ├── const 'type'\n"
      "        │   └── const\n"
      "        └── assign\n"
      "            ├── ref 'b'\n"
      "            └── const '100'\n";

// b=100, a=100 (swapped version)
const std::string ba_hundred
    = "ba_hundred\n"
      "└── top\n"
      "    └── stmts\n"
      "        ├── attr_set\n"
      "        │   ├── ref 'b'\n"
      "        │   ├── const 'type'\n"
      "        │   └── const\n"
      "        ├── assign\n"
      "        │   ├── ref 'b'\n"
      "        │   └── const '100'\n"
      "        ├── attr_set\n"
      "        │   ├── ref 'a'\n"
      "        │   ├── const 'type'\n"
      "        │   └── const\n"
      "        └── assign\n"
      "            ├── ref 'a'\n"
      "            └── const '100'\n";

// Actually different values: a=1 vs a=2
const std::string a_is_one
    = "a_one\n"
      "└── top\n"
      "    └── stmts\n"
      "        ├── attr_set\n"
      "        │   ├── ref 'a'\n"
      "        │   ├── const 'type'\n"
      "        │   └── const\n"
      "        └── assign\n"
      "            ├── ref 'a'\n"
      "            └── const '1'\n";

const std::string a_is_two
    = "a_two\n"
      "└── top\n"
      "    └── stmts\n"
      "        ├── attr_set\n"
      "        │   ├── ref 'a'\n"
      "        │   ├── const 'type'\n"
      "        │   └── const\n"
      "        └── assign\n"
      "            ├── ref 'a'\n"
      "            └── const '2'\n";

}  // namespace

// ─── Test cases ──────────────────────────────────────────────────────

class SemanticDiffTest : public ::testing::Test {
protected:
  std::vector<std::string> temp_files;

  std::string make_temp(const std::string& name, const std::string& content) {
    auto path = write_temp(name, content);
    if (!path.empty()) {
      temp_files.push_back(path);
    }
    return path;
  }

  void TearDown() override {
    for (const auto& f : temp_files) {
      std::remove(f.c_str());
    }
  }
};

// ── Plain diff baseline tests ────────────────────────────────────────

TEST_F(SemanticDiffTest, PlainDiff_IdenticalTrees) {
  auto f1 = make_temp("plain_id_a", add_trivial);
  auto f2 = make_temp("plain_id_b", add_trivial);
  auto d1 = load_test_dump(f1);
  auto d2 = load_test_dump(f2);
  EXPECT_EQ(compute_diff(d1, d2), 0.0);
}

TEST_F(SemanticDiffTest, PlainDiff_SwappedDeclarations) {
  auto f1 = make_temp("plain_sw_a", add_trivial);
  auto f2 = make_temp("plain_sw_b", add_trivial_swapped);
  auto d1 = load_test_dump(f1);
  auto d2 = load_test_dump(f2);
  EXPECT_EQ(compute_diff(d1, d2), 6.0) << "Plain diff should detect 6 relabels for swapped x/y declarations";
}

// ── Semantic diff: independent statement reordering ──────────────────

TEST_F(SemanticDiffTest, Semantic_SwappedDeclarations_DistanceZero) {
  auto   f1   = make_temp("sem_sw_a", add_trivial);
  auto   f2   = make_temp("sem_sw_b", add_trivial_swapped);
  double dist = run_diff(f1, f2, /*semantic=*/true);
  EXPECT_EQ(dist, 0.0) << "Semantic diff: swapping independent const x/y should give distance 0";
}

TEST_F(SemanticDiffTest, Semantic_ThreeIndependent_Reversed) {
  auto   f1   = make_temp("sem_3i_a", three_independent);
  auto   f2   = make_temp("sem_3i_b", three_independent_reversed);
  double dist = run_diff(f1, f2, /*semantic=*/true);
  EXPECT_EQ(dist, 0.0) << "Semantic diff: reversing 3 independent declarations should give distance 0";
}

TEST_F(SemanticDiffTest, Semantic_ProfessorExample_AB_Hundred) {
  auto   f1   = make_temp("sem_ab_a", ab_hundred);
  auto   f2   = make_temp("sem_ab_b", ba_hundred);
  double dist = run_diff(f1, f2, /*semantic=*/true);
  EXPECT_EQ(dist, 0.0) << "Semantic diff: a=100;b=100 ≡ b=100;a=100 should give distance 0";
}

// ── Semantic diff: dependency preservation ───────────────────────────

TEST_F(SemanticDiffTest, Semantic_DependentStatements_NotReordered) {
  auto   f1   = make_temp("sem_dep_a", dependent_raw);
  auto   f2   = make_temp("sem_dep_b", dependent_raw_swapped);
  double dist = run_diff(f1, f2, /*semantic=*/true);
  EXPECT_GT(dist, 0.0) << "Semantic diff: dependent statements (b=a+1 depends on a=1) should NOT be equivalent when reordered";
}

// ── Semantic diff: no false equivalence ──────────────────────────────

TEST_F(SemanticDiffTest, Semantic_DifferentValues_NotEquivalent) {
  auto   f1   = make_temp("sem_val_a", a_is_one);
  auto   f2   = make_temp("sem_val_b", a_is_two);
  double dist = run_diff(f1, f2, /*semantic=*/true);
  EXPECT_GT(dist, 0.0) << "Semantic diff: a=1 vs a=2 are genuinely different, distance must be > 0";
}

TEST_F(SemanticDiffTest, Semantic_IdenticalTrees_StillZero) {
  auto   f1   = make_temp("sem_id_a", add_trivial);
  auto   f2   = make_temp("sem_id_b", add_trivial);
  double dist = run_diff(f1, f2, /*semantic=*/true);
  EXPECT_EQ(dist, 0.0) << "Semantic diff: identical trees should still be distance 0";
}

// ── Semantic diff: single statement edge case ────────────────────────

TEST_F(SemanticDiffTest, Semantic_SingleStatement_SelfCompare) {
  auto   f1   = make_temp("sem_single_a", single_stmt);
  auto   f2   = make_temp("sem_single_b", single_stmt);
  double dist = run_diff(f1, f2, /*semantic=*/true);
  EXPECT_EQ(dist, 0.0) << "Semantic diff: single statement compared to itself should be 0";
}

// ── Semantic diff: plain mode unaffected ─────────────────────────────

TEST_F(SemanticDiffTest, PlainMode_AB_Hundred_NotZero) {
  auto   f1   = make_temp("plain_ab_a", ab_hundred);
  auto   f2   = make_temp("plain_ab_b", ba_hundred);
  double dist = run_diff(f1, f2, /*semantic=*/false);
  EXPECT_GT(dist, 0.0) << "Plain diff: a=100;b=100 vs b=100;a=100 should detect differences";
}

TEST_F(SemanticDiffTest, PlainMode_ThreeIndependent_NotZero) {
  auto   f1   = make_temp("plain_3i_a", three_independent);
  auto   f2   = make_temp("plain_3i_b", three_independent_reversed);
  double dist = run_diff(f1, f2, /*semantic=*/false);
  EXPECT_GT(dist, 0.0) << "Plain diff: reversed independent declarations should detect differences";
}

// ── Symmetry ─────────────────────────────────────────────────────────

TEST_F(SemanticDiffTest, Semantic_Symmetry) {
  auto   f1      = make_temp("sym_a", add_trivial);
  auto   f2      = make_temp("sym_b", add_trivial_swapped);
  double dist_ab = run_diff(f1, f2, /*semantic=*/true);
  double dist_ba = run_diff(f2, f1, /*semantic=*/true);
  EXPECT_EQ(dist_ab, dist_ba) << "Semantic diff should be symmetric";
}