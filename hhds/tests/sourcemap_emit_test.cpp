// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "hhds/sourcemap_emit.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "hhds/source_locator.hpp"

namespace {

using hhds::Source_locator;
using hhds::SourceId;
using hhds::SourceId_invalid;
using hhds::sourcemap::Segment;
using hhds::sourcemap::to_json;

// ---- minimal v3 readers (decode what to_json encodes) -----------------------

int b64_index(char c) {
  constexpr std::string_view tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  const auto                 p   = tbl.find(c);
  return p == std::string_view::npos ? -1 : static_cast<int>(p);
}

int64_t decode_vlq(std::string_view s, size_t& i) {
  uint64_t v     = 0;
  int      shift = 0;
  for (;;) {
    EXPECT_LT(i, s.size());
    const int d = b64_index(s[i++]);
    EXPECT_GE(d, 0);
    v     |= static_cast<uint64_t>(d & 0x1f) << shift;
    shift += 5;
    if ((d & 0x20) == 0) {
      break;
    }
  }
  const bool neg   = (v & 1) != 0;
  v              >>= 1;
  return neg ? -static_cast<int64_t>(v) : static_cast<int64_t>(v);
}

struct Dseg {
  uint32_t gen_line;
  uint32_t gen_col;
  uint32_t src_idx;
  uint32_t src_line;
  uint32_t src_col;
  bool     operator==(const Dseg&) const = default;
};

std::vector<Dseg> decode_mappings(std::string_view m) {
  std::vector<Dseg> out;
  uint32_t          gline = 0;
  int64_t           col = 0, src = 0, sline = 0, scol = 0;
  size_t            i = 0;
  while (i < m.size()) {
    if (m[i] == ';') {
      ++gline;
      col = 0;  // generated column resets per line
      ++i;
      continue;
    }
    if (m[i] == ',') {
      ++i;
      continue;
    }
    col   += decode_vlq(m, i);
    src   += decode_vlq(m, i);
    sline += decode_vlq(m, i);
    scol  += decode_vlq(m, i);
    out.push_back(
        {gline, static_cast<uint32_t>(col), static_cast<uint32_t>(src), static_cast<uint32_t>(sline), static_cast<uint32_t>(scol)});
  }
  return out;
}

// Raw value of a JSON string field; only valid for fields whose value cannot
// contain an (escaped) quote — fine for "mappings".
std::string json_string_field(const std::string& json, std::string_view key) {
  const std::string needle = "\"" + std::string(key) + "\":\"";
  const auto        pos    = json.find(needle);
  EXPECT_NE(pos, std::string::npos) << "missing field " << key;
  const auto start = pos + needle.size();
  const auto end   = json.find('"', start);
  EXPECT_NE(end, std::string::npos);
  return json.substr(start, end - start);
}

// ---- tests -------------------------------------------------------------------

TEST(SourcemapEmit, ResolvesSortsAndDrops) {
  Source_locator sl;

  // a.prp gets full content: span anchors resolve to exact line:col.
  // Lines start at bytes 0, 10, 20.
  sl.set_file_content("a.prp", std::string("mut x = 1\nmut y = 2\nout = x+y\n"));
  const SourceId a0 = sl.mint("a.prp", 0, 9, 1);    // line 1 col 1
  const SourceId a1 = sl.mint("a.prp", 14, 19, 2);  // line 2 col 5

  // b.prp is line-only ingress (no bytes): the recorded line, column 0.
  const SourceId bl = sl.mint_line("b.prp", 7);

  // c.prp is a span with no content/offsets: falls back to the mint line.
  const SourceId cs = sl.mint("c.prp", 5, 8, 3);

  // A combine displays as its primary (first) parent.
  const SourceId cb = sl.combine({a1, a0});

  // Unsorted on purpose; the dangling id must be dropped, not emitted.
  std::vector<Segment> segs{
      {5,  0,               cb},
      {2, 10,               bl},
      {0,  0,               a0},
      {4,  0,               cs},
      {2,  4,               a1},
      {1,  0, SourceId_invalid},
      {1,  2,       0xdeadbeef},
  };
  const std::string json = to_json("top.v", sl, segs);

  EXPECT_NE(json.find("\"version\":3"), std::string::npos);
  EXPECT_NE(json.find("\"file\":\"top.v\""), std::string::npos);

  // Sources intern in first-use order of the (gen-sorted) segments.
  EXPECT_NE(json.find("\"sources\":[\"a.prp\",\"b.prp\",\"c.prp\"]"), std::string::npos);

  // Only a.prp has bytes; the others are null placeholders.
  EXPECT_NE(json.find("\"sourcesContent\":[\"mut x = 1\\nmut y = 2\\nout = x+y\\n\",null,null]"), std::string::npos);

  const auto              decoded = decode_mappings(json_string_field(json, "mappings"));
  const std::vector<Dseg> expected{
      {0,  0, 0, 0, 0}, // a0: a.prp 1:1
      {2,  4, 0, 1, 4}, // a1: a.prp 2:5
      {2, 10, 1, 6, 0}, // bl: b.prp line 7, no column
      {4,  0, 2, 2, 0}, // cs: c.prp mint-line fallback
      {5,  0, 0, 1, 4}, // cb: primary parent a1
  };
  EXPECT_EQ(decoded, expected);

  // x_hhds carries the original ids (combine id itself, not its parent), in
  // the same sorted order as the mappings.
  const std::string ids = "\"x_hhds\":{\"source_ids\":[" + std::to_string(a0) + "," + std::to_string(a1) + "," + std::to_string(bl)
                          + "," + std::to_string(cs) + "," + std::to_string(cb) + "]}";
  EXPECT_NE(json.find(ids), std::string::npos);
}

TEST(SourcemapEmit, EscapesContentAndOmitsEmptySourcesContent) {
  Source_locator sl;
  sl.set_file_content("q.prp", std::string("a\"b\\c\td\re\x01z"));
  const SourceId q = sl.mint("q.prp", 0, 3, 1);

  const std::string json = to_json("q.v",
                                   sl,
                                   {
                                       {0, 0, q}
  });
  EXPECT_NE(json.find("\"sourcesContent\":[\"a\\\"b\\\\c\\td\\re\\u0001z\"]"), std::string::npos);

  // No segment resolves → no sources, no sourcesContent block, empty mappings.
  const std::string empty = to_json("e.v",
                                    sl,
                                    {
                                        {0, 0, SourceId_invalid}
  });
  EXPECT_NE(empty.find("\"sources\":[]"), std::string::npos);
  EXPECT_EQ(empty.find("sourcesContent"), std::string::npos);
  EXPECT_NE(empty.find("\"mappings\":\"\""), std::string::npos);
  EXPECT_NE(empty.find("\"x_hhds\":{\"source_ids\":[]}"), std::string::npos);
}

TEST(SourcemapEmit, NegativeDeltasAcrossLines) {
  Source_locator sl;
  sl.set_file_content("a.prp", std::string("aaaa\nbb\ncc\n"));  // lines at 0, 5, 8
  const SourceId late  = sl.mint("a.prp", 8, 9, 3);             // line 3 col 1
  const SourceId early = sl.mint("a.prp", 0, 3, 1);             // line 1 col 1

  // Source positions go backwards while generated positions go forward:
  // exercises the negative-delta VLQ path.
  const std::string       json    = to_json("n.v",
                                            sl,
                                            {
                                                {0, 0,  late},
                                                {1, 0, early}
  });
  const auto              decoded = decode_mappings(json_string_field(json, "mappings"));
  const std::vector<Dseg> expected{
      {0, 0, 0, 2, 0},
      {1, 0, 0, 0, 0}
  };
  EXPECT_EQ(decoded, expected);
}

}  // namespace
