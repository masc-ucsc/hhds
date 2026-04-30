// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

// Concurrent Forest registry stress test.
//
// Forest is required to be safe under multi-threaded access on the registry
// (create_io / find_io / create_tree on different IOs). The Tree bodies
// themselves remain single-threaded per pointer, so each thread in this test
// owns its slot's Tree exclusively. Build with --config=tsan to actually
// catch races; under a normal build it just exercises the lock paths.

#include <gtest/gtest.h>

#include <atomic>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "hhds/tree.hpp"

namespace {

constexpr int kThreads          = 8;
constexpr int kIosPerThread     = 32;
constexpr int kFindsPerIteration = 16;

}  // namespace

TEST(ForestConcurrency, ParallelCreateIoOnDistinctNames) {
  auto forest = hhds::Forest::create();

  std::atomic<int> ready{0};
  std::atomic<bool> go{false};

  std::vector<std::thread> threads;
  threads.reserve(kThreads);

  for (int t = 0; t < kThreads; ++t) {
    threads.emplace_back([t, &forest, &ready, &go] {
      ready.fetch_add(1, std::memory_order_acq_rel);
      while (!go.load(std::memory_order_acquire)) {
      }
      for (int i = 0; i < kIosPerThread; ++i) {
        const auto name = "t" + std::to_string(t) + "_io" + std::to_string(i);
        auto       tio  = forest->create_io(name);
        ASSERT_NE(tio, nullptr);
        ASSERT_LT(tio->get_tid(), 0);
        auto tree = tio->create_tree();
        ASSERT_NE(tree, nullptr);
        // Body work on this thread's tree only — single-threaded per pointer.
        (void)tree->add_root_node();
      }
    });
  }

  while (ready.load(std::memory_order_acquire) < kThreads) {
  }
  go.store(true, std::memory_order_release);
  for (auto& th : threads) {
    th.join();
  }

  // All IOs must be queryable afterwards.
  for (int t = 0; t < kThreads; ++t) {
    for (int i = 0; i < kIosPerThread; ++i) {
      const auto name = "t" + std::to_string(t) + "_io" + std::to_string(i);
      auto       tio  = forest->find_io(name);
      ASSERT_NE(tio, nullptr) << name;
      auto tree = tio->get_tree();
      ASSERT_NE(tree, nullptr) << name;
    }
  }
}

TEST(ForestConcurrency, ParallelCreateAndFind) {
  auto forest = hhds::Forest::create();

  // Pre-populate so finders always have something to look at.
  for (int t = 0; t < kThreads; ++t) {
    for (int i = 0; i < kIosPerThread; ++i) {
      auto tio = forest->create_io("seed_" + std::to_string(t) + "_" + std::to_string(i));
      (void)tio->create_tree();
    }
  }

  std::atomic<bool> go{false};
  std::atomic<bool> stop{false};

  std::vector<std::thread> writers;
  writers.reserve(kThreads);
  for (int t = 0; t < kThreads; ++t) {
    writers.emplace_back([t, &forest, &go] {
      while (!go.load(std::memory_order_acquire)) {
      }
      for (int i = 0; i < kIosPerThread; ++i) {
        const auto name = "fresh_t" + std::to_string(t) + "_io" + std::to_string(i);
        auto       tio  = forest->create_io(name);
        ASSERT_NE(tio, nullptr);
        auto tree = tio->create_tree();
        ASSERT_NE(tree, nullptr);
        (void)tree->add_root_node();
      }
    });
  }

  std::vector<std::thread> readers;
  readers.reserve(kThreads);
  for (int t = 0; t < kThreads; ++t) {
    readers.emplace_back([t, &forest, &go, &stop] {
      std::mt19937 rng(static_cast<unsigned>(t) * 9173u);
      while (!go.load(std::memory_order_acquire)) {
      }
      while (!stop.load(std::memory_order_acquire)) {
        for (int j = 0; j < kFindsPerIteration; ++j) {
          const auto wt = static_cast<int>(rng() % static_cast<unsigned>(kThreads));
          const auto wi = static_cast<int>(rng() % static_cast<unsigned>(kIosPerThread));
          const auto name = "seed_" + std::to_string(wt) + "_" + std::to_string(wi);
          auto       tio  = forest->find_io(name);
          if (tio) {
            (void)tio->has_tree();
            (void)tio->get_tree();
          }
          (void)forest->find_tree(name);
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

TEST(ForestConcurrency, ParallelCreateAndDeleteDistinctSlots) {
  auto forest = hhds::Forest::create();

  std::atomic<bool> go{false};

  std::vector<std::thread> threads;
  threads.reserve(kThreads);
  for (int t = 0; t < kThreads; ++t) {
    threads.emplace_back([t, &forest, &go] {
      while (!go.load(std::memory_order_acquire)) {
      }
      for (int i = 0; i < kIosPerThread; ++i) {
        const auto name = "churn_t" + std::to_string(t) + "_io" + std::to_string(i);
        auto       tio  = forest->create_io(name);
        ASSERT_NE(tio, nullptr);
        auto tree = tio->create_tree();
        ASSERT_NE(tree, nullptr);
        (void)tree->add_root_node();
        // Drop our own slot — different threads only touch their own names.
        forest->delete_treeio(name);
        ASSERT_EQ(forest->find_io(name), nullptr);
      }
    });
  }
  go.store(true, std::memory_order_release);
  for (auto& th : threads) {
    th.join();
  }
}
