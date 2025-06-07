#include <benchmark/benchmark.h>
#include <string>
#include <tree_sitter/api.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <random>
#include <chrono>
#include <filesystem>

#include "tree.hpp"
#include "lhtree.hpp"

thread_local std::default_random_engine generator(42); // Fixed seed for reproducibility

// Utility function to generate a random int within a range
int generate_random_int(std::default_random_engine& generator, int min, int max) {
  std::uniform_int_distribution<int> distribution(min, max);
  return distribution(generator);
}


// Declare the external language (from the compiled grammar)
extern "C" const TSLanguage *tree_sitter_cpp();

void build_tree(TSNode node, hhds::tree<int>& hhds_tree,  uint32_t num_nodes = 5, int depth = 0) {
  std::vector<int> stack;
  std::vector<TSNode> node_stack;
  auto hhds_root = hhds_tree.add_root(generate_random_int(generator, 1, 100));
  stack.push_back(hhds_root);
  node_stack.push_back(node);
  uint32_t node_count = 1;

  while (!node_stack.empty()) {
    auto cur_node = node_stack.back();
    auto current = stack.back();
    stack.pop_back();
    node_stack.pop_back();
    uint32_t child_count = ts_node_child_count(cur_node);
    for (int i = child_count - 1; i >= 0; i--) {
      TSNode child = ts_node_child(cur_node, i);
      node_stack.push_back(child);
      stack.push_back(hhds_tree.add_child(current, generate_random_int(generator, 1, 100)));
      node_count += 1;
      if (node_count == num_nodes) {
        return;
      }
    }
  }
  //printf("max nodes: %d\n", node_count);
  //std::cout << "max nodes: " << node_count << std::endl;
}


TSTree *ts_init(hhds::tree<int>& hhds_tree, int num_nodes) {
  std::string path ("./hhds/SelectionDAG.cpp");
  std::ifstream t(path);
  if (!t) {
    throw std::runtime_error("Failed to open file: " + path);
  }
  std::stringstream buffer;
  buffer << t.rdbuf();
  std::string source = buffer.str();

  TSParser *parser = ts_parser_new();
  ts_parser_set_language(parser, tree_sitter_cpp());

  TSTree *tree = ts_parser_parse_string(parser, NULL, source.c_str(), source.size());
  return tree;
}

void ts_delete(TSTree* tree) {
  ts_tree_delete(tree);
}

void test_repr_tree_10_hhds(benchmark::State& state) {
  uint32_t num_nodes = 10;
  hhds::tree<int> hhds_tree;
  TSTree *tree = ts_init(hhds_tree, num_nodes);
    for (auto _ : state) {
	  hhds::tree<int> hhds_tree;
	  TSNode root_node = ts_tree_root_node(tree);
	  build_tree(root_node, hhds_tree, num_nodes);
    }
    ts_delete(tree);
}
void test_repr_tree_100_hhds(benchmark::State& state) {
  uint32_t num_nodes = 100;
  hhds::tree<int> hhds_tree;
  TSTree *tree = ts_init(hhds_tree, num_nodes);
    for (auto _ : state) {
	  hhds::tree<int> hhds_tree;
	  TSNode root_node = ts_tree_root_node(tree);
	  build_tree(root_node, hhds_tree, num_nodes);
    }
    ts_delete(tree);
}
void test_repr_tree_1000_hhds(benchmark::State& state) {
  uint32_t num_nodes = 1000;
  hhds::tree<int> hhds_tree;
  TSTree *tree = ts_init(hhds_tree, num_nodes);
    for (auto _ : state) {
	  hhds::tree<int> hhds_tree;
	  TSNode root_node = ts_tree_root_node(tree);
	  build_tree(root_node, hhds_tree, num_nodes);
    }
    ts_delete(tree);
}
void test_repr_tree_10000_hhds(benchmark::State& state) {
  uint32_t num_nodes = 10000;
  hhds::tree<int> hhds_tree;
  TSTree *tree = ts_init(hhds_tree, num_nodes);
    for (auto _ : state) {
	  hhds::tree<int> hhds_tree;
	  TSNode root_node = ts_tree_root_node(tree);
	  build_tree(root_node, hhds_tree, num_nodes);
    }
    ts_delete(tree);
}
void test_repr_tree_100000_hhds(benchmark::State& state) {
  uint32_t num_nodes = 100000;
  hhds::tree<int> hhds_tree;
  TSTree *tree = ts_init(hhds_tree, num_nodes);
    for (auto _ : state) {
	  hhds::tree<int> hhds_tree;
	  TSNode root_node = ts_tree_root_node(tree);
	  build_tree(root_node, hhds_tree, num_nodes);
    }
    ts_delete(tree);
}
void test_repr_tree_1000000_hhds(benchmark::State& state) {
  uint32_t num_nodes = 1000000;
  hhds::tree<int> hhds_tree;
  TSTree *tree = ts_init(hhds_tree, num_nodes);
    for (auto _ : state) {
	  hhds::tree<int> hhds_tree;
	  TSNode root_node = ts_tree_root_node(tree);
	  build_tree(root_node, hhds_tree, num_nodes);
    }
    ts_delete(tree);
}

BENCHMARK(test_repr_tree_10_hhds);
BENCHMARK(test_repr_tree_100_hhds);
BENCHMARK(test_repr_tree_1000_hhds);
BENCHMARK(test_repr_tree_10000_hhds);
BENCHMARK(test_repr_tree_100000_hhds);
BENCHMARK(test_repr_tree_1000000_hhds);
BENCHMARK_MAIN();
