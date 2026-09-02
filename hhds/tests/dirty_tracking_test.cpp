// Does a STRUCTURAL change with no attribute write still get saved?
//
// GraphLibrary::save skips a body whose `dirty_` is false. That flag used to be
// set by Attr_host::attr_note_modified() as well, so an attribute write marked
// the whole body dirty and forced a full re-save -- which is what blocked a
// cheap attribute-only save, and what accidentally covered every structural
// mutator that forgot to set the flag itself. Each round below saves once
// (clearing dirty_), makes ONE change of a single shape, saves again, reloads,
// and checks the change survived:
//   1. a bare typed node          (no attribute, no edge)
//   2. an attribute value         (must persist via the attr-tail rewrite)
//   3. a DELETED attribute value  (the tail must be truncated, not left stale)
//   4. an edge between two existing pins, on a graph nothing has walked yet
// 4 is the shape whose only mutation record lives inside
// patch_traversal_caches_for_edge, which is a no-op while the traversal caches
// are invalid -- i.e. on every freshly loaded graph.
//
// A final round covers the other half of "skip a clean body": clean is only
// clean relative to WHERE the body already sits, so a save to a new directory
// must still write it out.

#include <unistd.h>

#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

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

// The body.bin format version field: magic, then version, then the endian mark.
uint32_t version_of(const fs::path& body) {
  std::ifstream ifs(body, std::ios::binary);
  uint32_t      magic = 0, version = 0;
  ifs.read(reinterpret_cast<char*>(&magic), sizeof(magic));
  ifs.read(reinterpret_cast<char*>(&version), sizeof(version));
  return version;
}

void stamp_version(const fs::path& body, uint32_t version) {
  std::fstream fs_out(body, std::ios::binary | std::ios::in | std::ios::out);
  fs_out.seekp(sizeof(uint32_t));
  fs_out.write(reinterpret_cast<const char*>(&version), sizeof(version));
}

std::string read_all(const fs::path& path) {
  std::ifstream      ifs(path, std::ios::binary);
  std::ostringstream oss;
  oss << ifs.rdbuf();
  return oss.str();
}

}  // namespace

int main() {
  hhds::register_attr_tag<mark_t>("hhds.tests.dirty_mark");
  // PID-qualified: five fixed names under the machine-global temp dir collide
  // across concurrent runs (--runs_per_test, or two `bazel test` invocations),
  // and the loser dies in remove_all/create_directories on a half-written tree.
  const std::string tag = "hhds_dirty_tracking_test_" + std::to_string(::getpid());
  const auto        dir = fs::temp_directory_path() / tag;
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

  // --- an EDGE between two already-existing pins, with nothing else touched and
  // NO traversal beforehand. add_edge/del_edge record the mutation from inside
  // patch_traversal_caches_for_edge, which does nothing when both traversal
  // caches are already invalid -- and they always are on a freshly loaded graph.
  // A walk before the mutation validates the caches and hides the hole, so this
  // case deliberately does not walk.
  {
    hhds::GraphLibrary fifth;
    fifth.load(dir.string());
    auto g5 = fifth.find_io("m")->get_graph();
    g5->get_input_pin("a").connect_sink(g5->get_output_pin("z"));  // EDGE ONLY
    fifth.save(dir.string());
  }
  {
    hhds::GraphLibrary sixth;
    sixth.load(dir.string());
    auto   g6    = sixth.find_io("m")->get_graph();
    size_t edges = 0;
    for (auto edge : g6->get_input_pin("a").out_edges()) {
      (void)edge;
      ++edges;
    }
    if (edges != 2) {
      std::printf("FAIL: an edge added to a freshly loaded graph was not persisted (%zu sinks on 'a', want 2)\n", edges);
      return 1;
    }
    std::printf("ok: an edge-only change on an unwalked graph is marked dirty\n");
  }

  // --- save-as: a body that was materialized (so it is no longer in
  // pending_body_dir_ for the verbatim-copy pass) and is otherwise clean must
  // still be written at a NEW destination, or the module vanishes from it.
  {
    const auto other = fs::temp_directory_path() / (tag + "_saveas");
    fs::remove_all(other);
    {
      hhds::GraphLibrary tenth;
      tenth.load(dir.string());
      (void)tenth.find_io("m")->get_graph();  // materialize, change nothing
      tenth.save(other.string());
    }
    hhds::GraphLibrary eleventh;
    eleventh.load(other.string());
    auto g11 = eleventh.find_io("m")->get_graph();
    if (live_nodes(g11.get()) != 2) {
      std::printf("FAIL: save-as dropped the body of a materialized-but-clean module\n");
      return 1;
    }
    std::printf("ok: save-as writes a materialized-but-clean body\n");
    fs::remove_all(other);
  }

  // --- a body from an OLDER format version must be refused, not decoded.
  // load_body accepts exactly GRAPH_BODY_VERSION: bodies before 6 predate the
  // constant pool, so their CONST_NODE pins carry no pool slot and there is no
  // faithful upgrade -- reading one would publish constants with no value.
  // (This replaces the old v3..v5 "never tail-patch a legacy header" round:
  // with a single accepted version there is no legacy file left to patch.)
  // Bump `kBodyVersion` in step with GRAPH_BODY_VERSION in graph.cpp.
  {
    constexpr uint32_t kBodyVersion = 6;

    const auto legacy = fs::temp_directory_path() / (tag + "_legacy");
    fs::remove_all(legacy);
    {
      hhds::GraphLibrary seventh;
      seventh.load(dir.string());
      (void)seventh.find_io("m")->get_graph();
      seventh.save(legacy.string());
    }

    fs::path body;
    for (const auto& entry : fs::directory_iterator(legacy)) {
      if (entry.is_directory() && entry.path().filename().string().rfind("graph_", 0) == 0) {
        body = entry.path() / "body.bin";
      }
    }
    if (body.empty() || !fs::exists(body)) {
      std::printf("FAIL: no graph body directory was written for the legacy round\n");
      return 1;
    }
    if (version_of(body) != kBodyVersion) {
      std::printf("FAIL: expected a freshly saved body to be version %u, got %u\n", kBodyVersion, version_of(body));
      return 1;
    }

    stamp_version(body, kBodyVersion - 1);
    bool refused = false;
    try {
      hhds::GraphLibrary eighth;
      eighth.load(legacy.string());  // lazy: the body is read on first get_graph()
      (void)eighth.find_io("m")->get_graph();
    } catch (const std::exception&) {
      refused = true;
    }
    if (!refused) {
      std::printf("FAIL: a body one version older than the loader was decoded anyway\n");
      return 1;
    }
    if (version_of(body) != kBodyVersion - 1) {
      std::printf("FAIL: the refused load rewrote the body it could not read\n");
      return 1;
    }

    // Same bytes, current header: still a perfectly good body, and an
    // attribute-only save on it takes the in-place tail rewrite.
    stamp_version(body, kBodyVersion);
    {
      hhds::GraphLibrary ninth;
      ninth.load(legacy.string());
      auto g9 = ninth.find_io("m")->get_graph();
      (*g9->body().nodes(hhds::Node_order::forward).begin()).attr(mark_t{}).set(int32_t{99});
      ninth.save(legacy.string());
    }
    if (version_of(body) != kBodyVersion) {
      std::printf("FAIL: an attribute-only save left the body at version %u\n", version_of(body));
      return 1;
    }
    hhds::GraphLibrary tenth_again;
    tenth_again.load(legacy.string());
    auto g10 = tenth_again.find_io("m")->get_graph();
    auto n10 = g10->body().nodes(hhds::Node_order::forward).begin();
    if (!(*n10).attr(mark_t{}).has() || (*n10).attr(mark_t{}).get() != 99 || live_nodes(g10.get()) != 2) {
      std::printf("FAIL: the body did not survive the version stamp round-trip\n");
      return 1;
    }
    std::printf("ok: a body from an older format version is refused, not decoded\n");
    fs::remove_all(legacy);
  }

  // --- deleting an EDGELESS declared pin. Graph::delete_pin unlinks and
  // tombstones the PinEntry; its only mutation record used to live inside the
  // per-removed-edge loop, which runs zero times when the pin carries no edges.
  // The body then stayed clean, save() skipped it, and the reloaded body still
  // carried the stale entry. Observed as "body.bin was not rewritten at all",
  // which is exactly what leaves the tombstone unpersisted.
  {
    const auto pins = fs::temp_directory_path() / (tag + "_delpin");
    fs::remove_all(pins);
    hhds::GraphLibrary lib_p;
    auto               twelfth = lib_p.create_io("p");
    twelfth->add_input("a", 1);
    twelfth->add_input("b", 1);  // declared, never connected
    twelfth->add_output("z", 1);
    {
      auto gp = twelfth->create_graph();
      auto np = gp->create_node();
      np.set_type(hhds::Type{1});
      gp->get_input_pin("a").connect_sink(np.create_sink_pin(0));
      np.create_driver_pin(0).connect_sink(gp->get_output_pin("z"));
    }
    lib_p.save(pins.string());  // clears dirty_

    fs::path body;
    for (const auto& entry : fs::directory_iterator(pins)) {
      if (entry.is_directory() && entry.path().filename().string().rfind("graph_", 0) == 0) {
        body = entry.path() / "body.bin";
      }
    }
    if (body.empty() || !fs::exists(body)) {
      std::printf("FAIL: no graph body directory was written for the pin-delete round\n");
      return 1;
    }
    const auto before_bytes = read_all(body);

    twelfth->delete_input("b");  // EDGELESS PIN DELETE ONLY, on a clean body
    lib_p.save(pins.string());

    if (read_all(body) == before_bytes) {
      std::printf("FAIL: an edgeless-pin delete left body.bin untouched, so the stale pin entry survives\n");
      return 1;
    }
    hhds::GraphLibrary fourteenth;
    fourteenth.load(pins.string());
    auto g14 = fourteenth.find_io("p")->get_graph();
    if (live_nodes(g14.get()) != 1) {
      std::printf("FAIL: the pin delete damaged the body\n");
      return 1;
    }
    size_t a_edges = 0;
    for (auto edge : g14->get_input_pin("a").out_edges()) {
      (void)edge;
      ++a_edges;
    }
    if (a_edges != 1) {
      std::printf("FAIL: the pin delete dropped an unrelated edge (%zu on 'a', want 1)\n", a_edges);
      return 1;
    }
    std::printf("ok: an edgeless-pin delete marks the body for re-save\n");
    fs::remove_all(pins);
  }

  // --- the split itself. Every round above passes just as well if
  // attr_note_modified() goes back to setting dirty_ too, because a full
  // save_body() reproduces the same bytes -- so nothing so far actually pins
  // the optimisation this commit exists for. save_body's first act is to sweep
  // the graph directory of legacy "overflow_<i>.bin" files; save_attrs_only
  // never touches the directory. A planted overflow_-prefixed sentinel
  // therefore survives an attribute-only save and dies in a full one.
  {
    const auto probe = fs::temp_directory_path() / (tag + "_probe");
    fs::remove_all(probe);

    hhds::GraphLibrary lib_s;
    auto               io_s = lib_s.create_io("s");
    io_s->add_input("a", 1);
    io_s->add_output("z", 1);
    {
      auto gs = io_s->create_graph();
      auto ns = gs->create_node();
      ns.set_type(hhds::Type{1});
      gs->get_input_pin("a").connect_sink(ns.create_sink_pin(0));
      ns.create_driver_pin(0).connect_sink(gs->get_output_pin("z"));
    }
    lib_s.save(probe.string());

    fs::path gdir;
    for (const auto& entry : fs::directory_iterator(probe)) {
      if (entry.is_directory() && entry.path().filename().string().rfind("graph_", 0) == 0) {
        gdir = entry.path();
      }
    }
    if (gdir.empty()) {
      std::printf("FAIL: no graph body directory for the split probe\n");
      return 1;
    }
    const auto sentinel = gdir / "overflow_sentinel.bin";

    {  // attribute only -> the tail is patched in place, the directory untouched
      std::ofstream(sentinel, std::ios::binary).put('x');
      auto gs = lib_s.find_io("s")->get_graph();
      (*gs->body().nodes(hhds::Node_order::forward).begin()).attr(mark_t{}).set(int32_t{1});
      lib_s.save(probe.string());
      if (!fs::exists(sentinel)) {
        std::printf("FAIL: an attribute-only save rewrote the whole body (the dirty_/attrs_dirty_ split is inert)\n");
        return 1;
      }
    }
    {  // one structural change -> the full body write, sentinel swept away
      auto gs    = lib_s.find_io("s")->get_graph();
      auto extra = gs->create_node();
      extra.set_type(hhds::Type{2});
      lib_s.save(probe.string());
      if (fs::exists(sentinel)) {
        std::printf("FAIL: a structural change did not take the full save_body path\n");
        return 1;
      }
    }
    std::printf("ok: an attribute-only save patches the tail; a structural one rewrites the body\n");
    fs::remove_all(probe);
  }

  // --- a body.bin rewritten UNDER us. body_dir_ only says we once wrote that
  // file; another GraphLibrary over the same db directory moves the tail, and
  // patching at our now-stale offset lands mid-table and truncates the rest --
  // an unloadable body where the pre-split code merely lost the update.
  {
    const auto shared = fs::temp_directory_path() / (tag + "_shared");
    fs::remove_all(shared);

    hhds::GraphLibrary lib1;
    auto               io1 = lib1.create_io("t");
    io1->add_input("a", 1);
    io1->add_output("z", 1);
    {
      auto g1 = io1->create_graph();
      auto n1 = g1->create_node();
      n1.set_type(hhds::Type{1});
      g1->get_input_pin("a").connect_sink(n1.create_sink_pin(0));
      n1.create_driver_pin(0).connect_sink(g1->get_output_pin("z"));
    }
    lib1.save(shared.string());  // lib1 records offset/dir/size for this file

    {  // a second library grows the same body on disk
      hhds::GraphLibrary lib2;
      lib2.load(shared.string());
      auto g2 = lib2.find_io("t")->get_graph();
      for (int i = 0; i < 20; ++i) {
        auto n2 = g2->create_node();
        n2.set_type(hhds::Type{2});
      }
      lib2.save(shared.string());
    }

    // lib1 is still alive and still thinks the tail starts where it left it.
    {
      auto g1 = lib1.find_io("t")->get_graph();
      (*g1->body().nodes(hhds::Node_order::forward).begin()).attr(mark_t{}).set(int32_t{5});
      lib1.save(shared.string());
    }

    try {
      hhds::GraphLibrary check;
      check.load(shared.string());
      auto gc = check.find_io("t")->get_graph();
      if (live_nodes(gc.get()) == 0) {
        std::printf("FAIL: the body written over a rewritten file has no live nodes\n");
        return 1;
      }
      auto nc = gc->body().nodes(hhds::Node_order::forward).begin();
      if (!(*nc).attr(mark_t{}).has() || (*nc).attr(mark_t{}).get() != 5) {
        std::printf("FAIL: the attribute written over a rewritten file did not survive\n");
        return 1;
      }
    } catch (const std::exception& e) {
      std::printf("FAIL: a body.bin rewritten by another library was corrupted, not rewritten: %s\n", e.what());
      return 1;
    }
    std::printf("ok: a body.bin rewritten under us is written whole, never tail-patched\n");
    fs::remove_all(shared);
  }

  std::printf("PASS\n");
  return 0;
}
