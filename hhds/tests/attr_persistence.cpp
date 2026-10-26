// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include <algorithm>
#include <cstdint>
#include <limits>
#include <numeric>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "hhds/attr.hpp"

namespace {
struct Integer {
  using value_type = uint32_t;
  using storage    = hhds::flat_storage;
};
struct Text {
  using value_type = std::string;
  using storage    = hhds::flat_storage;
};
struct Dense {
  using value_type = uint32_t;
  using storage    = hhds::flat_storage;
  using layout     = hhds::dense_layout;
};
struct Flag {
  using value_type = hhds::flag;
  using storage    = hhds::flat_storage;
};
struct Segment {
  int64_t layer, x1, y1, x2, y2;
  bool    operator==(const Segment&) const = default;
};
struct Routes {
  using value_type = std::vector<Segment>;
  using storage    = hhds::flat_storage;
  using layout     = hhds::dense_layout;
};
struct Nested {
  using value_type = std::vector<std::vector<std::string>>;
  using storage    = hhds::flat_storage;
};
struct Bits {
  using value_type = std::vector<bool>;
  using storage    = hhds::flat_storage;
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
std::string save(const Host& host) {
  std::ostringstream stream(std::ios::binary);
  host.save_attr_stores(stream);
  return stream.str();
}
void check(bool value) {
  if (!value) {
    throw std::runtime_error("attribute persistence changed with hash iteration order");
  }
}
}  // namespace
int main() {
  hhds::register_attr_tag<Integer>("hhds.persistence.integer");
  hhds::register_attr_tag<Text>("hhds.persistence.text");
  hhds::register_attr_tag<Dense>("hhds.persistence.dense");
  hhds::register_attr_tag<Flag>("hhds.persistence.flag");
  hhds::register_attr_tag<Routes>("hhds.persistence.routes");
  hhds::register_attr_tag<Nested>("hhds.persistence.nested");
  hhds::register_attr_tag<Bits>("hhds.persistence.bits");
  {
    Host                 host;
    std::vector<Segment> segments(10000, {3, 1, 2, 100, 2});
    host.attr<Routes>(3).set(segments);
    host.attr<Nested>(4).set({
        {"signal", std::string("a\0b", 3)},
        {},
        {std::string(10000, 'x')}
    });
    host.attr<Nested>(7).set({});
    host.attr<Bits>(1).set({true, false, true});
    check(!host.attr<Routes>(5).has());  // dense default-value sentinel
    check(host.attr<Nested>(7).has());   // sparse retains an empty payload
    const auto         bytes = save(host);
    Host               loaded;
    std::istringstream stream(bytes, std::ios::binary);
    loaded.load_attr_stores(stream);
    check(loaded.attr<Routes>(3).get() == segments);
    check(!loaded.attr<Routes>(5).has());
    check(loaded.attr<Nested>(4).get() == host.attr<Nested>(4).get());
    check(loaded.attr<Nested>(7).has());
    check(loaded.attr<Bits>(1).get() == host.attr<Bits>(1).get());
    check(save(loaded) == bytes);
    loaded.attr<Routes>(3).del();
    check(!loaded.attr<Routes>(3).has());
    loaded.attr<Routes>(3).set({
        {4, 0, 0, 1, 0}
    });
    check(loaded.attr<Routes>(3).get().size() == 1);
  }
  const auto rejects = [](auto type, const std::string& bytes) {
    using T = decltype(type);
    std::istringstream stream(bytes, std::ios::binary);
    bool               rejected = false;
    try {
      (void)hhds::detail::read_value<T>(stream);
    } catch (const std::runtime_error&) {
      rejected = true;
    }
    check(rejected);
  };
  rejects(std::vector<int64_t>{}, std::string(3, '\0'));
  std::ostringstream bad(std::ios::binary);
  hhds::detail::write_value(bad, std::numeric_limits<uint64_t>::max());
  rejects(std::vector<int64_t>{}, bad.str());
  rejects(std::vector<std::string>{}, bad.str());
  bad.str("");
  bad.clear();
  hhds::detail::write_value(bad, uint64_t{100000000});
  rejects(std::vector<int64_t>{}, bad.str());  // bounded allocation on short input
  rejects(std::string{}, bad.str());
  bad.str("");
  bad.clear();
  hhds::detail::write_value(bad, uint64_t{1});
  hhds::detail::write_value(bad, uint8_t{2});
  rejects(std::vector<bool>{}, bad.str());
  bad.str("");
  bad.clear();
  hhds::detail::write_value(bad, std::vector<std::string>{"abc"});
  rejects(std::vector<std::string>{}, bad.str().substr(0, bad.str().size() - 1));
  std::vector<uint64_t> keys(2048);
  std::iota(keys.begin(), keys.end(), uint64_t{1});
  std::mt19937 random(42);
  std::string  reference;
  for (int trial = 0; trial < 5; ++trial) {
    std::shuffle(keys.begin(), keys.end(), random);
    Host host;
    for (auto key : keys) {
      host.attr<Integer>(key).set(static_cast<uint32_t>(key * 3));
      host.attr<Text>(key).set(std::to_string(key));
      host.attr<Dense>(key).set(static_cast<uint32_t>(key));
      if (key % 3 == 0) {
        host.attr<Flag>(key).set();
      }
    }
    // Exercise erase/reinsert as well as separate hash-table allocation seeds.
    for (auto key : keys) {
      host.attr<Integer>(key).del();
      host.attr<Integer>(key).set(static_cast<uint32_t>(key * 3));
    }
    const auto bytes = save(host);
    if (trial == 0) {
      reference = bytes;
    } else {
      check(bytes == reference);
    }
    Host               loaded;
    std::istringstream input(bytes, std::ios::binary);
    loaded.load_attr_stores(input);
    for (auto key : keys) {
      check(loaded.attr<Integer>(key).get() == key * 3);
      check(loaded.attr<Text>(key).get() == std::to_string(key));
      check(loaded.attr<Dense>(key).get() == key);
      check(loaded.attr<Flag>(key).has() == (key % 3 == 0));
    }
    check(save(loaded) == reference);
    loaded.attr<Integer>(1).set(9999);
    check(save(loaded) != reference);
  }
}
