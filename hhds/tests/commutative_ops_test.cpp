#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

#include "gtest/gtest.h"

namespace {

std::string write_temp(const std::string& name, const std::string& content) {
  std::string tmpl = "/tmp/hhds_commtest_" + name + "_XXXXXX";
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

double run_diff(const std::string& file1, const std::string& file2, bool semantic) {
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

// Each pair tests an op with operands in original vs swapped order.
// All use the standard LNAST shape: first child = destination, rest = operands.

// Helper to build a simple 2-operand expression dump
std::string make_binop_dump(const std::string& name, const std::string& op, const std::string& op1, const std::string& op2) {
  return name + "\n"
       "└── top\n"
       "    └── stmts\n"
       "        └── " + op + "\n"
       "            ├── ref '___1'\n"
       "            ├── " + op1 + "\n"
       "            └── " + op2 + "\n";
}

// Helper to build a 3-operand (N-ary) expression dump
std::string make_ternop_dump(const std::string& name, const std::string& op, const std::string& op1, const std::string& op2,
                             const std::string& op3) {
  return name + "\n"
       "└── top\n"
       "    └── stmts\n"
       "        └── " + op + "\n"
       "            ├── ref '___1'\n"
       "            ├── " + op1 + "\n"
       "            ├── " + op2 + "\n"
       "            └── " + op3 + "\n";
}

// Combined: statement reordering + commutative op
// a = 1; b = 2; c = a + b  vs  b = 2; a = 1; c = b + a
const std::string combined_original
    = "combined_orig\n"
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
      "        ├── plus\n"
      "        │   ├── ref '___1'\n"
      "        │   ├── ref 'a'\n"
      "        │   └── ref 'b'\n"
      "        ├── attr_set\n"
      "        │   ├── ref 'c'\n"
      "        │   ├── const 'type'\n"
      "        │   └── const 'mut'\n"
      "        └── assign\n"
      "            ├── ref 'c'\n"
      "            └── ref '___1'\n";

const std::string combined_swapped
    = "combined_swap\n"
      "└── top\n"
      "    └── stmts\n"
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
      "        ├── assign\n"
      "        │   ├── ref 'a'\n"
      "        │   └── const '1'\n"
      "        ├── plus\n"
      "        │   ├── ref '___1'\n"
      "        │   ├── ref 'b'\n"
      "        │   └── ref 'a'\n"
      "        ├── attr_set\n"
      "        │   ├── ref 'c'\n"
      "        │   ├── const 'type'\n"
      "        │   └── const 'mut'\n"
      "        └── assign\n"
      "            ├── ref 'c'\n"
      "            └── ref '___1'\n";

}  // namespace

class CommutativeOpsTest : public ::testing::Test {
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

  // Test a commutative op: swapped operands should give distance 0 in semantic mode
  void test_commutative(const std::string& op_name, const std::string& op1, const std::string& op2) {
    auto dump_a = make_binop_dump("test_a", op_name, op1, op2);
    auto dump_b = make_binop_dump("test_b", op_name, op2, op1);
    auto f1     = make_temp(op_name + "_a", dump_a);
    auto f2     = make_temp(op_name + "_b", dump_b);

    double plain    = run_diff(f1, f2, false);
    double semantic = run_diff(f1, f2, true);

    EXPECT_GT(plain, 0.0) << op_name << ": plain diff should detect swapped operands";
    EXPECT_EQ(semantic, 0.0) << op_name << ": semantic diff should give 0 for swapped operands";
  }

  // Test a non-commutative op: swapped operands should give distance > 0 even in semantic mode
  void test_non_commutative(const std::string& op_name, const std::string& op1, const std::string& op2) {
    auto dump_a = make_binop_dump("test_a", op_name, op1, op2);
    auto dump_b = make_binop_dump("test_b", op_name, op2, op1);
    auto f1     = make_temp(op_name + "_nc_a", dump_a);
    auto f2     = make_temp(op_name + "_nc_b", dump_b);

    double semantic = run_diff(f1, f2, true);
    EXPECT_GT(semantic, 0.0) << op_name << ": non-commutative op should NOT give 0 for swapped operands";
  }
};

// ─── Commutative operations: swapped operands → distance 0 ──────────

TEST_F(CommutativeOpsTest, Plus_Commutative) { test_commutative("plus", "ref 'x'", "ref 'y'"); }

TEST_F(CommutativeOpsTest, Mult_Commutative) { test_commutative("mult", "ref 'a'", "ref 'b'"); }

TEST_F(CommutativeOpsTest, BitAnd_Commutative) { test_commutative("bit_and", "ref 'p'", "ref 'q'"); }

TEST_F(CommutativeOpsTest, BitOr_Commutative) { test_commutative("bit_or", "ref 'm'", "ref 'n'"); }

TEST_F(CommutativeOpsTest, BitXor_Commutative) { test_commutative("bit_xor", "ref 's'", "ref 't'"); }

TEST_F(CommutativeOpsTest, LogAnd_Commutative) { test_commutative("log_and", "ref 'c1'", "ref 'c2'"); }

TEST_F(CommutativeOpsTest, LogOr_Commutative) { test_commutative("log_or", "ref 'd1'", "ref 'd2'"); }

TEST_F(CommutativeOpsTest, Eq_Commutative) { test_commutative("eq", "ref 'lhs'", "ref 'rhs'"); }

TEST_F(CommutativeOpsTest, Ne_Commutative) { test_commutative("ne", "ref 'val1'", "ref 'val2'"); }

// ─── Commutative with constants ──────────────────────────────────────

TEST_F(CommutativeOpsTest, Plus_RefAndConst) { test_commutative("plus", "ref 'x'", "const '5'"); }

TEST_F(CommutativeOpsTest, Eq_TwoConsts) { test_commutative("eq", "const '1'", "const '2'"); }

// ─── Non-commutative operations: swapped operands → distance > 0 ────

TEST_F(CommutativeOpsTest, Minus_NonCommutative) { test_non_commutative("minus", "ref 'x'", "ref 'y'"); }

TEST_F(CommutativeOpsTest, Div_NonCommutative) { test_non_commutative("div", "ref 'a'", "ref 'b'"); }

TEST_F(CommutativeOpsTest, Mod_NonCommutative) { test_non_commutative("mod", "ref 'a'", "ref 'b'"); }

TEST_F(CommutativeOpsTest, Shl_NonCommutative) { test_non_commutative("shl", "ref 'x'", "const '2'"); }

TEST_F(CommutativeOpsTest, Sra_NonCommutative) { test_non_commutative("sra", "ref 'x'", "const '1'"); }

TEST_F(CommutativeOpsTest, Lt_NonCommutative) { test_non_commutative("lt", "ref 'a'", "ref 'b'"); }

TEST_F(CommutativeOpsTest, Le_NonCommutative) { test_non_commutative("le", "ref 'a'", "ref 'b'"); }

TEST_F(CommutativeOpsTest, Gt_NonCommutative) { test_non_commutative("gt", "ref 'a'", "ref 'b'"); }

TEST_F(CommutativeOpsTest, Ge_NonCommutative) { test_non_commutative("ge", "ref 'a'", "ref 'b'"); }

// ─── N-ary commutative: 3 operands ──────────────────────────────────

TEST_F(CommutativeOpsTest, Plus_ThreeOperands_AllPermutations) {
  auto dump_abc = make_ternop_dump("abc", "plus", "ref 'a'", "ref 'b'", "ref 'c'");
  auto dump_cab = make_ternop_dump("cab", "plus", "ref 'c'", "ref 'a'", "ref 'b'");
  auto dump_bca = make_ternop_dump("bca", "plus", "ref 'b'", "ref 'c'", "ref 'a'");

  auto f_abc = make_temp("nary_abc", dump_abc);
  auto f_cab = make_temp("nary_cab", dump_cab);
  auto f_bca = make_temp("nary_bca", dump_bca);

  EXPECT_EQ(run_diff(f_abc, f_cab, true), 0.0) << "plus(a,b,c) ≡ plus(c,a,b)";
  EXPECT_EQ(run_diff(f_abc, f_bca, true), 0.0) << "plus(a,b,c) ≡ plus(b,c,a)";
  EXPECT_EQ(run_diff(f_cab, f_bca, true), 0.0) << "plus(c,a,b) ≡ plus(b,c,a)";
}

// ─── Destination preserved ───────────────────────────────────────────

TEST_F(CommutativeOpsTest, DestinationNotSorted) {
  auto dump_a = make_binop_dump("dst_a", "plus", "ref 'a'", "ref 'b'");
  auto dump_b = make_binop_dump("dst_b", "plus", "ref 'b'", "ref 'a'");
  auto f1     = make_temp("dst_test_a", dump_a);
  auto f2     = make_temp("dst_test_b", dump_b);

  EXPECT_EQ(run_diff(f1, f2, true), 0.0) << "Destination should stay in place";
}

// ─── Identical commutative ops → distance 0 ─────────────────────────

TEST_F(CommutativeOpsTest, Identical_StillZero) {
  auto dump = make_binop_dump("same", "plus", "ref 'x'", "ref 'y'");
  auto f1   = make_temp("id_a", dump);
  auto f2   = make_temp("id_b", dump);

  EXPECT_EQ(run_diff(f1, f2, true), 0.0);
  EXPECT_EQ(run_diff(f1, f2, false), 0.0);
}

// ─── Combined: statement reordering + commutative ops ────────────────

TEST_F(CommutativeOpsTest, Combined_StmtReorder_PlusSwap) {
  auto f1 = make_temp("comb_a", combined_original);
  auto f2 = make_temp("comb_b", combined_swapped);

  double plain    = run_diff(f1, f2, false);
  double semantic = run_diff(f1, f2, true);

  EXPECT_GT(plain, 0.0) << "Plain diff should detect both reordering and operand swap";
  EXPECT_EQ(semantic, 0.0) << "Semantic diff should handle statement reordering + commutative operand swap together";
}

// ─── Symmetry ────────────────────────────────────────────────────────

TEST_F(CommutativeOpsTest, Symmetry) {
  auto dump_a = make_binop_dump("sym_a", "plus", "ref 'x'", "ref 'y'");
  auto dump_b = make_binop_dump("sym_b", "plus", "ref 'y'", "ref 'x'");
  auto f1     = make_temp("sym_1", dump_a);
  auto f2     = make_temp("sym_2", dump_b);

  double ab = run_diff(f1, f2, true);
  double ba = run_diff(f2, f1, true);
  EXPECT_EQ(ab, ba) << "Semantic diff should be symmetric for commutative ops";
}