#include <iostream>
#include <random>
#include <vector>
#include <unistd.h>

#include "tree.hpp"

// Utility function to generate a random int within a range
int generate_random_int(std::default_random_engine& generator, int min, int max) {
  std::uniform_int_distribution<int> distribution(min, max);
  return distribution(generator);
}

// Utility function to compare two vectors
template <typename T>
bool compare_vectors(const std::vector<T>& vec1, const std::vector<T>& vec2) {
  return vec1 == vec2;
}

// Test 1: Very Deep Tree (Tens of Millions of Nodes)
void test_deep_tree(int nodes) {
  std::default_random_engine generator(42);
  //int                        num_nodes = 10'000'000;  // Use a smaller number for testing
  //int                        num_nodes = 10'000'000;  // Use a smaller number for testing
  //int                        num_nodes = 100;  // Use a smaller number for testing
  int num_nodes = nodes;

  hhds::tree<int> hhds_tree;

  auto data_to_add = generate_random_int(generator, 1, 100);
  auto hhds_root   = hhds_tree.add_root(data_to_add);
  auto           hhds_current = hhds_root;

  for (int i = 0; i < num_nodes; ++i) {
    data_to_add  = generate_random_int(generator, 1, 100);
    hhds_current = hhds_tree.add_child(hhds_current, data_to_add);
  }
}

int main(int argc, char*argv[]) {
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
