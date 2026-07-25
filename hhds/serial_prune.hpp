// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace hhds::serial {

// Shared by GraphLibrary::save (graph.cpp) and Forest::save (tree_serial.cpp).
//
// A save writes an authoritative declaration file (`library.txt` / `forest.txt`)
// plus one body directory per live entity. Nothing used to remove the body
// directories of entities the artifact NO LONGER holds, so re-emitting a
// different — or merely smaller — artifact over a populated directory left the
// previous run's bodies behind. That is dead weight on disk, and a real hazard
// once an id is recreated: a lazy body load finds the stale body sitting exactly
// where the fresh one belongs.
//
// prune_body_dirs makes db_path describe exactly what was just saved: every
// `<prefix><digits>` sub-directory whose id `keep` rejects is removed. Entries
// that do not match `<prefix><digits>` are never touched, so a caller may keep
// unrelated files next to an artifact.
//
// Stale entries are first renamed into a single trash directory — one O(1)
// rename each, so a save never walks a large tree inline — and that trash is
// then removed in one call. The removal is deliberately synchronous: deleting a
// whole 172-graph / 38 MB library measures ~15 ms, which does not justify
// spawning a background worker inside a library that is otherwise thread-free
// (and is built under tsan). Moving the final remove_all onto a worker is the
// obvious change if a profile ever disagrees.
//
// The trash directory name starts with '.', so it can never collide with a body
// directory and every loader ignores it. If a process dies mid-prune, the next
// prune of the same directory sweeps it.
template <typename KeepFn>
inline void prune_body_dirs(const std::string& db_path, std::string_view prefix, KeepFn keep) {
  namespace fs = std::filesystem;

  std::vector<fs::path> stale;
  {
    std::error_code scan_ec;
    for (const auto& entry : fs::directory_iterator(db_path, scan_ec)) {
      const std::string name = entry.path().filename().string();
      if (!std::string_view{name}.starts_with(prefix)) {
        continue;
      }
      const std::string_view digits{name.data() + prefix.size(), name.size() - prefix.size()};
      if (digits.empty()) {
        continue;
      }
      uint64_t    id       = 0;
      const char* one_past = digits.data() + digits.size();
      const auto  conv     = std::from_chars(digits.data(), one_past, id);
      if (conv.ec != std::errc{} || conv.ptr != one_past) {
        continue;  // not `<prefix><number>` — not ours to touch
      }
      if (keep(id)) {
        continue;
      }
      std::error_code dir_ec;
      if (!entry.is_directory(dir_ec) || dir_ec) {
        continue;
      }
      stale.emplace_back(entry.path());
    }
  }
  if (stale.empty()) {
    return;
  }

  std::error_code ec;
  const auto      trash = fs::path(db_path) / ".hhds_pruned";
  fs::remove_all(trash, ec);
  ec.clear();
  fs::create_directories(trash, ec);
  ec.clear();
  for (size_t i = 0; i < stale.size(); ++i) {
    fs::rename(stale[i], trash / std::to_string(i), ec);
    if (ec) {  // cross-device, or the trash could not be made: delete in place
      ec.clear();
      fs::remove_all(stale[i], ec);
      ec.clear();
    }
  }
  fs::remove_all(trash, ec);
}

}  // namespace hhds::serial
