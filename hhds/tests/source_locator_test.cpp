// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "hhds/source_locator.hpp"

#include <gtest/gtest.h>
#include <unistd.h>

#include <array>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "hhds/graph.hpp"
#include "hhds/tree.hpp"

namespace hhds {

// Test-only seam (friend of Source_locator): narrowing the hash mask makes
// collision/probe/merge-convergence paths reachable — genuine 64-bit rapidhash
// collisions cannot be constructed in a test.
class Source_locator_tester {
public:
  static void set_hash_mask(Source_locator& sl, uint64_t mask) { sl.hash_mask_ = mask; }
};

}  // namespace hhds

namespace {

namespace fs = std::filesystem;

using hhds::Source_locator;
using hhds::Source_locator_tester;
using hhds::SourceId;
using hhds::SourceId_invalid;

using Kind = Source_locator::Anchor_kind;

SourceId combine2(Source_locator& sl, SourceId a, SourceId b) {
  const std::array<SourceId, 2> parents{a, b};
  return sl.combine(parents);
}

fs::path fresh_dir(const char* name) {
  // pid suffix: concurrent manual runs of this binary must not share dirs.
  const auto dir = fs::temp_directory_path() / (std::string(name) + "_" + std::to_string(getpid()));
  fs::remove_all(dir);
  fs::create_directories(dir);
  return dir;
}

TEST(SourceLocator, MintDeterminismAcrossLocators) {
  Source_locator x;
  Source_locator y;
  const SourceId xa = x.mint("src/foo.prp", 10, 25, 3);
  const SourceId ya = y.mint("src/foo.prp", 10, 25, 3);
  EXPECT_EQ(xa, ya);
  EXPECT_NE(xa, SourceId_invalid);

  // Same span re-minted dedups to one entry.
  EXPECT_EQ(x.mint("src/foo.prp", 10, 25, 3), xa);
  EXPECT_EQ(x.size(), 1u);

  // Distinct spans / kinds / paths get distinct ids.
  EXPECT_NE(x.mint("src/foo.prp", 10, 26, 3), xa);
  EXPECT_NE(x.mint("src/bar.prp", 10, 25, 3), xa);
  EXPECT_NE(x.mint_line("src/foo.prp", 3), xa);
}

TEST(SourceLocator, SpanMinterMatchesMintAndDedups) {
  Source_locator viamint;
  Source_locator viaminter;
  viaminter.reserve(8);  // advisory; must not change results

  auto m = viaminter.span_minter("src/foo.prp");
  ASSERT_TRUE(m.valid());

  // span_minter ids are byte-identical to mint(path, ...) for the same span.
  EXPECT_EQ(m.mint(10, 25, 3), viamint.mint("src/foo.prp", 10, 25, 3));
  EXPECT_EQ(m.mint(30, 40, 4), viamint.mint("src/foo.prp", 30, 40, 4));

  // Re-minting a span dedups to the same id and one entry (the leading entry
  // is the interned file's first use; minted spans follow).
  const SourceId again = m.mint(10, 25, 3);
  EXPECT_EQ(again, viamint.mint("src/foo.prp", 10, 25, 3));
  EXPECT_EQ(viaminter.size(), 2u);

  // Resolves back through the same machinery as mint().
  const auto a = viaminter.resolve(m.mint(10, 25, 3));
  ASSERT_TRUE(a.has_value());
  EXPECT_EQ(a->path, "src/foo.prp");
  EXPECT_EQ(a->start_byte, 10u);
  EXPECT_EQ(a->end_byte, 25u);
  EXPECT_EQ(a->line, 3u);
}

TEST(SourceLocator, ResolveRoundTrip) {
  Source_locator sl;
  const SourceId span = sl.mint("a.prp", 5, 9, 2);
  const SourceId line = sl.mint_line("b.v", 42);

  const auto sa = sl.resolve(span);
  ASSERT_TRUE(sa.has_value());
  EXPECT_EQ(sa->path, "a.prp");
  EXPECT_EQ(sa->start_byte, 5u);
  EXPECT_EQ(sa->end_byte, 9u);
  EXPECT_EQ(sa->line, 2u);
  EXPECT_EQ(sa->kind, Kind::Span);

  const auto la = sl.resolve(line);
  ASSERT_TRUE(la.has_value());
  EXPECT_EQ(la->path, "b.v");
  EXPECT_EQ(la->line, 42u);
  EXPECT_EQ(la->kind, Kind::Line_only);

  EXPECT_FALSE(sl.resolve(SourceId{0xdeadbeef}).has_value());
  EXPECT_FALSE(sl.has(SourceId{0xdeadbeef}));
}

TEST(SourceLocator, CombineOrderSignificantAndDedup) {
  Source_locator sl;
  const SourceId a = sl.mint("a.prp", 0, 4, 1);
  const SourceId b = sl.mint("a.prp", 8, 12, 2);

  const SourceId ab  = combine2(sl, a, b);
  const SourceId ab2 = combine2(sl, a, b);
  const SourceId ba  = combine2(sl, b, a);
  EXPECT_EQ(ab, ab2) << "re-minting the same ordered list must dedup";
  EXPECT_NE(ab, ba) << "parents are order-significant (parents[0] is the primary)";
  EXPECT_EQ(sl.size(), 4u);  // a, b, ab, ba

  // Degenerate single-parent combine returns the parent itself.
  const std::array<SourceId, 1> one{a};
  EXPECT_EQ(sl.combine(one), a);

  // resolve follows the primary (first) parent to a concrete anchor.
  const auto pa = sl.resolve(ab);
  ASSERT_TRUE(pa.has_value());
  EXPECT_EQ(pa->start_byte, 0u);
  const auto pb = sl.resolve(ba);
  ASSERT_TRUE(pb.has_value());
  EXPECT_EQ(pb->start_byte, 8u);

  // resolve_all expands first-parent-first.
  const auto all = sl.resolve_all(ab);
  EXPECT_FALSE(all.truncated);
  ASSERT_EQ(all.anchors.size(), 2u);
  EXPECT_EQ(all.anchors[0].start_byte, 0u);
  EXPECT_EQ(all.anchors[1].start_byte, 8u);
}

TEST(SourceLocator, ResolveAllDiamondAndTruncation) {
  Source_locator sl;
  const SourceId a  = sl.mint("a.prp", 0, 1, 1);
  const SourceId b  = sl.mint("a.prp", 2, 3, 1);
  const SourceId c  = sl.mint("a.prp", 4, 5, 1);
  const SourceId d1 = combine2(sl, a, b);
  const SourceId d2 = combine2(sl, a, c);
  const SourceId e  = combine2(sl, d1, d2);

  // Diamond: `a` is reachable twice but reported once.
  const auto all = sl.resolve_all(e);
  EXPECT_FALSE(all.truncated);
  ASSERT_EQ(all.anchors.size(), 3u);
  EXPECT_EQ(all.anchors[0].start_byte, 0u);
  EXPECT_EQ(all.anchors[1].start_byte, 2u);
  EXPECT_EQ(all.anchors[2].start_byte, 4u);

  // Depth cap reports truncation instead of silently dropping anchors.
  const auto capped = sl.resolve_all(e, 1);
  EXPECT_TRUE(capped.truncated);
  EXPECT_LT(capped.anchors.size(), 3u);
}

TEST(SourceLocator, LineColDerivation) {
  Source_locator sl;
  (void)sl.mint("a.prp", 0, 4, 1);
  sl.set_file_line_offsets("a.prp", {0, 10, 25});

  const auto lc0 = sl.to_line_col("a.prp", 0);
  ASSERT_TRUE(lc0.has_value());
  EXPECT_EQ(lc0->line, 1u);
  EXPECT_EQ(lc0->col, 1u);

  const auto lc9 = sl.to_line_col("a.prp", 9);
  ASSERT_TRUE(lc9.has_value());
  EXPECT_EQ(lc9->line, 1u);
  EXPECT_EQ(lc9->col, 10u);

  const auto lc10 = sl.to_line_col("a.prp", 10);
  ASSERT_TRUE(lc10.has_value());
  EXPECT_EQ(lc10->line, 2u);
  EXPECT_EQ(lc10->col, 1u);

  const auto lc30 = sl.to_line_col("a.prp", 30);
  ASSERT_TRUE(lc30.has_value());
  EXPECT_EQ(lc30->line, 3u);
  EXPECT_EQ(lc30->col, 6u);

  EXPECT_FALSE(sl.to_line_col("missing.prp", 0).has_value());
}

TEST(SourceLocator, SetFileContentDerivesMetadata) {
  Source_locator sl;
  sl.set_file_content("src/a.prp", std::string("ab\ncd\n"));

  const auto content = sl.file_content("src/a.prp");
  ASSERT_NE(content, nullptr);
  EXPECT_EQ(*content, "ab\ncd\n");
  EXPECT_EQ(sl.file_content_hash("src/a.prp"), Source_locator::content_hash_of("ab\ncd\n"));

  const auto* offs = sl.file_line_offsets("src/a.prp");
  ASSERT_NE(offs, nullptr);
  EXPECT_EQ(*offs, (std::vector<uint64_t>{0, 3, 6}));
  const auto lc = sl.to_line_col("src/a.prp", 4);  // the 'd'
  ASSERT_TRUE(lc.has_value());
  EXPECT_EQ(lc->line, 2u);
  EXPECT_EQ(lc->col, 2u);

  EXPECT_EQ(sl.file_content("src/missing.prp"), nullptr);
}

TEST(SourceLocator, FileContentSharesAcrossCarryMergeAndBase) {
  Source_locator src;
  src.set_file_content("src/a.prp", std::string("hello\n"));

  // Cross-locator carry is a refcount bump, not a byte copy.
  Source_locator dst;
  dst.set_file_content("src/a.prp", src.file_content("src/a.prp"));
  EXPECT_EQ(dst.file_content("src/a.prp").get(), src.file_content("src/a.prp").get());

  // merge() carries the content pointer with the rest of the file metadata.
  Source_locator merged;
  (void)merged.merge(src);
  EXPECT_EQ(merged.file_content("src/a.prp").get(), src.file_content("src/a.prp").get());

  // file_content looks through the base chain like every other lookup.
  Source_locator chained;
  chained.set_base(&src);
  EXPECT_EQ(chained.file_content("src/a.prp").get(), src.file_content("src/a.prp").get());
}

TEST(SourceLocator, BaseChaining) {
  Source_locator base;
  const SourceId in_base = base.mint("a.prp", 0, 4, 1);

  Source_locator delta;
  delta.set_base(&base);
  EXPECT_TRUE(delta.has(in_base));
  const auto a = delta.resolve(in_base);
  ASSERT_TRUE(a.has_value());
  EXPECT_EQ(a->path, "a.prp");

  // Minting a span that already lives in the base returns the base id and
  // leaves the delta empty.
  EXPECT_EQ(delta.mint("a.prp", 0, 4, 1), in_base);
  EXPECT_TRUE(delta.empty());

  // A fresh span lands in the delta; the base is untouched.
  const SourceId fresh = delta.mint("a.prp", 8, 12, 2);
  EXPECT_TRUE(delta.has(fresh));
  EXPECT_FALSE(base.has(fresh));

  // Combines may reference base parents.
  const SourceId c   = combine2(delta, in_base, fresh);
  const auto     all = delta.resolve_all(c);
  EXPECT_EQ(all.anchors.size(), 2u);
}

TEST(SourceLocator, ProbeRemapOnTrueCollision) {
  Source_locator sl;
  Source_locator_tester::set_hash_mask(sl, 1);  // every hash lands on id 1

  const SourceId p1 = sl.mint("a.prp", 0, 4, 1);
  const SourceId p2 = sl.mint("a.prp", 8, 12, 2);
  EXPECT_EQ(p1, 1u);
  EXPECT_EQ(p2, 2u) << "true collision probes to the next free id";

  // Probed ids are stable for re-mints.
  EXPECT_EQ(sl.mint("a.prp", 0, 4, 1), p1);
  EXPECT_EQ(sl.mint("a.prp", 8, 12, 2), p2);
  EXPECT_EQ(sl.size(), 2u);

  const auto a2 = sl.resolve(p2);
  ASSERT_TRUE(a2.has_value());
  EXPECT_EQ(a2->start_byte, 8u);
}

TEST(SourceLocator, MergeDisjointAndOverlap) {
  Source_locator x;
  Source_locator y;
  const SourceId shared_x = x.mint("a.prp", 0, 4, 1);
  const SourceId only_x   = x.mint("a.prp", 8, 12, 2);
  const SourceId shared_y = y.mint("a.prp", 0, 4, 1);
  const SourceId only_y   = y.mint("b.prp", 1, 2, 1);
  EXPECT_EQ(shared_x, shared_y);

  const auto remap = x.merge(y);
  EXPECT_TRUE(remap.empty()) << "hash-agreeing locators union without remap";
  EXPECT_EQ(x.size(), 3u) << "shared span dedups by payload";
  EXPECT_TRUE(x.has(only_x));
  EXPECT_TRUE(x.has(only_y));
}

TEST(SourceLocator, MergeCrossOrderCollisionConverges) {
  Source_locator x;
  Source_locator y;
  Source_locator_tester::set_hash_mask(x, 1);
  Source_locator_tester::set_hash_mask(y, 1);

  // Same two colliding payloads, probe-resolved in opposite orders.
  const SourceId x1 = x.mint("a.prp", 0, 4, 1);   // id 1
  const SourceId x2 = x.mint("a.prp", 8, 12, 2);  // id 2
  const SourceId y1 = y.mint("a.prp", 8, 12, 2);  // id 1
  const SourceId y2 = y.mint("a.prp", 0, 4, 1);   // id 2
  EXPECT_EQ(x1, y1);
  EXPECT_EQ(x2, y2);

  const auto remap = x.merge(y);
  EXPECT_EQ(x.size(), 2u) << "payload dedup must converge the cross-order collision";
  ASSERT_EQ(remap.size(), 2u);
  EXPECT_EQ(remap.at(y1), x2) << "y's id 1 (span 8-12) lands on x's id 2";
  EXPECT_EQ(remap.at(y2), x1);
}

TEST(SourceLocator, MergeCombineCascade) {
  Source_locator x;
  Source_locator y;
  Source_locator_tester::set_hash_mask(x, 0xF);
  Source_locator_tester::set_hash_mask(y, 0xF);

  // x holds the two anchors in mint order A,B; y minted them in the opposite
  // order, then combined them. If A/B collide cross-order, the combine entry's
  // parent list must be rewritten and the entry re-keyed on merge.
  const SourceId xa = x.mint("a.prp", 0, 4, 1);
  const SourceId xb = x.mint("a.prp", 8, 12, 2);

  const SourceId yb = y.mint("a.prp", 8, 12, 2);
  const SourceId ya = y.mint("a.prp", 0, 4, 1);
  const SourceId yc = combine2(y, ya, yb);

  const auto     remap = x.merge(y);
  const SourceId xc    = remap.count(yc) != 0 ? remap.at(yc) : yc;
  ASSERT_TRUE(x.has(xc));

  const auto all = x.resolve_all(xc);
  ASSERT_EQ(all.anchors.size(), 2u);
  EXPECT_EQ(all.anchors[0].start_byte, 0u) << "primary parent stays the A anchor through the cascade";
  EXPECT_EQ(all.anchors[1].start_byte, 8u);

  // The cascaded combine must agree with a directly-minted combine(A,B).
  EXPECT_EQ(combine2(x, xa, xb), xc);
}

TEST(SourceLocator, MergeFlattensBaseChain) {
  Source_locator base;
  const SourceId a = base.mint("a.prp", 0, 4, 1);

  Source_locator delta;
  delta.set_base(&base);
  const SourceId b = delta.mint("a.prp", 8, 12, 2);
  const SourceId c = combine2(delta, a, b);

  // The union operand is the own-then-base view: base entries (and combine
  // parents that live there) must land in the destination too.
  Source_locator dst;
  const auto     remap = dst.merge(delta);
  EXPECT_TRUE(remap.empty());
  EXPECT_TRUE(dst.has(a));
  EXPECT_TRUE(dst.has(b));
  const auto all = dst.resolve_all(c);
  EXPECT_EQ(all.anchors.size(), 2u);
}

TEST(SourceLocatorPersist, SaveLoadRoundTrip) {
  const auto dir = fresh_dir("hhds_srcloc_roundtrip");

  Source_locator sl;
  const SourceId a = sl.mint("src/a.prp", 0, 4, 1);
  const SourceId b = sl.mint_line("src/b.v", 42);
  const SourceId c = combine2(sl, a, b);
  sl.set_file_content_hash("src/a.prp", 0x1234u);
  sl.set_file_line_offsets("src/a.prp", {0, 10, 25});
  sl.save(dir.string());

  Source_locator loaded;
  ASSERT_TRUE(loaded.load(dir.string()));
  EXPECT_EQ(loaded.size(), sl.size());
  const auto la = loaded.resolve(a);
  ASSERT_TRUE(la.has_value());
  EXPECT_EQ(la->path, "src/a.prp");
  EXPECT_EQ(la->end_byte, 4u);
  EXPECT_EQ(loaded.file_content_hash("src/a.prp"), 0x1234u);
  const auto lc = loaded.to_line_col("src/a.prp", 10);
  ASSERT_TRUE(lc.has_value());
  EXPECT_EQ(lc->line, 2u);
  const auto all = loaded.resolve_all(c);
  ASSERT_EQ(all.anchors.size(), 2u);
  EXPECT_EQ(all.anchors[0].path, "src/a.prp");

  // Loaded state is payload-identical: merging it back is a pure no-op.
  Source_locator again;
  (void)again.merge(sl);
  const auto remap = again.merge(loaded);
  EXPECT_TRUE(remap.empty());
  EXPECT_EQ(again.size(), sl.size());

  // An empty locator's save removes the stale table.
  Source_locator empty;
  empty.save(dir.string());
  EXPECT_FALSE(fs::exists(dir / "srcmap.txt"));

  fs::remove_all(dir);
}

TEST(SourceLocatorPersist, LoadFileContentValidatesHash) {
  const auto        dir = fresh_dir("hhds_srcloc_content");
  // The "workspace": a real source file under `root`, ingested whole.
  const std::string rel = "a.prp";
  {
    std::ofstream ofs(dir / rel);
    ofs << "mut x = 1\n";
  }
  Source_locator sl;
  (void)sl.mint(rel, 0, 9, 1);
  sl.set_file_content(rel, std::string("mut x = 1\n"));
  sl.save(dir.string());

  Source_locator loaded;
  ASSERT_TRUE(loaded.load(dir.string()));
  EXPECT_EQ(loaded.file_content(rel), nullptr);  // bytes are never persisted
  const auto bytes = loaded.load_file_content(rel, dir.string());
  ASSERT_NE(bytes, nullptr);
  EXPECT_EQ(*bytes, "mut x = 1\n");

  // In-memory content short-circuits the disk read.
  const auto live = sl.load_file_content(rel, dir.string());
  ASSERT_NE(live, nullptr);
  EXPECT_EQ(live.get(), sl.file_content(rel).get());

  // Drifted source: hash mismatch returns nullptr, never the wrong bytes.
  {
    std::ofstream ofs(dir / rel);
    ofs << "mut x = 2\n";
  }
  EXPECT_EQ(loaded.load_file_content(rel, dir.string()), nullptr);

  // No recorded hash: nothing to validate against, so no content.
  Source_locator nohash;
  (void)nohash.mint(rel, 0, 9, 1);
  EXPECT_EQ(nohash.load_file_content(rel, dir.string()), nullptr);

  fs::remove_all(dir);
}

TEST(SourceLocatorPersist, PathsWithSpacesSurvive) {
  const auto dir = fresh_dir("hhds_srcloc_spaces");

  Source_locator sl;
  const SourceId a = sl.mint("dir with space/a b.prp", 3, 7, 1);
  sl.save(dir.string());

  Source_locator loaded;
  ASSERT_TRUE(loaded.load(dir.string()));
  const auto la = loaded.resolve(a);
  ASSERT_TRUE(la.has_value());
  EXPECT_EQ(la->path, "dir with space/a b.prp");

  fs::remove_all(dir);
}

TEST(SourceLocatorGraph, LibrarySaveLoadCarriesProvenance) {
  const auto dir = fresh_dir("hhds_srcloc_graphlib");

  SourceId span = SourceId_invalid;
  {
    hhds::GraphLibrary lib;
    auto               gio = lib.create_io("alu");
    gio->add_input("x", 0);
    gio->add_output("y", 0);
    {
      auto g = gio->create_graph();
      span   = g->source_locator().mint("src/alu.prp", 100, 140, 7);
      auto n = g->create_node();
      n.attr(hhds::attrs::srcid).set(span);
    }
    lib.save(dir.string());

    // The delta folded into the library base at save; resolution still works
    // through the graph (own-then-base chain).
    auto g = lib.find_io("alu")->get_graph();
    ASSERT_TRUE(g);
    EXPECT_TRUE(g->source_locator().empty());
    EXPECT_TRUE(g->source_locator().has(span));
    EXPECT_TRUE(lib.source_map().has(span));
  }

  hhds::GraphLibrary lib2;
  lib2.load(dir.string());
  auto g2 = lib2.find_io("alu")->get_graph();
  ASSERT_TRUE(g2);
  bool found = false;
  for (auto node : g2->fast_class()) {
    if (!node.attr(hhds::attrs::srcid).has()) {
      continue;
    }
    found             = true;
    const SourceId id = node.attr(hhds::attrs::srcid).get();
    EXPECT_EQ(id, span);
    const auto a = g2->source_locator().resolve(id);
    ASSERT_TRUE(a.has_value());
    EXPECT_EQ(a->path, "src/alu.prp");
    EXPECT_EQ(a->start_byte, 100u);
    EXPECT_EQ(a->line, 7u);
  }
  EXPECT_TRUE(found) << "srcid attribute must ride the body save/load";

  fs::remove_all(dir);
}

TEST(SourceLocatorGraph, LoadMergeUnionsSourceMaps) {
  const auto base = fresh_dir("hhds_srcloc_merge");
  const auto dirA = (base / "A").string();
  const auto dirB = (base / "B").string();

  SourceId shared_a = SourceId_invalid;
  SourceId only_a   = SourceId_invalid;
  SourceId only_b   = SourceId_invalid;
  {
    hhds::GraphLibrary a;
    auto               gio = a.create_io("foo");
    {
      auto g   = gio->create_graph();
      shared_a = g->source_locator().mint("src/common.prp", 0, 9, 1);
      only_a   = g->source_locator().mint("src/foo.prp", 5, 9, 2);
      auto n   = g->create_node();
      n.attr(hhds::attrs::srcid).set(only_a);
    }
    a.save(dirA);
  }
  {
    hhds::GraphLibrary b;
    auto               gio = b.create_io("bar");
    {
      auto           g        = gio->create_graph();
      const SourceId shared_b = g->source_locator().mint("src/common.prp", 0, 9, 1);
      EXPECT_EQ(shared_b, shared_a) << "same span, same id, in a separately-built library";
      only_b = g->source_locator().mint("src/bar.prp", 2, 6, 1);
      auto n = g->create_node();
      n.attr(hhds::attrs::srcid).set(only_b);
    }
    b.save(dirB);
  }

  hhds::GraphLibrary c;
  c.load_merge(dirA);
  c.load_merge(dirB);
  EXPECT_TRUE(c.source_map().has(shared_a));
  EXPECT_TRUE(c.source_map().has(only_a));
  EXPECT_TRUE(c.source_map().has(only_b));

  auto gfoo = c.find_io("foo")->get_graph();
  auto gbar = c.find_io("bar")->get_graph();
  ASSERT_TRUE(gfoo && gbar);
  const auto afoo = gfoo->source_locator().resolve(only_a);
  ASSERT_TRUE(afoo.has_value());
  EXPECT_EQ(afoo->path, "src/foo.prp");
  const auto abar = gbar->source_locator().resolve(only_b);
  ASSERT_TRUE(abar.has_value());
  EXPECT_EQ(abar->path, "src/bar.prp");

  // The merged library re-saves with one union table.
  const auto dirC = (base / "C").string();
  c.save(dirC);
  hhds::GraphLibrary d;
  d.load(dirC);
  EXPECT_TRUE(d.source_map().has(shared_a));
  EXPECT_TRUE(d.source_map().has(only_a));
  EXPECT_TRUE(d.source_map().has(only_b));

  fs::remove_all(base);
}

TEST(SourceLocator, InitializerListCombine) {
  Source_locator sl;
  const SourceId a = sl.mint("a.prp", 0, 4, 1);
  const SourceId b = sl.mint("a.prp", 8, 12, 2);
  EXPECT_EQ(sl.combine({a, b}), combine2(sl, a, b));
}

TEST(SourceLocator, DeepCombineChainResolves) {
  Source_locator sl;
  const SourceId a    = sl.mint("a.prp", 0, 4, 1);
  SourceId       head = a;
  for (uint32_t i = 1; i <= 40; ++i) {
    head = sl.combine({head, sl.mint("a.prp", i * 100, i * 100 + 4, i)});
  }
  // resolve() follows the primary parent without an arbitrary depth cap.
  const auto pa = sl.resolve(head);
  ASSERT_TRUE(pa.has_value());
  EXPECT_EQ(pa->start_byte, 0u);
  // resolve_all() honors its depth cap and reports the truncation...
  const auto capped = sl.resolve_all(head);
  EXPECT_TRUE(capped.truncated);
  // ...and a sufficient cap recovers all 41 anchors.
  const auto full = sl.resolve_all(head, 64);
  EXPECT_FALSE(full.truncated);
  EXPECT_EQ(full.anchors.size(), 41u);
}

TEST(SourceLocator, SpanLineDisagreementDoesNotSplitIdentity) {
  // Span identity is (path, start, end); the stored line is derived data —
  // first writer wins, the id never diverges.
  Source_locator sl;
  const SourceId a = sl.mint("a.prp", 0, 4, 1);
  const SourceId b = sl.mint("a.prp", 0, 4, 9);
  EXPECT_EQ(a, b);
  EXPECT_EQ(sl.size(), 1u);
  EXPECT_EQ(sl.resolve(a)->line, 1u);
}

TEST(SourceLocatorPersist, MalformedTablesAreRejectedWhole) {
  const auto dir = fresh_dir("hhds_srcloc_corrupt");

  const auto try_load = [&dir](std::string_view body) {
    {
      std::ofstream ofs(dir / "srcmap.txt");
      ofs << body;
    }
    Source_locator sl;
    const bool     ok = sl.load(dir.string());
    if (!ok) {
      EXPECT_TRUE(sl.empty()) << "a rejected table must leave the locator empty";
    }
    return ok;
  };

  EXPECT_FALSE(try_load("not_a_srcmap 1\n"));
  EXPECT_FALSE(try_load("hhds_srcmap 1\nfile 0 a.prp\ncombine 5 0\n"));                      // empty combine
  EXPECT_FALSE(try_load("hhds_srcmap 1\nfile 0 a.prp\ncombine 5 2 77 78\n"));                // dangling parents
  EXPECT_FALSE(try_load("hhds_srcmap 1\nspan 9 99 0 0 1\n"));                                // undeclared file id
  EXPECT_FALSE(try_load("hhds_srcmap 1\nfile 0 a.prp\nspan 7 0 0 4 1\nspan 7 0 8 12 2\n"));  // duplicate id
  EXPECT_FALSE(try_load("hhds_srcmap 1\nfile x a.prp\n"));                                   // non-numeric fid
  EXPECT_FALSE(try_load("hhds_srcmap 1\nfile 0 a.prp\nfilelines 0 3 0 10\n"));               // short offset list
  EXPECT_FALSE(try_load("hhds_srcmap 1\nfile 0 a.prp\nfilelines 0 3 0 25 10\n"));            // non-ascending
  EXPECT_FALSE(try_load("hhds_srcmap 1\nfile 0 a.prp\nwhatever 1 2 3\n"));                   // unknown record
  // Untrusted-count/id guards: these must degrade to "rejected", never wrap an
  // index out of bounds or attempt a multi-GB allocation.
  EXPECT_FALSE(try_load("hhds_srcmap 1\nfile 4294967295 a.prp\n"));                                // fid wrap / OOB resize
  EXPECT_FALSE(try_load("hhds_srcmap 1\nfile 0 a.prp\nfilelines 0 18446744073709551615 0 10\n"));  // huge filelines count
  EXPECT_FALSE(
      try_load("hhds_srcmap 1\nfile 0 a.prp\nspan 7 0 0 4 1\ncombine 9 18446744073709551615 7 7\n"));  // huge combine count
  EXPECT_TRUE(try_load("hhds_srcmap 1\nfile 0 a.prp\nspan 7 0 0 4 1\ncombine 9 2 7 7\n"));             // valid control

  fs::remove_all(dir);
}

TEST(SourceLocatorGraph, SaveFoldRemapRewritesStampedAttrs) {
  const auto dir = fresh_dir("hhds_srcloc_fold_remap");

  // Narrow the graph delta's hash so its two mints probe-collide onto the tiny
  // ids 1 and 2; the library-level fold re-mints them at canonical full-width
  // ids, forcing a non-empty remap that must rewrite the stamped attr values.
  hhds::GraphLibrary lib;
  auto               gio = lib.create_io("alu");
  {
    auto g = gio->create_graph();
    Source_locator_tester::set_hash_mask(g->source_locator(), 1);
    const SourceId s1 = g->source_locator().mint("src/a.prp", 0, 4, 1);
    const SourceId s2 = g->source_locator().mint("src/a.prp", 8, 12, 2);
    ASSERT_EQ(s1, 1u);
    ASSERT_EQ(s2, 2u);
    auto n1 = g->create_node();
    auto n2 = g->create_node();
    n1.attr(hhds::attrs::srcid).set(s1);
    n2.attr(hhds::attrs::srcid).set(s2);
  }
  lib.save(dir.string());

  // The canonical ids the fold must have remapped onto.
  Source_locator reference;
  const SourceId c1 = reference.mint("src/a.prp", 0, 4, 1);
  const SourceId c2 = reference.mint("src/a.prp", 8, 12, 2);

  hhds::GraphLibrary lib2;
  lib2.load(dir.string());
  auto g2 = lib2.find_io("alu")->get_graph();
  ASSERT_TRUE(g2);
  std::vector<SourceId> stamped;
  for (auto node : g2->fast_class()) {
    if (node.attr(hhds::attrs::srcid).has()) {
      stamped.push_back(node.attr(hhds::attrs::srcid).get());
    }
  }
  ASSERT_EQ(stamped.size(), 2u);
  std::sort(stamped.begin(), stamped.end());
  std::vector<SourceId> expected{c1, c2};
  std::sort(expected.begin(), expected.end());
  EXPECT_EQ(stamped, expected) << "fold remap must rewrite the saved attr values to the canonical ids";
  const auto a1 = g2->source_locator().resolve(c1);
  ASSERT_TRUE(a1.has_value());
  EXPECT_EQ(a1->start_byte, 0u);
  const auto a2 = g2->source_locator().resolve(c2);
  ASSERT_TRUE(a2.has_value());
  EXPECT_EQ(a2->start_byte, 8u);

  fs::remove_all(dir);
}

TEST(SourceLocatorGraph, LoadMergeRemapRewritesAbsorbedAttrs) {
  const auto base = fresh_dir("hhds_srcloc_merge_remap");
  const auto dirA = (base / "A").string();

  SourceId orig = SourceId_invalid;
  {
    hhds::GraphLibrary a;
    auto               gio = a.create_io("foo");
    {
      auto g = gio->create_graph();
      orig   = g->source_locator().mint("src/foo.prp", 5, 9, 2);
      auto n = g->create_node();
      n.attr(hhds::attrs::srcid).set(orig);
    }
    a.save(dirA);
  }

  // A destination whose hash space is collapsed re-keys every incoming entry,
  // so the absorbed body's attr values must be rewritten through the remap.
  hhds::GraphLibrary c;
  Source_locator_tester::set_hash_mask(c.source_map(), 1);
  c.load_merge(dirA);

  auto g = c.find_io("foo")->get_graph();
  ASSERT_TRUE(g);
  bool found = false;
  for (auto node : g->fast_class()) {
    if (!node.attr(hhds::attrs::srcid).has()) {
      continue;
    }
    found             = true;
    const SourceId id = node.attr(hhds::attrs::srcid).get();
    EXPECT_NE(id, orig) << "collapsed destination hash space must have re-keyed the id";
    const auto a = c.source_map().resolve(id);
    ASSERT_TRUE(a.has_value());
    EXPECT_EQ(a->path, "src/foo.prp");
    EXPECT_EQ(a->start_byte, 5u);
  }
  EXPECT_TRUE(found);

  fs::remove_all(base);
}

TEST(SourceLocatorForest, SaveLoadAndResaveCarriesProvenance) {
  const auto dir1 = fresh_dir("hhds_srcloc_forest1");
  const auto dir2 = fresh_dir("hhds_srcloc_forest2");

  SourceId span = SourceId_invalid;
  {
    // The working locator belongs to the artifact wrapper (livehd's Lnast); the
    // caller unions it into the forest before save. Modeled here with a
    // standalone unit locator.
    Source_locator unit;
    span = unit.mint("src/top.prp", 12, 30, 2);

    auto forest = hhds::Forest::create();
    auto tio    = forest->create_io("top");
    {
      auto tree = tio->create_tree();
      auto root = tree->add_root_node();
      root.attr(hhds::attrs::srcid).set(span);
      tree->commit();
    }
    const auto remap = forest->source_map().merge(unit);
    EXPECT_TRUE(remap.empty());
    forest->save(dir1.string());
  }

  auto loaded = hhds::Forest::create();
  loaded->load(dir1.string());
  EXPECT_TRUE(loaded->source_map().has(span));
  {
    auto tree = loaded->find_tree("top");
    ASSERT_TRUE(tree);
    const auto root = tree->get_root_node();
    ASSERT_TRUE(root.attr(hhds::attrs::srcid).has());
    EXPECT_EQ(root.attr(hhds::attrs::srcid).get(), span);
    const auto a = loaded->source_map().resolve(root.attr(hhds::attrs::srcid).get());
    ASSERT_TRUE(a.has_value());
    EXPECT_EQ(a->path, "src/top.prp");
    EXPECT_EQ(a->start_byte, 12u);
  }

  // load -> no-op -> save must not drop provenance (the table is rewritten in
  // full, independent of the dirty-skip on clean tree bodies).
  loaded->save(dir2.string());
  auto reloaded = hhds::Forest::create();
  reloaded->load(dir2.string());
  EXPECT_TRUE(reloaded->source_map().has(span));

  fs::remove_all(dir1);
  fs::remove_all(dir2);
}

TEST(SourceLocator, ResolveSpanLineCol) {
  Source_locator sl;
  sl.set_file_content("src/a.prp", std::string("mut x = 1\ny = x << amt\n"));
  const SourceId id = sl.mint("src/a.prp", 14, 22, 2);  // "x << amt" on line 2

  const auto span = sl.resolve_span(id);
  EXPECT_FALSE(span.is_null());
  EXPECT_EQ(span.source_id.value_or(0), id);
  EXPECT_EQ(span.file, "src/a.prp");
  EXPECT_EQ(span.start_byte.value_or(0), 14u);
  EXPECT_EQ(span.end_byte.value_or(0), 22u);
  EXPECT_EQ(span.start_line.value_or(0), 2u);
  EXPECT_EQ(span.start_col.value_or(0), 5u);
  EXPECT_EQ(span.end_line.value_or(0), 2u);
  EXPECT_EQ(span.end_col.value_or(0), 13u);  // exclusive

  // Without a line-offset table the span still carries bytes + minted line.
  Source_locator bare;
  const auto     bspan = bare.resolve_span(bare.mint("src/a.prp", 14, 22, 2));
  EXPECT_EQ(bspan.start_line.value_or(0), 2u);
  EXPECT_FALSE(bspan.start_col.has_value());

  // Line-only anchors resolve to a line with no byte range.
  const auto lspan = sl.resolve_span(sl.mint_line("src/b.v", 42));
  EXPECT_EQ(lspan.start_line.value_or(0), 42u);
  EXPECT_FALSE(lspan.start_byte.has_value());
}

TEST(SourceLocator, ResolveSpanUnknownIsNull) {
  Source_locator sl;
  EXPECT_TRUE(sl.resolve_span(SourceId_invalid).is_null());
  EXPECT_TRUE(sl.resolve_span(SourceId{0xdeadbeefu}).is_null());
  EXPECT_TRUE(sl.resolve_spans(SourceId{0xdeadbeefu}).primary.is_null());
}

TEST(SourceLocator, ResolveSpansCombined) {
  Source_locator sl;
  sl.set_file_content("src/a.prp", std::string("aaa\nbbb\n"));
  const SourceId a    = sl.mint("src/a.prp", 0, 3, 1);
  const SourceId b    = sl.mint("src/a.prp", 4, 7, 2);
  const SourceId comb = combine2(sl, a, b);

  const auto rs = sl.resolve_spans(comb);
  EXPECT_FALSE(rs.truncated);
  // Primary = first parent; every span keeps the resolving (combined) id.
  EXPECT_EQ(rs.primary.start_byte.value_or(99), 0u);
  EXPECT_EQ(rs.primary.source_id.value_or(0), comb);
  ASSERT_EQ(rs.related.size(), 1u);
  EXPECT_EQ(rs.related[0].start_line.value_or(0), 2u);
  EXPECT_EQ(rs.related[0].source_id.value_or(0), comb);

  // A plain anchor has no related spans.
  EXPECT_TRUE(sl.resolve_spans(a).related.empty());
}

TEST(SourceLocator, ImportFromReMintsAndCarriesContent) {
  Source_locator src;
  src.set_file_content("src/i.prp", std::string("aaa\nbbb\n"));
  const SourceId sa   = src.mint("src/i.prp", 0, 3, 1);
  const SourceId sb   = src.mint("src/i.prp", 4, 7, 2);
  const SourceId comb = combine2(src, sa, sb);

  Source_locator dst;
  // Payload-hashed ids re-mint to the SAME id; the combine re-mints too.
  EXPECT_EQ(dst.import_from(src, sa), sa);
  EXPECT_EQ(dst.import_from(src, comb), comb);
  EXPECT_TRUE(dst.has(sb));  // flattening minted the second parent as well

  // The bytes rode along (shared pointer), so line:col resolves in dst too.
  EXPECT_EQ(dst.file_content("src/i.prp"), src.file_content("src/i.prp"));
  const auto rs = dst.resolve_spans(comb);
  EXPECT_EQ(rs.primary.start_col.value_or(0), 1u);
  ASSERT_EQ(rs.related.size(), 1u);
  EXPECT_EQ(rs.related[0].start_line.value_or(0), 2u);

  // Re-import is a no-op (already present).
  EXPECT_EQ(dst.import_from(src, comb), comb);
}

TEST(SourceLocator, ImportFromCarriesMetadataWithoutContent) {
  Source_locator src;
  src.set_file_line_offsets("src/m.prp", {0, 10, 25});
  src.set_file_content_hash("src/m.prp", 0xfeedu);
  const SourceId s = src.mint("src/m.prp", 12, 14, 2);

  Source_locator dst;
  EXPECT_EQ(dst.import_from(src, s), s);
  const auto* offs = dst.file_line_offsets("src/m.prp");
  ASSERT_NE(offs, nullptr);
  EXPECT_EQ(offs->size(), 3u);
  EXPECT_EQ(dst.file_content_hash("src/m.prp"), 0xfeedu);  // disk fallback still validates
  EXPECT_EQ(dst.file_content("src/m.prp"), nullptr);
  EXPECT_EQ(dst.resolve_span(s).start_col.value_or(0), 3u);
}

TEST(SourceLocator, ImportFromEdgeCases) {
  Source_locator src;
  Source_locator dst;
  EXPECT_EQ(dst.import_from(src, SourceId_invalid), SourceId_invalid);
  EXPECT_EQ(dst.import_from(src, SourceId{12345u}), SourceId_invalid);  // unknown in src

  const SourceId s = dst.mint("src/a.prp", 1, 2, 1);
  EXPECT_EQ(dst.import_from(dst, s), s);  // self-import is a no-op
}

// ---- R3: per-file re-run / incremental eviction --------------------------------

TEST(SourceLocator, BeginFileShortCircuitsAndRefreshesInPlace) {
  Source_locator sl;
  sl.set_file_content("f.v", std::string("hello\nworld\n"));
  const SourceId id = sl.mint("f.v", 0, 5, 1);

  EXPECT_TRUE(sl.file_unchanged("f.v", "hello\nworld\n"));
  EXPECT_FALSE(sl.file_unchanged("f.v", "hello\nWORLD\n"));

  // Matching bytes => skip re-parse, mutate nothing.
  const auto txn = sl.begin_file("f.v", std::string("hello\nworld\n"));
  EXPECT_TRUE(txn.unchanged);
  EXPECT_TRUE(sl.has(id));

  // Changed bytes => the prior generation's own span is dropped and metadata refreshed.
  const auto txn2 = sl.begin_file("f.v", std::string("HELLO\nworld\n"));
  EXPECT_FALSE(txn2.unchanged);
  EXPECT_FALSE(sl.has(id)) << "prior generation's span evicted in place";
  EXPECT_EQ(sl.file_content_hash("f.v"), Source_locator::content_hash_of("HELLO\nworld\n"));
}

TEST(SourceLocator, AbortFileDropsEntriesAndMetadata) {
  Source_locator sl;
  const auto     txn = sl.begin_file("f.v", std::string("aaaa\n"));
  ASSERT_FALSE(txn.unchanged);
  const SourceId id = sl.mint("f.v", 0, 4, 1);
  ASSERT_TRUE(sl.has(id));

  sl.abort_file(txn.file_id);
  EXPECT_FALSE(sl.has(id)) << "aborted file's spans dropped";
  EXPECT_EQ(sl.file_content_hash("f.v"), 0u) << "aborted file's metadata cleared";
}

TEST(SourceLocator, RerunEditedFileEvictsStaleSpansViaMerge) {
  // Base = the accumulated shared/library map after run 1 (two files).
  Source_locator base;
  base.set_file_content("f1.v", std::string("aaaa\nbbbb\n"));
  base.set_file_content("f2.v", std::string("xx\n"));
  const SourceId f1_old  = base.mint("f1.v", 0, 4, 1);
  const SourceId f2_keep = base.mint("f2.v", 0, 2, 1);
  const uint64_t h1      = base.file_content_hash("f1.v");

  // Run 2: f1 edited (bytes + offsets shift), f2 untouched. The producer
  // ingests the new bytes and re-mints f1 into a working locator, then folds.
  Source_locator work;
  work.set_base(&base);
  const auto txn = work.begin_file("f1.v", std::string("HEADER\naaaa\nbbbb\n"));
  EXPECT_FALSE(txn.unchanged);
  const SourceId f1_new = work.mint("f1.v", 7, 11, 2);  // shifted -> a different id
  EXPECT_NE(f1_new, f1_old);

  (void)base.merge(work);

  EXPECT_FALSE(base.has(f1_old)) << "stale span from the prior generation evicted on fold";
  EXPECT_TRUE(base.has(f1_new));
  EXPECT_TRUE(base.has(f2_keep)) << "an unrelated file is never touched by a re-run";
  EXPECT_NE(base.file_content_hash("f1.v"), h1) << "file metadata refreshed, not pinned to first ingest";
  EXPECT_EQ(base.file_content_hash("f1.v"), Source_locator::content_hash_of("HEADER\naaaa\nbbbb\n"));
}

TEST(SourceLocator, RerunEvictsDependentCombineViaMerge) {
  Source_locator base;
  base.set_file_content("f1.v", std::string("aaaa\n"));
  base.set_file_content("f2.v", std::string("bbbb\n"));
  const SourceId a = base.mint("f1.v", 0, 4, 1);
  const SourceId b = base.mint("f2.v", 0, 4, 1);
  const SourceId c = combine2(base, a, b);  // combine spanning BOTH files
  ASSERT_TRUE(base.has(c));

  Source_locator work;
  work.set_base(&base);
  (void)work.begin_file("f1.v", std::string("XXXX\naaaa\n"));
  (void)work.mint("f1.v", 5, 9, 2);
  (void)base.merge(work);

  EXPECT_FALSE(base.has(a)) << "f1 old span evicted";
  EXPECT_FALSE(base.has(c)) << "combine depending on the re-run file is cascade-evicted";
  EXPECT_TRUE(base.has(b)) << "the combine's f2 parent (untouched file) survives";
}

TEST(SourceLocator, RerunDanglingCombineIsPurgedAndTableStaysLoadable) {
  // A working locator declares f1 edited but builds a combine over the OLD
  // base-resident f1 span (a cross-generation reference). The fold evicts that
  // span; the incoming combine would dangle. It must be purged, not persisted —
  // else load_table would reject the whole table on reload.
  Source_locator base;
  base.set_file_content("f1.v", std::string("aaaa\n"));
  base.set_file_content("f2.v", std::string("bbbb\n"));
  const SourceId a = base.mint("f1.v", 0, 4, 1);
  const SourceId b = base.mint("f2.v", 0, 4, 1);

  Source_locator work;
  work.set_base(&base);
  (void)work.begin_file("f1.v", std::string("XXXX\naaaa\n"));
  const SourceId c = combine2(work, a, b);  // names the soon-to-be-evicted `a`
  (void)base.merge(work);

  EXPECT_FALSE(base.has(a)) << "edited file's old span evicted";
  EXPECT_FALSE(base.has(c)) << "dangling combine purged, not persisted";
  EXPECT_TRUE(base.has(b));

  namespace fs   = std::filesystem;
  const auto dir = (fs::temp_directory_path() / "hhds_dangling_combine_test").string();
  fs::remove_all(dir);
  base.save(dir);
  Source_locator reloaded;
  EXPECT_TRUE(reloaded.load(dir)) << "purged-combine table reloads cleanly (no whole-table reject)";
  EXPECT_TRUE(reloaded.has(b));
  fs::remove_all(dir);
}

TEST(SourceLocator, MergeKeepsLineOffsetsWhenIncomingCarriesHashOnly) {
  Source_locator base;
  base.set_file_content("f.v", std::string("ab\ncd\n"));  // offsets {0,3,6}
  (void)base.mint("f.v", 0, 2, 1);
  ASSERT_NE(base.file_line_offsets("f.v"), nullptr);

  // Metadata-only carry: same file, same hash, but no line-offset table (the
  // shape import_from produces for a hash-less file). It must not clobber base.
  Source_locator carry;
  carry.set_file_content_hash("f.v", Source_locator::content_hash_of("ab\ncd\n"));
  (void)carry.mint("f.v", 4, 5, 2);

  (void)base.merge(carry);

  const auto* offs = base.file_line_offsets("f.v");
  ASSERT_NE(offs, nullptr) << "merge must not drop valid line offsets";
  EXPECT_EQ(*offs, (std::vector<uint64_t>{0, 3, 6}));
}

}  // namespace
