// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

// Attr_host's debug-only overlap detector (see the block comment in attr.hpp).
// The detector exists because livehd's hierarchical LEC crashed ~5% of the time
// when one taskflow worker restructured a shared graph's attribute stores while
// its siblings were deep-copying that same graph. The point of this file is the
// two halves of that contract: overlap must abort loudly, and the shapes that
// merely LOOK like sharing -- concurrent readers, and a host handed from one
// thread to the next -- must stay silent, since a detector that fires on correct
// code is worse than no detector at all.

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <istream>
#include <mutex>
#include <ostream>
#include <sstream>
#include <streambuf>
#include <thread>
#include <vector>

#include "hhds/attr.hpp"

namespace {

struct probe_tag {
  using value_type = uint64_t;
  using storage    = hhds::flat_storage;
};

// A tag this host has never seen, so attr_store() must take its cold-path
// write scope to mint the store (the attr_stores_.resize() the file header
// blames for the LEC SIGSEGV).
struct cold_tag {
  using value_type = uint64_t;
  using storage    = hhds::flat_storage;
};

// Attr_host is abstract and its coarse operations are protected; the detector is
// a property of Attr_host itself, so the test drives it directly rather than
// through Graph or Tree.
class Test_host : public hhds::Attr_host {
public:
  using hhds::Attr_host::clone_attr_stores_from;
  using hhds::Attr_host::load_attr_stores;
  using hhds::Attr_host::save_attr_stores;

  [[nodiscard]] hhds::AttrRef<probe_tag> probe(uint64_t raw_id) {
    return hhds::AttrRef<probe_tag>(this, hhds::make_node_attr_key(raw_id));
  }

private:
  void attr_note_modified() noexcept override {}
};

// Streambuf that parks the first write until it is released, so a thread can be
// pinned INSIDE save_attr_stores -- i.e. holding the read scope -- with no
// reliance on scheduling luck.
class Blocking_streambuf : public std::streambuf {
public:
  // Bounded: if the stream ever stops routing its first output through this
  // parker, the caller must fail rather than hang the death test forever.
  void wait_until_inside() {
    std::unique_lock<std::mutex> lk(mu_);
    if (!cv_.wait_for(lk, std::chrono::seconds(30), [this] { return inside_; })) {
      std::fprintf(stderr, "Blocking_streambuf: writer never entered the parked buffer\n");
      std::abort();
    }
  }

protected:
  std::streamsize xsputn(const char*, std::streamsize count) override {
    park();
    return count;
  }

  // save_attr_stores uses os.write() today, which routes to xsputn; park here
  // too so a switch to operator<< / sputc does not silently stop parking.
  int overflow(int ch) override {
    park();
    return traits_type::not_eof(ch);
  }

private:
  void park() {
    {
      std::lock_guard<std::mutex> lk(mu_);
      inside_ = true;
    }
    cv_.notify_all();
    // Never released: the process is expected to abort while we hold the scope.
    std::unique_lock<std::mutex> lk(mu_);
    cv_.wait(lk, [] { return false; });
  }

  std::mutex              mu_;
  std::condition_variable cv_;
  bool                    inside_ = false;
};

// The input-side twin: parks the first READ, pinning a thread inside
// load_attr_stores -- i.e. holding a coarse WRITE scope on the host.
class Blocking_istreambuf : public std::streambuf {
public:
  void wait_until_inside() {
    std::unique_lock<std::mutex> lk(mu_);
    if (!cv_.wait_for(lk, std::chrono::seconds(30), [this] { return inside_; })) {
      std::fprintf(stderr, "Blocking_istreambuf: reader never entered the parked buffer\n");
      std::abort();
    }
  }

protected:
  std::streamsize xsgetn(char*, std::streamsize count) override {
    park();
    return count;
  }

  int underflow() override {
    park();
    return traits_type::eof();
  }

private:
  void park() {
    {
      std::lock_guard<std::mutex> lk(mu_);
      inside_ = true;
    }
    cv_.notify_all();
    std::unique_lock<std::mutex> lk(mu_);
    cv_.wait(lk, [] { return false; });
  }

  std::mutex              mu_;
  std::condition_variable cv_;
  bool                    inside_ = false;
};

}  // namespace

// A host built on one thread and then used on another is a sequential hand-off,
// not an overlap. This is why the detector counts live scopes instead of pinning
// an owner thread to the host: hhds's own concurrency tests and livehd's
// split_selfref worker are both exactly this shape.
TEST(AttrRace, SequentialHandoffAcrossThreadsIsSilent) {
  Test_host host;

  std::thread builder([&host] {
    for (uint64_t i = 0; i < 64; ++i) {
      host.probe(i).set(i + 1);
    }
  });
  builder.join();

  uint64_t    sum = 0;
  std::thread consumer([&host, &sum] {
    for (uint64_t i = 0; i < 64; ++i) {
      sum += host.probe(i).get();
      host.probe(i).set(i + 2);
    }
  });
  consumer.join();

  EXPECT_EQ(sum, 64ULL * 65ULL / 2);
}

// The shape the detector must never break: N workers deep-copying ONE shared
// source host at the same time. That is livehd's hierarchical LEC, and it is
// correct -- readers do not exclude readers.
TEST(AttrRace, ConcurrentClonesOfOneSourceAreSilent) {
  Test_host source;
  for (uint64_t i = 0; i < 256; ++i) {
    source.probe(i).set(i + 1);
  }

  constexpr int          kWorkers = 8;
  std::vector<Test_host> workers(kWorkers);
  std::atomic<int>       ready{0};
  std::atomic<bool>      go{false};

  std::vector<std::thread> threads;
  threads.reserve(kWorkers);
  for (int t = 0; t < kWorkers; ++t) {
    threads.emplace_back([t, &workers, &source, &ready, &go] {
      ready.fetch_add(1, std::memory_order_acq_rel);
      while (!go.load(std::memory_order_acquire)) {
      }
      for (int rep = 0; rep < 32; ++rep) {
        workers[t].clone_attr_stores_from(source);
      }
    });
  }
  while (ready.load(std::memory_order_acquire) < kWorkers) {
  }
  go.store(true, std::memory_order_release);
  for (auto& th : threads) {
    th.join();
  }

  for (auto& worker : workers) {
    EXPECT_EQ(worker.probe(7).get(), 8ULL);
  }
}

// clone_attr_stores_from and load_attr_stores both open with
// discard_attr_stores, so a coarse writer nested inside a coarse writer is
// ordinary single-threaded code. A non-re-entrant detector would abort here on
// every copy, on one thread, deterministically.
TEST(AttrRace, NestedCoarseWritersOnOneThreadAreSilent) {
  Test_host source;
  source.probe(1).set(11);

  Test_host dst;
  dst.probe(2).set(22);
  dst.clone_attr_stores_from(source);  // write scope on dst, nesting discard
  EXPECT_EQ(dst.probe(1).get(), 11ULL);

  std::ostringstream os;
  dst.save_attr_stores(os);  // read scope on dst, right after the write scope
  EXPECT_FALSE(os.str().empty());
}

#ifndef NDEBUG
// The overlap itself. A per-key set() never touches attr_stores_, so no coarse
// scope can see it -- yet the rehash it can trigger frees the very buffer the
// concurrent walk is copying. That is the crash this detector was built for, one
// level below the reported attr_stores_.resize().
TEST(AttrRaceDeathTest, WriteWhileAnotherThreadWalksTheStores) {
  EXPECT_DEATH(
      {
        auto* host = new Test_host();  // leaked on purpose: the reader never returns
        host->probe(1).set(11);

        auto*        buf = new Blocking_streambuf();
        std::ostream parked(buf);
        std::thread  reader([host, &parked] { host->save_attr_stores(parked); });
        reader.detach();
        buf->wait_until_inside();

        host->probe(2).set(22);  // must abort: a walk of this host is in flight
      },
      "attr race");
}

// The ORIGIN crash, and the only test that reaches attr_store()'s cold path:
// one worker walks the shared host's stores while another first-writes a tag on
// it, so attr_stores_.resize() reallocates the vector the walk is indexing.
// Without attr_store()'s write scope this mint is invisible -- a plain
// attr_store() call takes no per-key check to fall back on.
TEST(AttrRaceDeathTest, ColdMintWhileAnotherThreadWalksTheStores) {
  EXPECT_DEATH(
      {
        auto* host = new Test_host();  // leaked on purpose: the reader never returns
        host->probe(1).set(11);

        auto*        buf = new Blocking_streambuf();
        std::ostream parked(buf);
        std::thread  reader([host, &parked] { host->save_attr_stores(parked); });
        reader.detach();
        buf->wait_until_inside();

        (void)host->attr_store(cold_tag{});  // must abort: resizes attr_stores_
      },
      "attr race");
}

// The other half of that pair, from the reader's side: a clone must refuse to
// walk a host another thread is restructuring. The writer is parked inside
// load_attr_stores, which holds the coarse write scope across its whole read.
TEST(AttrRaceDeathTest, CloneWhileAnotherThreadRestructuresTheSource) {
  EXPECT_DEATH(
      {
        auto* source = new Test_host();  // leaked on purpose: the writer never returns
        source->probe(1).set(11);

        auto*        buf = new Blocking_istreambuf();
        std::istream parked(buf);
        std::thread  writer([source, &parked] { source->load_attr_stores(parked); });
        writer.detach();
        buf->wait_until_inside();

        Test_host dst;
        dst.clone_attr_stores_from(*source);  // must abort: source is being rebuilt
      },
      "attr race");
}
#endif
