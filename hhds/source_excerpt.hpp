//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// Annotated source excerpt for diagnostics: render a Source_span against the
// locator's hash-validated file bytes as the classic line-numbered,
// caret-underlined block:
//
//    12 | y = x << amt
//       |     ^~~~~~~~
//
// Degrades to "" whenever the bytes are unavailable or the span has no line
// information — no excerpt beats a wrong excerpt. The bytes come from the
// locator's in-memory file_content, falling back to load_file_content (which
// re-reads disk only when the recorded content hash still matches), so the
// rendered line can never drift from what the span was minted on.
//
// Rendering choices (kept deliberately simple):
// - Up to `max_lines` lines of the span are shown; a longer span ends with an
//   elided "..." gutter row. No context lines around the span.
// - Only the first line gets a caret row (clang style); the underline clamps
//   to the end of that line.
// - Columns are bytes: the caret pad reproduces the source line's tabs so the
//   carets stay aligned under any tab width, but a tab or multi-byte UTF-8
//   sequence inside the underline counts one '~' per byte.
// - A line-only span (no byte range, e.g. yosys ingress) shows the line with
//   no caret row.

#include <algorithm>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <vector>

#include "source_locator.hpp"

namespace hhds {

// `root` resolves workspace-relative paths for the disk fallback (see
// Source_locator::load_file_content). Multi-line result, no trailing newline.
inline std::string render_excerpt(const Source_locator& sl, const Source_span& span, std::string_view root = {},
                                  uint32_t max_lines = 3) {
  if (span.file.empty() || max_lines == 0) {
    return {};
  }
  auto content = sl.file_content(span.file);
  if (content == nullptr) {
    content = sl.load_file_content(span.file, root);  // hash-validated; never caches
  }
  if (content == nullptr) {
    return {};
  }
  const std::string_view text = *content;

  // Locate the first excerpt line: byte offset of its start + its 1-based
  // number. A span past EOF or a line past the last one means the span does
  // not match these bytes — render nothing.
  size_t   line_start = 0;
  uint32_t line_no    = 0;
  if (span.start_byte && *span.start_byte <= text.size()) {
    const auto pos = static_cast<size_t>(*span.start_byte);
    line_start     = (pos == 0) ? 0 : text.rfind('\n', pos - 1) + 1;  // npos + 1 == 0: first line
    if (span.start_line) {
      line_no = *span.start_line;
    } else {
      line_no = 1 + static_cast<uint32_t>(std::count(text.begin(), text.begin() + static_cast<ptrdiff_t>(line_start), '\n'));
    }
  } else if (span.start_line && *span.start_line >= 1) {
    for (uint32_t l = 1; l < *span.start_line; ++l) {
      const size_t nl = text.find('\n', line_start);
      if (nl == std::string_view::npos) {
        return {};
      }
      line_start = nl + 1;
    }
    line_no = *span.start_line;
  } else {
    return {};
  }

  // The spanned byte range, clamped to the buffer; a missing end degrades to a
  // single line with a one-char underline.
  const size_t span_begin = span.start_byte ? std::min(static_cast<size_t>(*span.start_byte), text.size()) : line_start;
  const size_t span_end   = std::clamp(span.end_byte ? static_cast<size_t>(*span.end_byte) : span_begin, span_begin, text.size());

  struct Line {
    size_t   begin;
    size_t   end;  // exclusive; excludes the '\n'
    uint32_t no;
  };
  std::vector<Line> lines;
  bool              elided = false;
  for (size_t cur = line_start, no = line_no;;) {
    const size_t nl  = text.find('\n', cur);
    const size_t end = (nl == std::string_view::npos) ? text.size() : nl;
    lines.push_back({cur, end, static_cast<uint32_t>(no)});
    if (nl == std::string_view::npos || nl + 1 >= text.size() || nl + 1 >= span_end) {
      break;  // EOF, or the span does not reach the next line
    }
    if (lines.size() == max_lines) {
      elided = true;
      break;
    }
    cur = nl + 1;
    ++no;
  }

  // Gutter sized by the widest line number shown (the "..." row needs 3).
  size_t width = std::to_string(lines.back().no).size();
  if (elided) {
    width = std::max(width, size_t{3});
  }

  std::string out;
  for (const Line& ln : lines) {
    if (!out.empty()) {
      out += '\n';
    }
    out += std::format("{:>{}} | {}", ln.no, width, text.substr(ln.begin, ln.end - ln.begin));
    if (&ln == &lines.front() && span.start_byte) {
      // Caret row: the pad reproduces tabs from the source line so the '^'
      // lands on the spanned byte under any tab rendering.
      std::string pad;
      for (size_t i = ln.begin; i < span_begin; ++i) {
        pad += (text[i] == '\t') ? '\t' : ' ';
      }
      const size_t ul_end = std::max(std::min(span_end, ln.end), span_begin + 1);
      std::string  underline(ul_end - span_begin, '~');
      underline.front()  = '^';
      out               += '\n';
      out               += std::format("{:>{}} | {}{}", "", width, pad, underline);
    }
  }
  if (elided) {
    out += '\n';
    out += std::format("{:>{}} |", "...", width);
  }
  return out;
}

}  // namespace hhds
