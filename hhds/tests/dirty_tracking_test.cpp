// Does a STRUCTURAL change with no attribute write and no edge still get saved?
//
// GraphLibrary::save skips a body whose `dirty_` is false. Today that flag is
// also set by Attr_host::attr_note_modified(), so an attribute write marks the
// whole body dirty and forces a full re-save -- which is what blocks a cheap
// attribute-only save. Before that coupling can be removed, the structural
// mutators have to set the flag on their own. This test pins exactly that: it
// saves once (clearing dirty_), then makes ONE structural change touching no
// attribute and no edge, saves again, and reloads.

#include <cassert>
#include <cstdio>
#include <filesystem>

#include "hhds/graph.hpp"

namespace fs = std::filesystem;

struct mark_t {
  using value_type = int32_t;
  using storage    = hhds::flat_storage;
};

namespace {

size_t live_nodes(hhds::Graph* g) {
  size_t n = 0;
  for (auto node : g->body().nodes(hhds::Node_order::forward)) {
    if (!node.is_invalid() && node.get_debug_nid() >= (static_cast<hhds::Nid>(4) << 2)) {
      ++n;
    }
  }
  return n;
}

}  // namespace

int main() {
  hhds::register_attr_tag<mark_t>("hhds.tests.dirty_mark");
  const auto dir = fs::temp_directory_path() / "hhds_dirty_tracking_test";
  fs::remove_all(dir);

  {
    hhds::GraphLibrary lib;
    auto               io = lib.create_io("m");
    io->add_input("a", 1);
    io->add_output("z", 2);
    {
      auto g = io->create_graph();
      auto n = g->create_node();
      n.set_type(hhds::Type{1});
      g->get_input_pin("a").connect_sink(n.create_sink_pin(0));
      n.create_driver_pin(0).connect_sink(g->get_output_pin("z"));
    }
    lib.save(dir.string());  // clears dirty_

    // ONE structural change: a new typed node. No attribute is written and no
    // edge is added, so neither attr_note_modified() nor the edge path fires.
    auto g     = lib.find_io("m")->get_graph();
    auto extra = g->create_node();
    extra.set_type(hhds::Type{2});
    lib.save(dir.string());
  }

  hhds::GraphLibrary loaded;
  loaded.load(dir.string());
  auto g = loaded.find_io("m")->get_graph();

  const auto n = live_nodes(g.get());
  std::printf("live nodes after reload: %zu (want 2)\n", n);
  if (n != 2) {
    std::printf("FAIL: a structural change with no attr write and no edge was NOT persisted\n");
    return 1;
  }
  std::printf("ok: structural changes are marked dirty on their own\n");

  // --- the other direction: an ATTRIBUTE-only change must still persist, now
  // that attr_note_modified() no longer marks the whole body dirty. This is the
  // save_attrs_only path.
  {
    auto first = g->body().nodes(hhds::Node_order::forward).begin();
    (*first).attr(mark_t{}).set(int32_t{4242});
    loaded.save(dir.string());
  }
  {
    hhds::GraphLibrary again;
    again.load(dir.string());
    auto g2 = again.find_io("m")->get_graph();
    auto n2 = g2->body().nodes(hhds::Node_order::forward).begin();
    if (!(*n2).attr(mark_t{}).has() || (*n2).attr(mark_t{}).get() != 4242) {
      std::printf("FAIL: an attribute-only change was not persisted\n");
      return 1;
    }
    if (live_nodes(g2.get()) != 2) {
      std::printf("FAIL: the attribute-only save damaged the node tables\n");
      return 1;
    }
    std::printf("ok: an attribute-only change persists without a full body save\n");
  }

  // --- shrink: dropping attributes must truncate, not leave stale bytes that
  // the next load would read as live entries.
  {
    hhds::GraphLibrary third;
    third.load(dir.string());
    auto g3 = third.find_io("m")->get_graph();
    for (auto node : g3->body().nodes(hhds::Node_order::forward)) {
      node.attr(mark_t{}).del();
    }
    third.save(dir.string());
  }
  {
    hhds::GraphLibrary fourth;
    fourth.load(dir.string());
    auto g4 = fourth.find_io("m")->get_graph();
    for (auto node : g4->body().nodes(hhds::Node_order::forward)) {
      if (node.attr(mark_t{}).has()) {
        std::printf("FAIL: a deleted attribute came back after an attr-only save (stale tail)\n");
        return 1;
      }
    }
    if (live_nodes(g4.get()) != 2) {
      std::printf("FAIL: the shrinking attr-only save damaged the node tables\n");
      return 1;
    }
    std::printf("ok: a shrinking attribute set truncates cleanly\n");
  }

  std::printf("PASS\n");
  return 0;
}
