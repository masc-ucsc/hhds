// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "hhds/source_excerpt.hpp"

#include <gtest/gtest.h>
#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <string>

namespace {

namespace fs = std::filesystem;

using hhds::render_excerpt;
using hhds::Source_locator;
using hhds::Source_span;
using hhds::SourceId;

fs::path fresh_dir(const char* name) {
  // pid suffix: concurrent manual runs of this binary must not share dirs.
  const auto dir = fs::temp_directory_path() / (std::string(name) + "_" + std::to_string(getpid()));
  fs::remove_all(dir);
  fs::create_directories(dir);
  return dir;
}

TEST(SourceExcerpt, SingleLineCaret) {
  Source_locator sl;
  sl.set_file_content("src/a.prp", std::string("mut x = 1\ny = x << amt\n"));
  const SourceId id = sl.mint("src/a.prp", 14, 22, 2);  // "x << amt"

  EXPECT_EQ(render_excerpt(sl, sl.resolve_span(id)),
            "2 | y = x << amt\n"
            "  |     ^~~~~~~~");
}

TEST(SourceExcerpt, TabPadKeepsCaretAligned) {
  Source_locator sl;
  sl.set_file_content("src/a.prp", std::string("\tif x {\n}\n"));
  const SourceId id = sl.mint("src/a.prp", 4, 5, 1);  // the "x"

  // The pad reproduces the source tab, so the caret column survives any tab
  // rendering width.
  EXPECT_EQ(render_excerpt(sl, sl.resolve_span(id)),
            "1 | \tif x {\n"
            "  | \t   ^");
}

TEST(SourceExcerpt, LineOnlyAnchorShowsLineWithoutCaret) {
  Source_locator sl;
  sl.set_file_content("src/a.prp", std::string("aaa\nbbb\n"));
  const SourceId id = sl.mint_line("src/a.prp", 2);

  EXPECT_EQ(render_excerpt(sl, sl.resolve_span(id)), "2 | bbb");
}

TEST(SourceExcerpt, DegradesToEmpty) {
  Source_locator sl;
  const SourceId id = sl.mint("src/a.prp", 0, 3, 1);  // no bytes ingested

  EXPECT_EQ(render_excerpt(sl, sl.resolve_span(id)), "");
  EXPECT_EQ(render_excerpt(sl, Source_span{}), "");  // null span

  // A line past EOF means the span does not match these bytes.
  sl.set_file_content("src/a.prp", std::string("one line\n"));
  Source_span far;
  far.file       = "src/a.prp";
  far.start_line = 99;
  EXPECT_EQ(render_excerpt(sl, far), "");
}

TEST(SourceExcerpt, MultiLineSpanElides) {
  Source_locator sl;
  sl.set_file_content("src/a.prp", std::string("l1\nl2\nl3\nl4\nl5\n"));
  const SourceId id = sl.mint("src/a.prp", 0, 14, 1);  // lines 1..5

  // Default cap of 3 span lines; only the first line gets the caret row; the
  // elided remainder shows as a "..." gutter row.
  EXPECT_EQ(render_excerpt(sl, sl.resolve_span(id)),
            "  1 | l1\n"
            "    | ^~\n"
            "  2 | l2\n"
            "  3 | l3\n"
            "... |");

  // A larger cap shows the whole span.
  EXPECT_EQ(render_excerpt(sl, sl.resolve_span(id), "", 8),
            "1 | l1\n"
            "  | ^~\n"
            "2 | l2\n"
            "3 | l3\n"
            "4 | l4\n"
            "5 | l5");
}

TEST(SourceExcerpt, DiskFallbackIsHashValidated) {
  const auto        dir   = fresh_dir("hhds_excerpt_disk");
  const std::string bytes = "x = 1\n";
  std::ofstream(dir / "ex.prp") << bytes;

  Source_locator good;
  const SourceId id = good.mint("ex.prp", 0, 1, 1);
  good.set_file_content_hash("ex.prp", Source_locator::content_hash_of(bytes));
  EXPECT_EQ(render_excerpt(good, good.resolve_span(id), dir.string()),
            "1 | x = 1\n"
            "  | ^");

  // A drifted hash refuses the bytes: no excerpt beats a wrong excerpt.
  Source_locator stale;
  const SourceId sid = stale.mint("ex.prp", 0, 1, 1);
  stale.set_file_content_hash("ex.prp", 0x1234u);
  EXPECT_EQ(render_excerpt(stale, stale.resolve_span(sid), dir.string()), "");

  fs::remove_all(dir);
}

}  // namespace
