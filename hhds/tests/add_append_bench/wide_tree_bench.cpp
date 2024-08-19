#include <benchmark/benchmark.h>
#include <iostream>
#include <vector>
#include <random>
#include <chrono>
#include <fstream>
#include <unistd.h>
#include <memory>

#include "../../tree.hpp"
#include "../../lhtree.hpp"

// class CustomMemoryManager : public benchmark::MemoryManager {
// public:
//     void Start() override {
//         start_memory_usage_ = GetCurrentMemoryUsage();
//     }

//     void Stop(Result& result) override { // Use reference instead of pointer
//         result.num_allocs = 0;  // Set to zero since we're not tracking allocations
//         result.max_bytes_used = GetCurrentMemoryUsage() - start_memory_usage_;
//     }

// private:
//     size_t start_memory_usage_;

//     size_t GetCurrentMemoryUsage() {
//         std::ifstream statm("/proc/self/statm");
//         size_t resident = 0;
//         statm >> resident >> resident; // The second value is the RSS
//         std::cout << "resident " << resident << std::endl;;
//         return resident * getpagesize(); // Convert pages to bytes
//     }
// };

auto now = std::chrono::high_resolution_clock::now();
auto microseconds = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();
std::default_random_engine generator(microseconds);

// Utility function to generate a random int within a range
int generate_random_int(std::default_random_engine& generator, int min, int max)  {
    std::uniform_int_distribution<int> distribution(min, max);
    return distribution(generator);
}

void build_hhds_tree(hhds::tree<int>& hhds_tree, int num_nodes) {
    auto hhds_root = hhds_tree.add_root(generate_random_int(generator, 1, 100));

    for (int i = 0; i < num_nodes; ++i) {
        hhds_tree.add_child(hhds_root, generate_random_int(generator, 1, 100));
    }
}

void build_lh_tree(lh::tree<int>& lh_tree, int num_nodes) {
    lh_tree.set_root(generate_random_int(generator, 1, 100));
    lh::Tree_index lh_current(0, 0);

    for (int i = 0; i < num_nodes; ++i) {
        lh_tree.add_child(lh_current, generate_random_int(generator, 1, 100));
    }
}

// Tree that is 10 nodes wide
void test_wide_tree_10_hhds(benchmark::State& state) {
    int num_nodes = 10;
    for (auto _ : state) {
        hhds::tree<int> hhds_tree;
        build_hhds_tree(hhds_tree, num_nodes);
    }
}
void test_wide_tree_10_lh(benchmark::State& state) {
    int num_nodes = 10;
    for (auto _ : state) {
        lh::tree<int> lh_tree;
        build_lh_tree(lh_tree, num_nodes);
    }
}

// Tree that is 100 nodes wide
void test_wide_tree_100_hhds(benchmark::State& state) {
    int num_nodes = 100;
    for (auto _ : state) {
        hhds::tree<int> hhds_tree;
        build_hhds_tree(hhds_tree, num_nodes);
    }
}
void test_wide_tree_100_lh(benchmark::State& state) {
    int num_nodes = 100;
    for (auto _ : state) {
        lh::tree<int> lh_tree;
        build_lh_tree(lh_tree, num_nodes);
    }
}

// Tree that is 1000 nodes wide
void test_wide_tree_1000_hhds(benchmark::State& state) {
    int num_nodes = 1000;
    for (auto _ : state) {
        hhds::tree<int> hhds_tree;
        build_hhds_tree(hhds_tree, num_nodes);
    }
}
void test_wide_tree_1000_lh(benchmark::State& state) {
    int num_nodes = 1000;
    for (auto _ : state) {
        lh::tree<int> lh_tree;
        build_lh_tree(lh_tree, num_nodes);
    }
}

// Tree that is 10000 nodes wide
void test_wide_tree_10000_hhds(benchmark::State& state) {
    int num_nodes = 10000;
    for (auto _ : state) {
        hhds::tree<int> hhds_tree;
        build_hhds_tree(hhds_tree, num_nodes);
    }
}
void test_wide_tree_10000_lh(benchmark::State& state) {
    int num_nodes = 10000;
    for (auto _ : state) {
        lh::tree<int> lh_tree;
        build_lh_tree(lh_tree, num_nodes);
    }
}

// Tree that is 100000 nodes wide
void test_wide_tree_100000_hhds(benchmark::State& state) {
    int num_nodes = 100000;
    for (auto _ : state) {
        hhds::tree<int> hhds_tree;
        build_hhds_tree(hhds_tree, num_nodes);
    }
}
void test_wide_tree_100000_lh(benchmark::State& state) {
    int num_nodes = 100000;
    for (auto _ : state) {
        lh::tree<int> lh_tree;
        build_lh_tree(lh_tree, num_nodes);
    }
}

// Tree that is 1000000 nodes wide
void test_wide_tree_1000000_hhds(benchmark::State& state) {
    int num_nodes = 1000000;
    for (auto _ : state) {
        hhds::tree<int> hhds_tree;
        build_hhds_tree(hhds_tree, num_nodes);
    }
}
void test_wide_tree_1000000_lh(benchmark::State& state) {
    int num_nodes = 1000000;
    for (auto _ : state) {
        lh::tree<int> lh_tree;
        build_lh_tree(lh_tree, num_nodes);
    }
}

// Tree that is 10000000 nodes wide
void test_wide_tree_10000000_hhds(benchmark::State& state) {
    int num_nodes = 10000000;
    for (auto _ : state) {
        hhds::tree<int> hhds_tree;
        build_hhds_tree(hhds_tree, num_nodes);
    }
}
void test_wide_tree_10000000_lh(benchmark::State& state) {
    int num_nodes = 10000000;
    for (auto _ : state) {
        lh::tree<int> lh_tree;
        build_lh_tree(lh_tree, num_nodes);
    }
}

// Benchmark registration
BENCHMARK(test_wide_tree_10_hhds);
BENCHMARK(test_wide_tree_10_lh);
BENCHMARK(test_wide_tree_100_hhds);
BENCHMARK(test_wide_tree_100_lh);
BENCHMARK(test_wide_tree_1000_hhds);
BENCHMARK(test_wide_tree_1000_lh);
BENCHMARK(test_wide_tree_10000_hhds);
BENCHMARK(test_wide_tree_10000_lh);
BENCHMARK(test_wide_tree_100000_hhds);
BENCHMARK(test_wide_tree_100000_lh);
BENCHMARK(test_wide_tree_1000000_hhds);
BENCHMARK(test_wide_tree_1000000_lh);
BENCHMARK(test_wide_tree_10000000_hhds);
BENCHMARK(test_wide_tree_10000000_lh);

BENCHMARK_MAIN();

// int main(int argc, char** argv) {
//     CustomMemoryManager memory_manager;
//     benchmark::RegisterMemoryManager(&memory_manager);
    
//     benchmark::Initialize(&argc, argv);
//     benchmark::RunSpecifiedBenchmarks();
//     benchmark::Shutdown();

//     benchmark::RegisterMemoryManager(nullptr); // Unregister the memory manager
//     return 0;
// }