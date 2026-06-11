//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// ECMA-426 (source map v3) egress projection. A textual egress artifact
// (generated Verilog, generated Pyrope, ...) records one Segment per emitted
// statement — just the generated position plus the SourceId stamped on the
// IR — and to_json resolves them through the locator and renders the standard
// {version,file,sources,mappings} object so stock JS/TS-ecosystem tooling can
// jump generated → original.
//
// Lossy by design: one display anchor per segment (a combined SourceId
// resolves to its primary parent). The full SourceId per segment rides under
// "x_hhds" so locator-aware tools can rejoin the combine parents. This is an
// output projection only; the locator's own serialized form (srcmap.txt)
// stays the canonical store.

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "source_locator.hpp"

namespace hhds::sourcemap {

// One emitted anchor: where the text landed in the generated file (0-based,
// per the v3 spec; renderers display 1-based) and the SourceId it came from.
// Resolution to file/line/col happens inside to_json; a segment whose id does
// not resolve is dropped.
struct Segment {
  uint32_t gen_line = 0;
  uint32_t gen_col  = 0;
  SourceId id       = SourceId_invalid;
};

namespace detail {

inline void append_vlq(std::string& out, int64_t value) {
  static constexpr char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  uint64_t              v     = value < 0 ? (static_cast<uint64_t>(-value) << 1) | 1 : static_cast<uint64_t>(value) << 1;
  do {
    uint32_t digit   = v & 0x1f;
    v              >>= 5;
    if (v != 0) {
      digit |= 0x20;  // continuation
    }
    out.push_back(b64[digit]);
  } while (v != 0);
}

inline void append_json_string(std::string& out, std::string_view s) {
  static constexpr char hex[] = "0123456789abcdef";
  out.push_back('"');
  for (char c : s) {
    switch (c) {
      case '"' : out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\t': out += "\\t"; break;
      case '\r': out += "\\r"; break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {  // JSON forbids raw control chars
          out += "\\u00";
          out.push_back(hex[(c >> 4) & 0xf]);
          out.push_back(hex[c & 0xf]);
        } else {
          out.push_back(c);
        }
        break;
    }
  }
  out.push_back('"');
}

}  // namespace detail

// Render the version-3 source map for `segments`, resolving each SourceId
// through `sl`. Segments may arrive unsorted; they are ordered by
// (gen_line, gen_col) as the format requires. Per segment: a span anchor with
// line offsets yields exact line:col, otherwise the anchor's recorded line
// (column 0); unresolvable ids are dropped. sourcesContent embeds the bytes
// the spans were minted on — in-memory from this run, else a disk re-read
// (relative to `content_root`) validated against the recorded content hash —
// since browser-style viewers can only display originals embedded there.
inline std::string to_json(std::string_view generated_file, const Source_locator& sl, std::vector<Segment> segments,
                           std::string_view content_root = {}) {
  std::sort(segments.begin(), segments.end(), [](const Segment& a, const Segment& b) {
    return a.gen_line != b.gen_line ? a.gen_line < b.gen_line : a.gen_col < b.gen_col;
  });

  // Resolved segments, sources interned in first-use order. Path keys point
  // into the locator's file table, stable for the duration of this call.
  struct Mapped {
    uint32_t gen_line;
    uint32_t gen_col;
    uint32_t src_idx;
    uint32_t src_line;  // 0-based
    uint32_t src_col;   // 0-based
    SourceId id;
  };
  std::vector<Mapped>                            mapped;
  std::vector<std::string>                       sources;
  std::unordered_map<std::string_view, uint32_t> source_idx;
  mapped.reserve(segments.size());
  for (const Segment& s : segments) {
    const auto a = sl.resolve(s.id);
    if (!a) {
      continue;
    }
    auto [it, inserted] = source_idx.try_emplace(a->path, static_cast<uint32_t>(sources.size()));
    if (inserted) {
      sources.emplace_back(a->path);
    }
    Mapped m{s.gen_line, s.gen_col, it->second, a->line > 0 ? a->line - 1 : 0, 0, s.id};
    if (a->kind != Source_locator::Anchor_kind::Line_only) {
      if (const auto lc = sl.to_line_col(a->path, a->start_byte)) {
        m.src_line = lc->line - 1;
        m.src_col  = lc->col - 1;
      }
    }
    mapped.push_back(m);
  }

  std::string mappings;
  uint32_t    line     = 0;
  int64_t     prev_col = 0, prev_src = 0, prev_sline = 0, prev_scol = 0;
  bool        first_in_line = true;
  for (const Mapped& m : mapped) {
    while (line < m.gen_line) {
      mappings.push_back(';');
      ++line;
      prev_col      = 0;  // generated column resets per line; the rest carry over
      first_in_line = true;
    }
    if (!first_in_line) {
      mappings.push_back(',');
    }
    detail::append_vlq(mappings, static_cast<int64_t>(m.gen_col) - prev_col);
    detail::append_vlq(mappings, static_cast<int64_t>(m.src_idx) - prev_src);
    detail::append_vlq(mappings, static_cast<int64_t>(m.src_line) - prev_sline);
    detail::append_vlq(mappings, static_cast<int64_t>(m.src_col) - prev_scol);
    prev_col      = m.gen_col;
    prev_src      = m.src_idx;
    prev_sline    = m.src_line;
    prev_scol     = m.src_col;
    first_in_line = false;
  }

  std::string out;
  out += "{\"version\":3,\"file\":";
  detail::append_json_string(out, generated_file);
  out += ",\"sources\":[";
  for (size_t i = 0; i < sources.size(); ++i) {
    if (i != 0) {
      out.push_back(',');
    }
    detail::append_json_string(out, sources[i]);
  }
  out += "]";
  std::vector<std::shared_ptr<const std::string>> contents;
  contents.reserve(sources.size());
  bool any_content = false;
  for (const auto& path : sources) {
    auto c = sl.file_content(path);
    if (c == nullptr) {
      c = sl.load_file_content(path, content_root);
    }
    any_content |= c != nullptr;
    contents.push_back(std::move(c));
  }
  if (any_content) {
    out += ",\"sourcesContent\":[";
    for (size_t i = 0; i < sources.size(); ++i) {
      if (i != 0) {
        out.push_back(',');
      }
      if (contents[i] != nullptr) {
        detail::append_json_string(out, *contents[i]);
      } else {
        out += "null";
      }
    }
    out += "]";
  }
  out += ",\"names\":[],\"mappings\":";
  detail::append_json_string(out, mappings);
  // hhds extra: the full SourceId per segment (same sorted order as the
  // mappings), so locator-aware tooling can rejoin the combine parents.
  out += ",\"x_hhds\":{\"source_ids\":[";
  for (size_t i = 0; i < mapped.size(); ++i) {
    if (i != 0) {
      out.push_back(',');
    }
    out += std::to_string(mapped[i].id);
  }
  out += "]}}";
  out.push_back('\n');
  return out;
}

}  // namespace hhds::sourcemap
