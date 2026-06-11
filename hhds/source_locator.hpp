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

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iostream>
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

  Source_locator() = default;

  // ---- minting (single-writer; no locks by design) -------------------------

  // Span anchor: the common case. `line` is stored so resolution never needs to
  // re-read the source file.
  SourceId mint(std::string_view path, uint64_t start_byte, uint64_t end_byte, uint32_t line) {
    return insert_anchor(hash_span(path, start_byte, end_byte), Anchor_kind::Span, path, start_byte, end_byte, line);
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
  // full LSP-grade intervals — without storing columns per entry.
  void set_file_content_hash(std::string_view path, uint64_t hash) { files_[intern_file(path)].content_hash = hash; }

  [[nodiscard]] uint64_t file_content_hash(std::string_view path) const {
    const File* f = find_file(path);
    return f == nullptr ? 0 : f->content_hash;
  }

  void set_file_line_offsets(std::string_view path, std::vector<uint64_t> offsets) {
    files_[intern_file(path)].line_offsets = std::move(offsets);
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
  };

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
    files_.push_back(File{std::string(path), 0, {}});
    path_to_fid_.emplace(files_.back().path, fid);
    return fid;
  }

  static Anchor make_anchor(const Entry& e, const Source_locator& owner) {
    return Anchor{owner.file_path(e.file_id), e.start_byte, e.end_byte, e.line, e.kind};
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
