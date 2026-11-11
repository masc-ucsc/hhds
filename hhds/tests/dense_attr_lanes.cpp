// A dense attribute store costs one slot per object, not one per key value.
//
// A flat key carries tag bits below the object index (a graph node key is `index << 3`, a pin key
// `index << 3 | 1` or `| 3`, a tree key `position << 1`). Dense_attr_map keeps one vector per tag
// value, so an attribute set on every node of a graph allocates one slot per node-table entry, and
// iteration still visits the keys in ascending order, which is what persistence writes.
#include <cstdint>
#include <cstdio>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "hhds/attr.hpp"

namespace {
struct Width {
  using value_type = uint32_t;
  using storage    = hhds::flat_storage;
  using layout     = hhds::dense_layout;
};
class Host : public hhds::Attr_host {
public:
  using hhds::Attr_host::load_attr_stores;
  using hhds::Attr_host::save_attr_stores;
  template <class Tag>
  hhds::AttrRef<Tag> attr(uint64_t key) {
    return {this, key};
  }

private:
  void attr_note_modified() noexcept override {}
};
void check(bool value, const char* what) {
  if (!value) {
    throw std::runtime_error(std::string("dense attribute lanes: ") + what);
  }
}
}  // namespace

int main() {
  using Map                  = hhds::detail::Dense_attr_map<uint32_t>;
  constexpr uint64_t objects = 10000;

  {  // every node of a graph: keys index << 3
    Map map;
    for (uint64_t index = 1; index <= objects; ++index) {
      map[hhds::make_node_attr_key(index << 2U)] = static_cast<uint32_t>(index);
    }
    check(map.size() == objects, "node attribute count");
    check(map.slot_count() == objects + 1, "one slot per node index (index 0 is the sentinel)");
    uint64_t expected = 1;
    for (const auto& [key, value] : map) {
      check(key == hhds::make_node_attr_key(expected << 2U) && value == expected, "node keys in order");
      ++expected;
    }
    check(expected == objects + 1, "every node visited");
  }

  {  // nodes, their pin 0 and PinEntry pins in one store: three lanes, no slot shared
    Map                          map;
    std::map<uint64_t, uint32_t> model;
    for (uint64_t index = 1; index <= 500; ++index) {
      const auto node = hhds::make_node_attr_key(index << 2U);
      const auto pin0 = hhds::make_pin_attr_key(index << 2U);
      const auto pin  = hhds::make_pin_attr_key((index << 2U) | 1U);
      map[node] = model[node] = static_cast<uint32_t>(3 * index);
      map.emplace(pin0, static_cast<uint32_t>(3 * index + 1));
      model[pin0] = static_cast<uint32_t>(3 * index + 1);
      if (index % 7 != 0) {
        map[pin] = model[pin] = static_cast<uint32_t>(3 * index + 2);
      }
    }
    check(map.size() == model.size(), "mixed count");
    check(map.slot_count() == 3 * 501, "three lanes of 501 slots");
    auto it = model.begin();
    for (const auto& [key, value] : map) {
      check(it != model.end() && key == it->first && value == it->second, "ascending key order across lanes");
      ++it;
    }
    check(it == model.end(), "every entry visited");
    for (const auto& [key, value] : model) {
      const auto found = map.find(key);
      check(found != map.end() && (*found).second == value, "find");
    }
    // A key whose lane was never written, and one past the end of its lane.
    check(map.find(hhds::make_node_attr_key(7U << 2U) | 4U) == map.end(), "unused lane");
    check(map.find(hhds::make_node_attr_key(uint64_t{100000} << 2U)) == map.end(), "past the lane");
    check(map.erase(hhds::make_node_attr_key(3U << 2U)) == 1, "erase present");
    check(map.erase(hhds::make_node_attr_key(3U << 2U)) == 0, "erase absent");
    check(map.find(hhds::make_node_attr_key(3U << 2U)) == map.end(), "erased is absent");
    check(map.find(hhds::make_pin_attr_key(3U << 2U)) != map.end(), "the neighbour lane keeps its value");
    check(map.size() == model.size() - 1, "count after erase");
    map.clear();
    check(map.empty() && map.begin() == map.end(), "clear");
  }

  {  // tree positions: keys position << 1 fall on four lanes, one slot per position in all
    Map map;
    for (uint64_t pos = 0; pos < 4000; ++pos) {
      map[hhds::make_node_attr_key(pos)] = static_cast<uint32_t>(pos + 1);
    }
    check(map.slot_count() == 4000, "one slot per tree position");
    uint64_t pos = 0;
    for (const auto& [key, value] : map) {
      check(key == hhds::make_node_attr_key(pos) && value == pos + 1, "tree keys in order");
      ++pos;
    }
    check(pos == 4000, "every position visited");
  }
  {  // an iterator from find() walks on from there; a lookup past everything is the end
    Map map;
    map[hhds::make_node_attr_key(2U << 2U)] = 20;
    map[hhds::make_pin_attr_key(2U << 2U)]  = 21;
    map[hhds::make_node_attr_key(9U << 2U)] = 90;
    auto it                                 = map.find(hhds::make_node_attr_key(2U << 2U));
    check(it != map.end() && (*it).second == 20, "find the first");
    ++it;
    check(it != map.end() && (*it).first == hhds::make_pin_attr_key(2U << 2U) && (*it).second == 21,
          "advance to the neighbour lane");
    ++it;
    check(it != map.end() && (*it).second == 90, "advance to a later slot");
    ++it;
    check(it == map.end(), "advance to the end");
    check(Map{}.begin() == Map{}.end(), "an empty store has no entries");
  }

  {  // storing the default value is del(): absent, not persisted, and no slot is allocated for it
    hhds::register_attr_tag<Width>("hhds.tests.dense_lanes.width");
    Host       host;
    const auto key = hhds::make_node_attr_key(5U << 2U);
    host.attr<Width>(key).set(7);
    check(host.attr<Width>(key).has() && host.attr<Width>(key).get() == 7, "a dense value");
    host.attr<Width>(key).set(0);
    check(!host.attr<Width>(key).has(), "a stored zero is absent");
    check(host.attr<Width>(key).get_or(42) == 42, "a stored zero reads as the fallback");
    host.attr<Width>(hhds::make_node_attr_key(uint64_t{1} << 30U)).set(0);  // would be gigabytes if it allocated
    std::ostringstream empty(std::ios::binary), one(std::ios::binary);
    host.save_attr_stores(empty);
    host.attr<Width>(key).set(9);
    host.save_attr_stores(one);
    check(one.str().size() > empty.str().size(), "only a present value is persisted");
    Host               loaded;
    std::istringstream in(one.str(), std::ios::binary);
    loaded.load_attr_stores(in);
    check(loaded.attr<Width>(key).get() == 9, "load");
  }
  std::puts("dense attribute lanes: ok");
  return 0;
}
