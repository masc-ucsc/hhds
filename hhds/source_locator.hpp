// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// Source_locator (hhds-srcloc): a compact provenance table mapping a 64-bit
// SourceId to a source anchor (file + byte span + line). IR nodes/pins carry one
// uint64 attribute (attrs::srcid) instead of a filename string + span struct.
//
// Identity. SourceId = rapidhash over (file path, start_byte, end_byte) — the same
// span yields the same id in every locator, every run, every separately-compiled
// artifact, so unioning two locators dedups for free (the GraphLibrary gid
// trick). Only true hash collisions probe-remap to a nearby id; remapped ids are
// locator-local. Paths must be workspace/exec-root-relative, never absolute, or
// cross-artifact agreement breaks.
//
// Combined ids. combine(parents) mints a new id meaning "all of these anchors".
// The parent list is caller-ordered and order-significant: parents[0] is the
// primary anchor (what a diagnostic renders as its span) and the id hashes the
// parents in that order, so combine(a,b) != combine(b,a) while re-minting the
// same ordered list dedups to one entry. The call is variadic: a consumer
// accumulating N sites collects them and mints once.
//
// Ownership/threading. One instance per single-writer artifact (a Graph body, a
// livehd Lnast) — mints need no locks because only one thread edits an artifact
// at a time. The per-GraphLibrary/Forest locator is the loaded read-only base
// plus the save-time union destination; working locators chain to it via
// set_base() (lookups consult own entries, then the base).
//
// Merge. merge(other) re-mints other's entries (base first, then own, preserving
// insertion order so parents always precede the combine entries that reference
// them): payload-identical entries dedup to one id regardless of the order two
// locators probe-remapped them, combine parent lists are translated through the
// remap-so-far (re-keying cascades naturally), and the returned old->new table
// is applied by the caller to the attrs::srcid stores of affected artifacts.
//
// Persistence. save()/load() write one text table (srcmap.txt) next to
// library.txt / forest.txt, in the same line-prefix style. Entries are
// append-only; existing entries never change meaning.
//
// File contents. set_file_content keeps the source bytes in memory (shared
// across locators by pointer) and derives content_hash + line_offsets from
// them; save() persists only the hash, and load_file_content re-reads disk
// validated against it — so consumers (sourcesContent egress, diagnostics
// excerpts) never see bytes that drifted from what the spans were minted on.

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <iterator>
#include <memory>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "hhds/attrs/srcid.hpp"
#include "hhds/rapidhash.h"

namespace hhds {

using SourceId = uint64_t;

inline constexpr SourceId SourceId_invalid = 0;

// Fully resolved, self-contained source span — the diagnostic-grade view of a
// SourceId. Unlike Anchor (whose `path` borrows from the locator's file
// table), every field is an owned copy/value, so a diagnostic record built
// from it can outlive the locator that resolved it. Every field is optional; a
// default-constructed Source_span means "unknown location" — consumers render
// location-less rather than wrong. Lines/cols are 1-based, ends exclusive
// (tree-sitter / LSP convention). A consumer diagnostic layer can adopt this
// directly as its span type.
struct Source_span {
  std::optional<uint64_t> source_id  = {};  // the SourceId this span resolved from
  std::optional<uint32_t> file_id    = {};  // producer-local file index (never set by the locator)
  std::string             file       = {};  // workspace-relative path; empty = unknown
  std::optional<uint64_t> start_byte = {};
  std::optional<uint64_t> end_byte   = {};  // exclusive
  std::optional<uint32_t> start_line = {};  // 1-based
  std::optional<uint32_t> start_col  = {};  // 1-based
  std::optional<uint32_t> end_line   = {};
  std::optional<uint32_t> end_col    = {};  // exclusive

  [[nodiscard]] bool is_null() const { return !source_id && !file_id && file.empty() && !start_byte && !start_line; }
};

class Source_locator_tester;  // test-only seam (friend)

class Source_locator {
public:
  static constexpr uint32_t kDefaultMaxDepth = 32;

  enum class Anchor_kind : uint8_t { Span = 0, Line_only = 1, Combine = 2 };

  // Resolved view. `path` points into the owning locator's file table and stays
  // valid while that locator is alive and un-cleared.
  struct Anchor {
    std::string_view path;
    uint64_t         start_byte = 0;
    uint64_t         end_byte   = 0;
    uint32_t         line       = 0;  // 1-based; 0 = unknown
    Anchor_kind      kind       = Anchor_kind::Span;
  };

  // resolve_all result: concrete anchors in first-parent-first order. When the
  // combine DAG is deeper than max_depth the expansion stops and reports it
  // (`truncated`) instead of silently dropping anchors.
  struct Resolved_set {
    std::vector<Anchor> anchors;
    bool                truncated = false;
  };

  struct Line_col {
    uint32_t line = 0;  // 1-based
    uint32_t col  = 0;  // 1-based
  };

  using Remap = std::unordered_map<SourceId, SourceId>;

  // Fast per-file Span minting handle (see span_minter() below). Caches the
  // file id and the path's hash contribution, so each span costs one rapidhash
  // over (start,end) plus the table insert — no per-span path re-hash and no
  // per-span path interning. Ids are byte-identical to mint(path, ...). Valid
  // only while the owning locator is alive and its file table is not cleared.
  class Span_minter {
  public:
    Span_minter() = default;
    SourceId mint(uint64_t start_byte, uint64_t end_byte, uint32_t line) const {
      return loc_->insert_span_fast(path_seed_, file_id_, start_byte, end_byte, line);
    }
    [[nodiscard]] bool valid() const noexcept { return loc_ != nullptr; }

  private:
    friend class Source_locator;
    Span_minter(Source_locator* loc, uint32_t file_id, uint64_t path_seed)
        : loc_(loc), file_id_(file_id), path_seed_(path_seed) {}
    Source_locator* loc_       = nullptr;
    uint32_t        file_id_   = 0;
    uint64_t        path_seed_ = 0;
  };

  Source_locator() = default;

  // ---- minting (single-writer; no locks by design) -------------------------

  // Span anchor: the common case. `line` is stored so resolution never needs to
  // re-read the source file.
  SourceId mint(std::string_view path, uint64_t start_byte, uint64_t end_byte, uint32_t line) {
    return insert_anchor(hash_span(path, start_byte, end_byte), Anchor_kind::Span, path, start_byte, end_byte, line);
  }

  // Hand out a Span_minter bound to `path` (interned once here). Use when
  // minting many spans for the same file (e.g. a parser materializing a tree):
  // the path hash and file interning are paid once instead of per span.
  [[nodiscard]] Span_minter span_minter(std::string_view path) {
    assert(!path.empty() && path.front() != '/'
           && "Source_locator: path must be workspace-relative (absolute paths break cross-artifact id agreement)");
    const uint32_t fid  = intern_file(path);
    const uint64_t seed = hash_bytes(static_cast<uint64_t>(Anchor_kind::Span), path);
    return Span_minter(this, fid, seed);
  }

  // Pre-size the entry table and id index for an expected entry count, so a
  // bulk mint (a whole-tree materialization) does not rehash/realloc its way
  // up from empty. Advisory; over- or under-reserving only affects speed.
  void reserve(size_t expected_entries) {
    entries_.reserve(expected_entries);
    index_.reserve(expected_entries);
  }

  // Line-only anchor: for ingress that only knows file:line (e.g. yosys).
  SourceId mint_line(std::string_view path, uint32_t line) {
    return insert_anchor(hash_line_only(path, line), Anchor_kind::Line_only, path, 0, 0, line);
  }

  // Combined id: caller-ordered, order-significant parent list; parents[0] is
  // the primary anchor. Every parent must already resolve (here or in the base).
  SourceId combine(std::span<const SourceId> parents) {
    assert(!parents.empty() && "Source_locator::combine: needs at least one parent");
    if (parents.empty()) {
      return SourceId_invalid;  // release-build guard: never mint an empty combine
    }
    for ([[maybe_unused]] const SourceId p : parents) {
      assert(has(p) && "Source_locator::combine: unknown parent SourceId");
    }
    if (parents.size() == 1) {
      return parents.front();  // degenerate: nothing to combine
    }
    std::vector<SourceId> copy(parents.begin(), parents.end());
    const SourceId        want = hash_combine_parents(copy);
    return insert_combine(want, std::move(copy));
  }

  SourceId combine(std::initializer_list<SourceId> parents) {
    return combine(std::span<const SourceId>(parents.begin(), parents.size()));
  }

  // Re-mint `id` (an entry of `src`) into this locator, so a node copied
  // across artifacts keeps a resolvable provenance id. Anchors re-mint to the
  // SAME id in practice (ids hash the payload); a combine flattens to its
  // concrete anchors (first-parent-first) and re-combines, which preserves the
  // primary anchor and the note ordering even when the nesting shape differs.
  // Per-file metadata is carried along — the bytes themselves (a shared-pointer
  // copy) or, for files ingested without content, the line-offset table and
  // content hash — so resolved spans keep full line:col and excerpt fidelity
  // in this locator too. Returns SourceId_invalid when `id` does not resolve
  // in `src`.
  SourceId import_from(const Source_locator& src, SourceId id) {
    if (id == SourceId_invalid || &src == this) {
      return id;
    }
    if (has(id)) {
      return id;  // already present (shared base, or a previous import)
    }
    const auto rs = src.resolve_all(id);
    if (rs.anchors.empty()) {
      return SourceId_invalid;
    }
    std::vector<SourceId> ids;
    ids.reserve(rs.anchors.size());
    for (const auto& a : rs.anchors) {
      if (file_content(a.path) == nullptr) {
        if (auto content = src.file_content(a.path); content != nullptr) {
          set_file_content(a.path, std::move(content));
        } else {
          if (file_line_offsets(a.path) == nullptr) {
            if (const auto* offs = src.file_line_offsets(a.path); offs != nullptr) {
              set_file_line_offsets(a.path, *offs);
            }
          }
          if (file_content_hash(a.path) == 0) {
            if (const uint64_t h = src.file_content_hash(a.path); h != 0) {
              set_file_content_hash(a.path, h);  // load_file_content can still validate here
            }
          }
        }
      }
      if (a.kind == Anchor_kind::Line_only) {
        ids.push_back(mint_line(a.path, a.line));
      } else {
        ids.push_back(mint(a.path, a.start_byte, a.end_byte, a.line));
      }
    }
    if (ids.size() == 1) {
      return ids.front();
    }
    return combine(std::span<const SourceId>(ids.data(), ids.size()));
  }

  // ---- resolution (own entries first, then the chained base) ---------------

  [[nodiscard]] bool has(SourceId id) const { return find_entry(id, nullptr) != nullptr; }

  // Primary anchor: combine entries follow their first parent down to a
  // concrete span/line anchor. Chain depth is unbounded (combine graphs are
  // DAGs by construction); the visited set only guards corrupt input.
  [[nodiscard]] std::optional<Anchor> resolve(SourceId id) const {
    std::unordered_set<SourceId> visited;
    for (;;) {
      if (!visited.insert(id).second) {
        return std::nullopt;  // cycle: only possible with corrupt data
      }
      const Source_locator* owner = nullptr;
      const Entry*          e     = find_entry(id, &owner);
      if (e == nullptr || (e->kind == Anchor_kind::Combine && e->parents.empty())) {
        return std::nullopt;
      }
      if (e->kind != Anchor_kind::Combine) {
        return make_anchor(*e, *owner);
      }
      id = e->parents.front();
    }
  }

  // All concrete anchors reachable from `id`, first-parent-first. Diamonds are
  // visited once (visited set); expansion is linear in reachable entries.
  [[nodiscard]] Resolved_set resolve_all(SourceId id, uint32_t max_depth = kDefaultMaxDepth) const {
    Resolved_set                               out;
    std::unordered_set<SourceId>               visited;
    std::vector<std::pair<SourceId, uint32_t>> stack;
    stack.emplace_back(id, 0);
    while (!stack.empty()) {
      const auto [cur, depth] = stack.back();
      stack.pop_back();
      if (!visited.insert(cur).second) {
        continue;
      }
      const Source_locator* owner = nullptr;
      const Entry*          e     = find_entry(cur, &owner);
      if (e == nullptr) {
        out.truncated = true;  // dangling id: report, never silently drop
        continue;
      }
      if (e->kind != Anchor_kind::Combine) {
        out.anchors.push_back(make_anchor(*e, *owner));
        continue;
      }
      if (depth >= max_depth) {
        out.truncated = true;
        continue;
      }
      for (auto it = e->parents.rbegin(); it != e->parents.rend(); ++it) {
        stack.emplace_back(*it, depth + 1);
      }
    }
    return out;
  }

  // ---- resolved spans (diagnostic-grade views) ------------------------------

  // Primary anchor of `id` as an owned Source_span: file + byte span + line,
  // plus full 1-based line:col intervals when the file's line-offset table is
  // known. A null span (is_null()) when `id` is invalid or unresolvable.
  [[nodiscard]] Source_span resolve_span(SourceId id) const {
    if (id == SourceId_invalid) {
      return {};
    }
    const auto a = resolve(id);
    if (!a) {
      return {};
    }
    return make_span(*a, id);
  }

  // resolve_spans result: the primary anchor plus the secondary anchors of a
  // combined id, first-parent-first. A diagnostic renders `primary` as its
  // span and `related` as its note locations — one call resolves both, so the
  // related sites (inline call chains, merged writes, ...) are never dropped.
  struct Resolved_spans {
    Source_span              primary;  // null when `id` does not resolve
    std::vector<Source_span> related;
    bool                     truncated = false;
  };

  [[nodiscard]] Resolved_spans resolve_spans(SourceId id, uint32_t max_depth = kDefaultMaxDepth) const {
    Resolved_spans out;
    if (id == SourceId_invalid) {
      return out;
    }
    const auto rs = resolve_all(id, max_depth);
    out.truncated = rs.truncated;
    if (rs.anchors.empty()) {
      return out;
    }
    out.primary = make_span(rs.anchors.front(), id);
    out.related.reserve(rs.anchors.size() - 1);
    for (size_t i = 1; i < rs.anchors.size(); ++i) {
      out.related.push_back(make_span(rs.anchors[i], id));
    }
    return out;
  }

  // ---- file table -----------------------------------------------------------

  [[nodiscard]] std::string_view file_path(uint32_t file_id) const {
    assert(file_id < files_.size() && "Source_locator::file_path: bad file id");
    return files_[file_id].path;
  }

  // Own files only (the base is not counted): with file_path this lets a
  // consumer enumerate the files this locator actually minted from — e.g. a
  // build-system depfile listing the sources a compile read.
  [[nodiscard]] uint32_t file_count() const noexcept { return static_cast<uint32_t>(files_.size()); }

  // The per-file line-offset table (nullptr when absent), so a consumer
  // re-minting anchors into another locator can carry the line:col metadata
  // along. Looks through the base chain like every other lookup.
  [[nodiscard]] const std::vector<uint64_t>* file_line_offsets(std::string_view path) const {
    const File* f = find_file(path);
    if (f == nullptr || f->line_offsets.empty()) {
      return nullptr;
    }
    return &f->line_offsets;
  }

  // Optional per-file metadata. The line-offset table (byte offset of each line
  // start, ascending, first entry 0) lets to_line_col derive 1-based line:col —
  // full LSP-grade intervals — without storing columns per entry. Producers
  // holding the file's bytes should prefer set_file_content, which derives
  // both; these manual setters remain for carry paths that only have metadata.
  void set_file_content_hash(std::string_view path, uint64_t hash) { files_[intern_file(path)].content_hash = hash; }

  [[nodiscard]] uint64_t file_content_hash(std::string_view path) const {
    const File* f = find_file(path);
    return f == nullptr ? 0 : f->content_hash;
  }

  void set_file_line_offsets(std::string_view path, std::vector<uint64_t> offsets) {
    files_[intern_file(path)].line_offsets = std::move(offsets);
  }

  // ---- file contents ---------------------------------------------------------

  // Deterministic hash of a file's bytes — the value content_hash records.
  // Public so every producer and validator agrees on the function.
  [[nodiscard]] static uint64_t content_hash_of(std::string_view content) noexcept {
    return rapidhash(content.data(), content.size());
  }

  // Full-file ingestion: keeps the bytes in memory and derives content_hash +
  // line_offsets from them in one step, so the three can never disagree. The
  // producer that just read/parsed the file calls this instead of hand-rolling
  // the newline scan. save() persists only the hash (see load_file_content).
  void set_file_content(std::string_view path, std::string&& content) {
    set_file_content(path, std::make_shared<const std::string>(std::move(content)));
  }

  // Shared-pointer form: carrying a file across locators (import/merge paths)
  // is a refcount bump, not a byte copy.
  void set_file_content(std::string_view path, std::shared_ptr<const std::string> content) {
    assert(content != nullptr && "Source_locator::set_file_content: null content");
    File& f        = files_[intern_file(path)];
    f.content      = std::move(content);
    f.content_hash = content_hash_of(*f.content);
    f.line_offsets = derive_line_offsets(*f.content);
  }

  // In-memory bytes (own files first, then the base chain); nullptr when this
  // run never ingested the file (e.g. a load()ed table). The shared_ptr keeps
  // the bytes alive independently of the locator, so string_view slices of
  // *result are safe while the pointer is held.
  [[nodiscard]] std::shared_ptr<const std::string> file_content(std::string_view path) const {
    const File* f = find_file(path);
    return f == nullptr ? nullptr : f->content;
  }

  // Post-load() recovery: srcmap.txt persists the content hash, never the
  // bytes. Re-read `path` from disk (resolved against `root` — paths are
  // workspace-relative by contract) and return the bytes ONLY when they still
  // match the recorded hash; a drifted, unknown, or hash-less file returns
  // nullptr (no content beats wrong content). Never caches into the locator:
  // a loaded table often serves as a shared read-only base across threads.
  [[nodiscard]] std::shared_ptr<const std::string> load_file_content(std::string_view path, std::string_view root = {}) const {
    const File* f = find_file(path);
    if (f == nullptr) {
      return nullptr;
    }
    if (f->content != nullptr) {
      return f->content;
    }
    if (f->content_hash == 0) {
      return nullptr;  // nothing to validate against
    }
    namespace fs = std::filesystem;
    const auto    full = root.empty() ? fs::path(path) : fs::path(root) / path;
    std::ifstream ifs(full, std::ios::binary);
    if (!ifs) {
      return nullptr;
    }
    std::string bytes((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    if (content_hash_of(bytes) != f->content_hash) {
      return nullptr;
    }
    return std::make_shared<const std::string>(std::move(bytes));
  }

  [[nodiscard]] std::optional<Line_col> to_line_col(std::string_view path, uint64_t byte) const {
    const File* f = find_file(path);
    if (f == nullptr || f->line_offsets.empty()) {
      return std::nullopt;
    }
    const auto&  offs = f->line_offsets;
    const auto   it   = std::upper_bound(offs.begin(), offs.end(), byte);
    const size_t idx  = static_cast<size_t>(it - offs.begin());
    if (idx == 0) {
      return std::nullopt;  // byte precedes the first line start
    }
    return Line_col{static_cast<uint32_t>(idx), static_cast<uint32_t>(byte - offs[idx - 1] + 1)};
  }

  // ---- base chaining ----------------------------------------------------------

  // Read-only fallback table (the library/forest-level union). The base must
  // outlive this locator or be detached with set_base(nullptr) first.
  void set_base(const Source_locator* base) noexcept { base_ = base; }

  [[nodiscard]] const Source_locator* base() const noexcept { return base_; }

  // ---- union-merge ------------------------------------------------------------

  // Union `other` (its base chain first, then its own entries) into this
  // locator. Payload-identical entries dedup to one id; residual true id
  // conflicts probe-remap; combine parent lists are translated through the
  // remap-so-far, so a remapped parent re-keys its combine entry transitively.
  // Returns old->new for every id that moved — the caller applies it to the
  // attrs::srcid values of the artifacts it is folding in.
  [[nodiscard]] Remap merge(const Source_locator& other) {
    Remap remap;
    if (&other == this) {
      return remap;  // self-merge is a no-op (and would otherwise mutate entries_ mid-iteration)
    }
    merge_into(other, remap);
    return remap;
  }

  // ---- persistence: <db_path>/srcmap.txt ------------------------------------

  void save(const std::string& db_path) const {
    namespace fs    = std::filesystem;
    const auto path = fs::path(db_path) / "srcmap.txt";
    if (empty()) {
      // A stale table from a previous save must not resurrect on the next load.
      std::error_code ec;
      fs::remove(path, ec);
      return;
    }
    fs::create_directories(db_path);
    std::ofstream ofs(path);
    assert(ofs.good() && "Source_locator::save: cannot open srcmap.txt");
    ofs << "hhds_srcmap 1\n";
    for (size_t fid = 0; fid < files_.size(); ++fid) {
      const File& f = files_[fid];
      ofs << "file " << fid << " " << f.path << "\n";
      if (f.content_hash != 0) {
        ofs << "filehash " << fid << " " << f.content_hash << "\n";
      }
      if (!f.line_offsets.empty()) {
        ofs << "filelines " << fid << " " << f.line_offsets.size();
        for (const uint64_t off : f.line_offsets) {
          ofs << " " << off;
        }
        ofs << "\n";
      }
    }
    // Insertion order: parents always precede the combine entries using them.
    for (const auto& [id, e] : entries_) {
      switch (e.kind) {
        case Anchor_kind::Span:
          ofs << "span " << id << " " << e.file_id << " " << e.start_byte << " " << e.end_byte << " " << e.line << "\n";
          break;
        case Anchor_kind::Line_only: ofs << "line " << id << " " << e.file_id << " " << e.line << "\n"; break;
        case Anchor_kind::Combine:
          ofs << "combine " << id << " " << e.parents.size();
          for (const SourceId p : e.parents) {
            ofs << " " << p;
          }
          ofs << "\n";
          break;
      }
    }
  }

  // Replaces the current own state with the on-disk table (the base pointer is
  // kept). Returns false when the dir has no srcmap.txt (older save dirs) OR
  // the table is malformed/corrupt — a bad table degrades to "no provenance"
  // (one stderr note), never to undefined behavior at resolve time.
  bool load(const std::string& db_path) {
    namespace fs    = std::filesystem;
    const auto path = fs::path(db_path) / "srcmap.txt";
    clear();
    std::ifstream ifs(path);
    if (!ifs) {
      return false;
    }
    if (!load_table(ifs)) {
      std::cerr << "Source_locator::load: malformed srcmap table at " << path.string() << " -- ignoring it\n";
      clear();
      return false;
    }
    return true;
  }

  // ---- state ------------------------------------------------------------------

  // Own data only — a non-empty base does not count.
  [[nodiscard]] bool empty() const noexcept { return entries_.empty() && files_.empty(); }

  [[nodiscard]] size_t size() const noexcept { return entries_.size(); }

  // Drops own entries and files; keeps the base pointer.
  void clear() noexcept {
    entries_.clear();
    index_.clear();
    files_.clear();
    path_to_fid_.clear();
  }

private:
  struct Entry {
    Anchor_kind           kind       = Anchor_kind::Span;
    uint32_t              file_id    = 0;
    uint64_t              start_byte = 0;
    uint64_t              end_byte   = 0;
    uint32_t              line       = 0;
    std::vector<SourceId> parents;  // Combine only
  };

  struct File {
    std::string           path;
    uint64_t              content_hash = 0;
    std::vector<uint64_t> line_offsets;  // ascending byte offsets of line starts
    // In-memory source bytes (never persisted). shared_ptr: cross-locator
    // carries are pointer copies, and views into *content outlive clear().
    std::shared_ptr<const std::string> content;
  };

  // Byte offset of every line start (first entry always 0) — the table
  // to_line_col binary-searches.
  [[nodiscard]] static std::vector<uint64_t> derive_line_offsets(std::string_view content) {
    std::vector<uint64_t> offsets;
    offsets.push_back(0);
    for (size_t i = 0; i < content.size(); ++i) {
      if (content[i] == '\n') {
        offsets.push_back(i + 1);
      }
    }
    return offsets;
  }

  // ---- hashing -----------------------------------------------------------------
  // Deterministic rapidhash (vendored rapidhash.h; std::hash is not stable
  // across platforms and must not be used — cross-artifact id agreement is the
  // whole point). Fields chain through the seed, integers serialized to
  // little-endian bytes first: rapidhash interprets its input as a
  // little-endian byte sequence on any host, so ids agree across platforms. A
  // distinct kind byte seeds each id family.

  static uint64_t hash_bytes(uint64_t h, std::string_view s) noexcept { return rapidhash_withSeed(s.data(), s.size(), h); }

  static uint64_t hash_u64(uint64_t h, uint64_t v) noexcept {
    uint8_t le[8];
    for (int i = 0; i < 8; ++i) {
      le[i] = static_cast<uint8_t>(v >> (i * 8));
    }
    return rapidhash_withSeed(le, sizeof(le), h);
  }

  [[nodiscard]] SourceId finish_hash(uint64_t h) const noexcept {
    h &= hash_mask_;
    return h == SourceId_invalid ? SourceId{1} : h;
  }

  [[nodiscard]] SourceId hash_span(std::string_view path, uint64_t start_byte, uint64_t end_byte) const noexcept {
    uint64_t h = hash_bytes(static_cast<uint64_t>(Anchor_kind::Span), path);
    h          = hash_u64(h, start_byte);
    h          = hash_u64(h, end_byte);
    return finish_hash(h);
  }

  [[nodiscard]] SourceId hash_line_only(std::string_view path, uint32_t line) const noexcept {
    uint64_t h = hash_bytes(static_cast<uint64_t>(Anchor_kind::Line_only), path);
    h          = hash_u64(h, line);
    return finish_hash(h);
  }

  [[nodiscard]] SourceId hash_combine_parents(std::span<const SourceId> parents) const noexcept {
    uint64_t h = static_cast<uint64_t>(Anchor_kind::Combine);
    for (const SourceId p : parents) {
      h = hash_u64(h, p);
    }
    return finish_hash(h);
  }

  // ---- lookup -------------------------------------------------------------------

  [[nodiscard]] const Entry* find_local(SourceId id) const {
    const auto it = index_.find(id);
    return it == index_.end() ? nullptr : &entries_[it->second].second;
  }

  // Own entries first, then the base chain. `owner` (optional) receives the
  // locator whose file table the entry's file_id indexes.
  const Entry* find_entry(SourceId id, const Source_locator** owner) const {
    if (const Entry* e = find_local(id)) {
      if (owner != nullptr) {
        *owner = this;
      }
      return e;
    }
    return base_ != nullptr ? base_->find_entry(id, owner) : nullptr;
  }

  [[nodiscard]] const File* find_file(std::string_view path) const {
    const auto it = path_to_fid_.find(std::string(path));
    if (it != path_to_fid_.end()) {
      return &files_[it->second];
    }
    return base_ != nullptr ? base_->find_file(path) : nullptr;
  }

  uint32_t intern_file(std::string_view path) {
    const auto it = path_to_fid_.find(std::string(path));
    if (it != path_to_fid_.end()) {
      return it->second;
    }
    const auto fid = static_cast<uint32_t>(files_.size());
    files_.push_back(File{std::string(path), 0, {}, nullptr});
    path_to_fid_.emplace(files_.back().path, fid);
    return fid;
  }

  static Anchor make_anchor(const Entry& e, const Source_locator& owner) {
    return Anchor{owner.file_path(e.file_id), e.start_byte, e.end_byte, e.line, e.kind};
  }

  // Anchor -> owned Source_span. Every Source_span the locator hands out keeps
  // the resolving id, so a consumer can chase back to the combine parents.
  [[nodiscard]] Source_span make_span(const Anchor& a, SourceId id) const {
    Source_span span;
    span.source_id = id;
    span.file      = std::string(a.path);
    if (a.line != 0) {
      span.start_line = a.line;
      span.end_line   = a.line;
    }
    if (a.kind != Anchor_kind::Line_only) {
      span.start_byte = a.start_byte;
      span.end_byte   = a.end_byte;
      if (const auto lc = to_line_col(a.path, a.start_byte)) {
        span.start_line = lc->line;
        span.start_col  = lc->col;
      }
      if (const auto lc = to_line_col(a.path, a.end_byte)) {
        span.end_line = lc->line;
        span.end_col  = lc->col;
      }
    }
    return span;
  }

  // ---- insertion (probe-remap on true collisions) ---------------------------------

  // Span identity is (path, start_byte, end_byte) — `line` is derived data the
  // hash excludes, so it must not split payload equality either (two producers
  // disagreeing on the line for one span would otherwise silently probe-remap
  // and forfeit cross-artifact id agreement; first writer wins instead). For
  // Line_only anchors the line IS the identity.
  [[nodiscard]] bool anchor_payload_equal(const Entry& e, const Source_locator& owner, Anchor_kind kind, std::string_view path,
                                          uint64_t start_byte, uint64_t end_byte, uint32_t line) const {
    return e.kind == kind && e.start_byte == start_byte && e.end_byte == end_byte
           && (kind != Anchor_kind::Line_only || e.line == line) && owner.file_path(e.file_id) == path;
  }

  // Probe from `want`: an existing identical payload dedups (own or base); a
  // different payload at the slot is a true collision and the id advances.
  SourceId insert_anchor(SourceId want, Anchor_kind kind, std::string_view path, uint64_t start_byte, uint64_t end_byte,
                         uint32_t line) {
    assert(!path.empty() && path.front() != '/'
           && "Source_locator: path must be workspace-relative (absolute paths break cross-artifact id agreement)");
    SourceId id = want;
    for (;;) {
      const Source_locator* owner = nullptr;
      const Entry*          e     = find_entry(id, &owner);
      if (e == nullptr) {
        break;
      }
      if (anchor_payload_equal(*e, *owner, kind, path, start_byte, end_byte, line)) {
        return id;
      }
      ++id;
      if (id == SourceId_invalid) {
        id = 1;
      }
    }
    Entry e;
    e.kind       = kind;
    e.file_id    = intern_file(path);
    e.start_byte = start_byte;
    e.end_byte   = end_byte;
    e.line       = line;
    index_.emplace(id, static_cast<uint32_t>(entries_.size()));
    entries_.emplace_back(id, std::move(e));
    return id;
  }

  // Span_minter's hot path: the file is already interned (`file_id`) and the
  // path's hash contribution is precomputed (`path_seed`), so this is
  // insert_anchor for a Span without the per-call path hash or path interning.
  // Identical id and dedup/probe semantics to insert_anchor(Span, ...).
  SourceId insert_span_fast(uint64_t path_seed, uint32_t file_id, uint64_t start_byte, uint64_t end_byte, uint32_t line) {
    SourceId id = finish_hash(hash_u64(hash_u64(path_seed, start_byte), end_byte));
    for (;;) {
      const Source_locator* owner = nullptr;
      const Entry*          e     = find_entry(id, &owner);
      if (e == nullptr) {
        break;
      }
      if (anchor_payload_equal(*e, *owner, Anchor_kind::Span, file_path(file_id), start_byte, end_byte, line)) {
        return id;
      }
      ++id;
      if (id == SourceId_invalid) {
        id = 1;
      }
    }
    Entry e;
    e.kind       = Anchor_kind::Span;
    e.file_id    = file_id;
    e.start_byte = start_byte;
    e.end_byte   = end_byte;
    e.line       = line;
    index_.emplace(id, static_cast<uint32_t>(entries_.size()));
    entries_.emplace_back(id, std::move(e));
    return id;
  }

  SourceId insert_combine(SourceId want, std::vector<SourceId>&& parents) {
    SourceId id = want;
    for (;;) {
      const Source_locator* owner = nullptr;
      const Entry*          e     = find_entry(id, &owner);
      if (e == nullptr) {
        break;
      }
      if (e->kind == Anchor_kind::Combine && e->parents == parents) {
        return id;
      }
      ++id;
      if (id == SourceId_invalid) {
        id = 1;
      }
    }
    Entry e;
    e.kind    = Anchor_kind::Combine;
    e.parents = std::move(parents);
    index_.emplace(id, static_cast<uint32_t>(entries_.size()));
    entries_.emplace_back(id, std::move(e));
    return id;
  }

  // Structural validation of a loaded table: every record parses fully, file
  // ids reference declared files, line-offset tables ascend, SourceIds are
  // unique, and combine parents are already defined (parents precede
  // combiners — the invariant merge_into's remap cascade relies on). Any
  // violation rejects the whole table.
  bool load_table(std::istream& is) {
    std::string line;
    if (!std::getline(is, line) || line.rfind("hhds_srcmap ", 0) != 0) {
      return false;
    }
    const auto valid_fid = [this](uint32_t fid) { return fid < files_.size() && !files_[fid].path.empty(); };
    while (std::getline(is, line)) {
      if (line.empty()) {
        continue;
      }
      if (line.rfind("file ", 0) == 0) {
        // "file <fid> <path>" — the path is everything after the second space
        // (paths may contain spaces; istream >> would truncate them).
        const size_t sp = line.find(' ', 5);
        if (sp == std::string::npos) {
          return false;
        }
        std::istringstream ss(line.substr(5, sp - 5));
        uint32_t           fid = 0;
        if (!(ss >> fid)) {
          return false;
        }
        if (fid >= files_.size()) {
          files_.resize(fid + 1);
        }
        if (!files_[fid].path.empty()) {
          return false;  // duplicate file id
        }
        files_[fid].path = line.substr(sp + 1);
        if (files_[fid].path.empty() || !path_to_fid_.emplace(files_[fid].path, fid).second) {
          return false;
        }
      } else if (line.rfind("filehash ", 0) == 0) {
        std::istringstream ss(line.substr(9));
        uint32_t           fid  = 0;
        uint64_t           hash = 0;
        if (!(ss >> fid >> hash) || !valid_fid(fid)) {
          return false;
        }
        files_[fid].content_hash = hash;
      } else if (line.rfind("filelines ", 0) == 0) {
        std::istringstream ss(line.substr(10));
        uint32_t           fid   = 0;
        size_t             count = 0;
        if (!(ss >> fid >> count) || !valid_fid(fid)) {
          return false;
        }
        auto& offs = files_[fid].line_offsets;
        offs.resize(count);
        for (size_t i = 0; i < count; ++i) {
          if (!(ss >> offs[i]) || (i > 0 && offs[i] <= offs[i - 1])) {
            return false;  // missing or non-ascending offsets
          }
        }
      } else if (line.rfind("span ", 0) == 0) {
        std::istringstream ss(line.substr(5));
        SourceId           id = 0;
        Entry              e;
        e.kind = Anchor_kind::Span;
        if (!(ss >> id >> e.file_id >> e.start_byte >> e.end_byte >> e.line) || !valid_fid(e.file_id)) {
          return false;
        }
        if (!append_loaded(id, std::move(e))) {
          return false;
        }
      } else if (line.rfind("line ", 0) == 0) {
        std::istringstream ss(line.substr(5));
        SourceId           id = 0;
        Entry              e;
        e.kind = Anchor_kind::Line_only;
        if (!(ss >> id >> e.file_id >> e.line) || !valid_fid(e.file_id)) {
          return false;
        }
        if (!append_loaded(id, std::move(e))) {
          return false;
        }
      } else if (line.rfind("combine ", 0) == 0) {
        std::istringstream ss(line.substr(8));
        SourceId           id    = 0;
        size_t             count = 0;
        if (!(ss >> id >> count) || count < 2) {
          return false;  // a combine has at least two parents by construction
        }
        Entry e;
        e.kind = Anchor_kind::Combine;
        e.parents.resize(count);
        for (size_t i = 0; i < count; ++i) {
          if (!(ss >> e.parents[i]) || find_local(e.parents[i]) == nullptr) {
            return false;  // missing or not-yet-defined parent
          }
        }
        if (!append_loaded(id, std::move(e))) {
          return false;
        }
      } else {
        return false;  // unknown record kind
      }
    }
    return true;
  }

  [[nodiscard]] bool append_loaded(SourceId id, Entry&& e) {
    if (id == SourceId_invalid) {
      return false;
    }
    const auto [it, inserted] = index_.emplace(id, static_cast<uint32_t>(entries_.size()));
    if (!inserted) {
      return false;  // duplicate SourceId
    }
    entries_.emplace_back(id, std::move(e));
    return true;
  }

  void merge_into(const Source_locator& other, Remap& remap) {
    // Base chain first (own-then-base view): a loaded artifact's combine entries
    // reference base ids, so those must land before the entries that use them.
    if (other.base_ != nullptr && other.base_ != this) {
      merge_into(*other.base_, remap);
    }
    // Per-file metadata (content hash, line offsets) survives the union.
    for (const File& f : other.files_) {
      const uint32_t fid  = intern_file(f.path);
      File&          mine = files_[fid];
      if (mine.content_hash == 0) {
        mine.content_hash = f.content_hash;
      }
      if (mine.line_offsets.empty() && !f.line_offsets.empty()) {
        mine.line_offsets = f.line_offsets;
      }
      if (mine.content == nullptr && f.content != nullptr) {
        mine.content = f.content;
      }
    }
    for (const auto& [oid, oe] : other.entries_) {
      SourceId nid = SourceId_invalid;
      if (oe.kind == Anchor_kind::Combine) {
        std::vector<SourceId> parents;
        parents.reserve(oe.parents.size());
        for (const SourceId p : oe.parents) {
          const auto it = remap.find(p);
          parents.push_back(it == remap.end() ? p : it->second);
        }
        const SourceId want = hash_combine_parents(parents);
        nid                 = insert_combine(want, std::move(parents));
      } else {
        const std::string_view path = other.file_path(oe.file_id);
        const SourceId         want
            = oe.kind == Anchor_kind::Span ? hash_span(path, oe.start_byte, oe.end_byte) : hash_line_only(path, oe.line);
        nid = insert_anchor(want, oe.kind, path, oe.start_byte, oe.end_byte, oe.line);
      }
      if (nid != oid) {
        remap.emplace(oid, nid);
      }
    }
  }

  std::vector<std::pair<SourceId, Entry>>   entries_;  // insertion order: parents precede combiners
  std::unordered_map<SourceId, uint32_t>    index_;    // id -> entries_ index
  // deque, NOT vector: Anchor.path is a string_view into File::path, and deque
  // push_back never moves existing elements (vector growth would dangle every
  // held Anchor whose path fits in the std::string SSO buffer).
  std::deque<File>                          files_;
  std::unordered_map<std::string, uint32_t> path_to_fid_;
  const Source_locator*                     base_      = nullptr;
  // Test seam: narrows every minted hash so collision/probe/merge-convergence
  // paths become reachable (64-bit rapidhash collisions cannot be constructed
  // in a test). Production code never changes it.
  uint64_t                                  hash_mask_ = ~uint64_t{0};

  friend class Source_locator_tester;
};

}  // namespace hhds
