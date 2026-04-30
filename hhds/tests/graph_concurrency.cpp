// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

// Concurrent GraphLibrary registry stress test. Mirrors forest_concurrency.cpp:
// only the registry is required to be thread-safe (create_io / find_io /
// create_graph / delete on different IOs). Build with --config=tsan to catch
// races; under a normal build it just exercises the lock paths.

#include <gtest/gtest.h>

#include <atomic>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "hhds/graph.hpp"

namespace {

constexpr int kThreads          = 8;
constexpr int kIosPerThread     = 32;
constexpr int kFindsPerIteration = 16;

}  // namespace

TEST(GraphConcurrency, ParallelCreateIoOnDistinctNames) {
  hhds::GraphLibrary lib;

  std::atomic<int>  ready{0};
  std::atomic<bool> go{false};

  std::vector<std::thread> threads;
  threads.reserve(kThreads);

  for (int t = 0; t < kThreads; ++t) {
    threads.emplace_back([t, &lib, &ready, &go] {
      ready.fetch_add(1, std::memory_order_acq_rel);
      while (!go.load(std::memory_order_acquire)) {
      }
      for (int i = 0; i < kIosPerThread; ++i) {
        const auto name = "t" + std::to_string(t) + "_io" + std::to_string(i);
        auto       gio  = lib.create_io(name);
        ASSERT_NE(gio, nullptr);
        auto graph = gio->create_graph();
        ASSERT_NE(graph, nullptr);
      }
    });
  }
  while (ready.load(std::memory_order_acquire) < kThreads) {
  }
  go.store(true, std::memory_order_release);
  for (auto& th : threads) {
    th.join();
  }

  for (int t = 0; t < kThreads; ++t) {
    for (int i = 0; i < kIosPerThread; ++i) {
      const auto name = "t" + std::to_string(t) + "_io" + std::to_string(i);
      auto       gio  = lib.find_io(name);
      ASSERT_NE(gio, nullptr) << name;
      ASSERT_NE(gio->get_graph(), nullptr) << name;
    }
  }
}

TEST(GraphConcurrency, ParallelCreateAndFind) {
  hhds::GraphLibrary lib;

  for (int t = 0; t < kThreads; ++t) {
    for (int i = 0; i < kIosPerThread; ++i) {
      auto gio = lib.create_io("seed_" + std::to_string(t) + "_" + std::to_string(i));
      (void)gio->create_graph();
    }
  }

  std::atomic<bool> go{false};
  std::atomic<bool> stop{false};

  std::vector<std::thread> writers;
  writers.reserve(kThreads);
  for (int t = 0; t < kThreads; ++t) {
    writers.emplace_back([t, &lib, &go] {
      while (!go.load(std::memory_order_acquire)) {
      }
      for (int i = 0; i < kIosPerThread; ++i) {
        const auto name = "fresh_t" + std::to_string(t) + "_io" + std::to_string(i);
        auto       gio  = lib.create_io(name);
        ASSERT_NE(gio, nullptr);
        ASSERT_NE(gio->create_graph(), nullptr);
      }
    });
  }

  std::vector<std::thread> readers;
  readers.reserve(kThreads);
  for (int t = 0; t < kThreads; ++t) {
    readers.emplace_back([t, &lib, &go, &stop] {
      std::mt19937 rng(static_cast<unsigned>(t) * 9173u);
      while (!go.load(std::memory_order_acquire)) {
      }
      while (!stop.load(std::memory_order_acquire)) {
        for (int j = 0; j < kFindsPerIteration; ++j) {
          const auto wt = static_cast<int>(rng() % static_cast<unsigned>(kThreads));
          const auto wi = static_cast<int>(rng() % static_cast<unsigned>(kIosPerThread));
          const auto name = "seed_" + std::to_string(wt) + "_" + std::to_string(wi);
          auto       gio  = lib.find_io(name);
          if (gio) {
            (void)gio->has_graph();
            (void)gio->get_graph();
          }
          (void)lib.find_graph(name);
          (void)lib.mutation_epoch();
        }
      }
    });
  }

  go.store(true, std::memory_order_release);
  for (auto& th : writers) {
    th.join();
  }
  stop.store(true, std::memory_order_release);
  for (auto& th : readers) {
    th.join();
  }
}

TEST(GraphConcurrency, ParallelCreateAndDeleteDistinctSlots) {
  hhds::GraphLibrary lib;

  std::atomic<bool> go{false};

  std::vector<std::thread> threads;
  threads.reserve(kThreads);
  for (int t = 0; t < kThreads; ++t) {
    threads.emplace_back([t, &lib, &go] {
      while (!go.load(std::memory_order_acquire)) {
      }
      for (int i = 0; i < kIosPerThread; ++i) {
        const auto name = "churn_t" + std::to_string(t) + "_io" + std::to_string(i);
        auto       gio  = lib.create_io(name);
        ASSERT_NE(gio, nullptr);
        ASSERT_NE(gio->create_graph(), nullptr);
        lib.delete_graphio(name);
        ASSERT_EQ(lib.find_io(name), nullptr);
      }
    });
  }
  go.store(true, std::memory_order_release);
  for (auto& th : threads) {
    th.join();
  }
}
