#include <unistd.h>

#include <iostream>
#include <random>
#include <vector>

#include "tree.hpp"

#if __has_include("hhds/tests/tree_test_utils.hpp")
#include "hhds/tests/tree_test_utils.hpp"
#elif __has_include("tests/tree_test_utils.hpp")
#include "tests/tree_test_utils.hpp"
#else
namespace hhds_test {

using IntNode = hhds::Tree::Node_class;

inline void ensure_size(std::vector<int>& values, hhds::Tid tid) {
  if (tid >= static_cast<hhds::Tid>(values.size())) {
    values.resize(static_cast<size_t>(tid + 1));
  }
}

inline void set_value(std::vector<int>& values, IntNode node, int value) {
  ensure_size(values, node.get_debug_nid());
  values[static_cast<size_t>(node.get_debug_nid())] = value;
}

inline IntNode add_root(hhds::Tree& tree, std::vector<int>& values, int value) {
  const auto node = tree.add_root_node();
  set_value(values, node, value);
  return node;
}

inline IntNode add_child(hhds::Tree& /*tree*/, std::vector<int>& values, IntNode parent, int value) {
  const auto node = parent.add_child();
  set_value(values, node, value);
  return node;
}

}  // namespace hhds_test
#endif

// Utility function to generate a random int within a range
int generate_random_int(std::default_random_engine& generator, int min, int max) {
  std::uniform_int_distribution<int> distribution(min, max);
  return distribution(generator);
}

// Test 1: Very Deep Tree (Tens of Millions of Nodes)
void test_deep_tree(int nodes) {
  std::default_random_engine generator(42);
  // int                        num_nodes = 10'000'000;  // Use a smaller number for testing
  // int                        num_nodes = 10'000'000;  // Use a smaller number for testing
  // int                        num_nodes = 100;  // Use a smaller number for testing
  int num_nodes = nodes;

  auto             hhds_tree = hhds::Tree::create();
  std::vector<int> hhds_values;

  auto data_to_add  = generate_random_int(generator, 1, 100);
  auto hhds_root    = hhds_test::add_root(*hhds_tree, hhds_values, data_to_add);
  auto hhds_current = hhds_root;

  for (int i = 0; i < num_nodes; ++i) {
    data_to_add  = generate_random_int(generator, 1, 100);
    hhds_current = hhds_test::add_child(*hhds_tree, hhds_values, hhds_current, data_to_add);
  }
}

int main(int argc, char* argv[]) {
  int nodes = 10;
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--nodes" && i + 1 < argc) {
      nodes = std::atoi(argv[++i]);
    }
  }
  test_deep_tree(nodes);
  sleep(5);
  return 0;
}
