#pragma once

#include <cassert>
#include <cstdint>
#include <functional>
#include <istream>
#include <memory>
#include <ostream>
#include <string>
#include <string_view>
#include <type_traits>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

#ifdef HHDS_ATTR_PROFILE
#include <cstdio>
#include <cstdlib>
#include <map>
#include <mutex>
#endif

#include "hhds/graph_sizing.hpp"

namespace hhds {

struct flat_storage {};
struct hier_storage {};

template <class Tag>
concept Attribute = requires {
  typename Tag::value_type;
  typename Tag::storage;
  requires std::is_same_v<typename Tag::storage, flat_storage> || std::is_same_v<typename Tag::storage, hier_storage>;
};

// Optional per-tag layout policy (`using layout = hhds::dense_layout;`).
// Default is sparse (hash map). dense_layout backs the store with a plain
// vector indexed by the flat key; it is only valid for flat_storage tags.
//
// dense_layout contract: value_type{} is reserved as the not-present
// sentinel (hhds ids reserve 0 as invalid), so a dense tag must never store
// value_type{} — set() asserts on it; use del() instead. Iteration over a
// dense store yields proxy entries: bind with `auto&&` or `const auto&`,
// never `auto&`.
//
// Build with -DHHDS_ATTR_PROFILE (bazel --config=attr_profile) to dump
// per-tag utilization at exit and get a dense-vs-sparse recommendation.
struct sparse_layout {};
struct dense_layout {};

template <class Tag>
struct attr_layout {
  using type = sparse_layout;
};

template <class Tag>
  requires requires { typename Tag::layout; }
struct attr_layout<Tag> {
  using type = typename Tag::layout;
};

template <class Tag>
using attr_layout_t = typename attr_layout<Tag>::type;

template <Attribute Tag>
[[nodiscard]] constexpr bool attr_is_dense() noexcept {
  using Layout = attr_layout_t<Tag>;
  static_assert(std::is_same_v<Layout, sparse_layout> || std::is_same_v<Layout, dense_layout>,
                "attribute Tag::layout must be hhds::sparse_layout or hhds::dense_layout");
  if constexpr (std::is_same_v<Layout, dense_layout>) {
    static_assert(std::is_same_v<typename Tag::storage, flat_storage>,
                  "dense_layout requires flat_storage; hier_storage attributes must stay sparse");
    return true;
  } else {
    return false;
  }
}

template <Attribute Tag>
using attr_result_t
    = std::conditional_t<std::is_trivially_copyable_v<typename Tag::value_type> && sizeof(typename Tag::value_type) <= 16,
                         typename Tag::value_type, const typename Tag::value_type&>;

using Attr_key = uint64_t;

struct Hier_attr_key {
  int64_t  hier_pos = 0;
  Attr_key flat_key = 0;

  [[nodiscard]] bool operator==(const Hier_attr_key& other) const noexcept {
    return hier_pos == other.hier_pos && flat_key == other.flat_key;
  }
};

struct Hier_attr_key_hash {
  [[nodiscard]] size_t operator()(const Hier_attr_key& key) const noexcept {
    const size_t h1 = std::hash<int64_t>{}(key.hier_pos);
    const size_t h2 = std::hash<Attr_key>{}(key.flat_key);
    return h1 ^ (h2 + 0x9e3779b97f4a7c15ULL + (h1 << 6U) + (h1 >> 2U));
  }
};

[[nodiscard]] constexpr Attr_key make_node_attr_key(uint64_t raw_id) noexcept { return (raw_id << 1U) | 0U; }
[[nodiscard]] constexpr Attr_key make_pin_attr_key(uint64_t raw_id) noexcept { return (raw_id << 1U) | 1U; }

enum class Attr_storage_kind : uint8_t { Flat = 0, Hier = 1 };

template <Attribute Tag>
[[nodiscard]] constexpr Attr_storage_kind attr_storage_kind() noexcept {
  if constexpr (std::is_same_v<typename Tag::storage, flat_storage>) {
    return Attr_storage_kind::Flat;
  } else {
    return Attr_storage_kind::Hier;
  }
}

template <Attribute Tag>
[[nodiscard]] inline std::string attr_tag_name() {
  return typeid(Tag).name();
}

template <typename T>
struct dependent_false : std::false_type {};

namespace detail {

class Attr_store_base;

struct Attr_tag_registry_entry {
  std::type_index                                   type_key{typeid(void)};
  Attr_storage_kind                                 storage_kind = Attr_storage_kind::Flat;
  std::string                                       persistent_id;
  std::function<std::unique_ptr<Attr_store_base>()> factory;
};

class Attr_tag_registry {
public:
  [[nodiscard]] static Attr_tag_registry& instance() {
    static Attr_tag_registry registry;
    return registry;
  }

  template <Attribute Tag>
  const Attr_tag_registry_entry& register_tag(std::string_view persistent_id);

  template <Attribute Tag>
  const Attr_tag_registry_entry& ensure_tag() {
    const auto type_key = std::type_index(typeid(Tag));
    const auto it       = by_type_.find(type_key);
    if (it != by_type_.end()) {
      return it->second;
    }
    return register_tag<Tag>(attr_tag_name<Tag>());
  }

  [[nodiscard]] const Attr_tag_registry_entry* find(std::string_view persistent_id) const {
    const auto it = by_id_.find(std::string(persistent_id));
    if (it == by_id_.end()) {
      return nullptr;
    }
    const auto type_it = by_type_.find(it->second);
    return type_it == by_type_.end() ? nullptr : &type_it->second;
  }

private:
  std::unordered_map<std::type_index, Attr_tag_registry_entry> by_type_;
  std::unordered_map<std::string, std::type_index>             by_id_;
};

template <typename T>
void write_value(std::ostream& os, const T& value) {
  if constexpr (std::is_same_v<T, std::string>) {
    const uint64_t size = value.size();
    os.write(reinterpret_cast<const char*>(&size), sizeof(size));
    if (size != 0) {
      os.write(value.data(), static_cast<std::streamsize>(size));
    }
  } else if constexpr (std::is_trivially_copyable_v<T>) {
    os.write(reinterpret_cast<const char*>(&value), sizeof(value));
  } else {
    static_assert(dependent_false<T>::value, "Attribute persistence supports only std::string and trivially copyable values");
  }
}

template <typename T>
T read_value(std::istream& is) {
  if constexpr (std::is_same_v<T, std::string>) {
    uint64_t size = 0;
    is.read(reinterpret_cast<char*>(&size), sizeof(size));
    std::string value(size, '\0');
    if (size != 0) {
      is.read(value.data(), static_cast<std::streamsize>(size));
    }
    return value;
  } else if constexpr (std::is_trivially_copyable_v<T>) {
    T value{};
    is.read(reinterpret_cast<char*>(&value), sizeof(value));
    return value;
  } else {
    static_assert(dependent_false<T>::value, "Attribute persistence supports only std::string and trivially copyable values");
  }
}

// Vector-backed store for dense_layout attributes. value_type{} marks an
// absent entry (see the dense_layout contract above), so presence needs no
// extra bitmap. Exposes the subset of the unordered_map surface that
// Attr_store_impl and AttrRef use; iterators yield proxy entries by value.
template <typename T>
class Dense_attr_map {
public:
  using key_type    = Attr_key;
  using mapped_type = T;

  template <bool IsConst>
  class basic_iterator {
  public:
    using vector_type    = std::conditional_t<IsConst, const std::vector<T>, std::vector<T>>;
    using reference_type = std::conditional_t<IsConst, const T&, T&>;

    struct entry {
      Attr_key       first;
      reference_type second;
    };

    struct arrow_proxy {
      entry  value;
      entry* operator->() noexcept { return &value; }
    };

    basic_iterator() = default;
    basic_iterator(vector_type* data, size_t pos) : data_(data), pos_(pos) { skip_absent(); }

    [[nodiscard]] entry       operator*() const noexcept { return entry{static_cast<Attr_key>(pos_), (*data_)[pos_]}; }
    [[nodiscard]] arrow_proxy operator->() const noexcept { return arrow_proxy{**this}; }

    basic_iterator& operator++() noexcept {
      ++pos_;
      skip_absent();
      return *this;
    }

    [[nodiscard]] bool operator==(const basic_iterator& other) const noexcept { return pos_ == other.pos_; }

  private:
    void skip_absent() noexcept {
      while (data_ != nullptr && pos_ < data_->size() && (*data_)[pos_] == T{}) {
        ++pos_;
      }
    }

    vector_type* data_ = nullptr;
    size_t       pos_  = 0;
  };

  using iterator       = basic_iterator<false>;
  using const_iterator = basic_iterator<true>;

  [[nodiscard]] iterator       begin() noexcept { return iterator(&data_, 0); }
  [[nodiscard]] iterator       end() noexcept { return iterator(&data_, data_.size()); }
  [[nodiscard]] const_iterator begin() const noexcept { return const_iterator(&data_, 0); }
  [[nodiscard]] const_iterator end() const noexcept { return const_iterator(&data_, data_.size()); }

  [[nodiscard]] iterator find(Attr_key key) noexcept {
    if (key < data_.size() && !(data_[key] == T{})) {
      return iterator(&data_, static_cast<size_t>(key));
    }
    return end();
  }

  [[nodiscard]] const_iterator find(Attr_key key) const noexcept {
    if (key < data_.size() && !(data_[key] == T{})) {
      return const_iterator(&data_, static_cast<size_t>(key));
    }
    return end();
  }

  [[nodiscard]] T& operator[](Attr_key key) {
    if (key >= data_.size()) {
      data_.resize(static_cast<size_t>(key) + 1);
    }
    return data_[static_cast<size_t>(key)];
  }

  template <typename V>
  void emplace(Attr_key key, V&& value) {
    (*this)[key] = std::forward<V>(value);
  }

  size_t erase(Attr_key key) {
    if (key < data_.size() && !(data_[key] == T{})) {
      data_[static_cast<size_t>(key)] = T{};
      return 1;
    }
    return 0;
  }

  void clear() noexcept { data_.clear(); }

  [[nodiscard]] size_t size() const noexcept {
    size_t count = 0;
    for (const auto& value : data_) {
      if (!(value == T{})) {
        ++count;
      }
    }
    return count;
  }

  [[nodiscard]] bool empty() const noexcept {
    for (const auto& value : data_) {
      if (!(value == T{})) {
        return false;
      }
    }
    return true;
  }

private:
  std::vector<T> data_;
};

#ifdef HHDS_ATTR_PROFILE
// Aggregates per-tag occupancy from flat Attr_store_impl destructors and
// dumps a dense-vs-sparse recommendation at exit. Immortal (never deleted):
// records arrive during static destruction, after atexit handlers started.
// Stores still alive at process exit are not counted.
class Attr_profile {
public:
  static Attr_profile& instance() {
    static auto* profile = new Attr_profile();
    return *profile;
  }

  void record(std::string_view persistent_id, bool is_dense, uint64_t value_bytes, uint64_t entries, uint64_t key_span) {
    const std::lock_guard<std::mutex> guard(mutex_);
    auto&                             stat  = stats_[std::string(persistent_id)];
    stat.is_dense                           = is_dense;
    stat.value_bytes                        = value_bytes;
    stat.stores                            += 1;
    stat.entries                           += entries;
    stat.key_span                          += key_span;
  }

private:
  struct Stat {
    bool     is_dense    = false;
    uint64_t value_bytes = 0;
    uint64_t stores      = 0;
    uint64_t entries     = 0;
    uint64_t key_span    = 0;
  };

  Attr_profile() {
    std::atexit([]() { instance().dump(); });
  }

  void dump() const {
    const std::lock_guard<std::mutex> guard(mutex_);
    if (stats_.empty()) {
      return;
    }
    std::fprintf(stderr, "\nhhds attr profile (flat stores, aggregated at store destruction):\n");
    for (const auto& [id, stat] : stats_) {
      // Dense pays value_bytes per key slot; sparse pays key+value plus hash
      // node overhead per present entry.
      const uint64_t dense_bytes  = stat.key_span * stat.value_bytes;
      const uint64_t sparse_bytes = stat.entries * (sizeof(Attr_key) + stat.value_bytes + 16);
      const double   utilization
          = stat.key_span == 0 ? 0.0 : 100.0 * static_cast<double>(stat.entries) / static_cast<double>(stat.key_span);
      std::fprintf(stderr,
                   "  %-44s layout=%-6s stores=%llu entries=%llu key_span=%llu util=%5.1f%% recommend=%s\n",
                   id.c_str(),
                   stat.is_dense ? "dense" : "sparse",
                   static_cast<unsigned long long>(stat.stores),
                   static_cast<unsigned long long>(stat.entries),
                   static_cast<unsigned long long>(stat.key_span),
                   utilization,
                   dense_bytes <= sparse_bytes ? "dense" : "sparse");
    }
  }

  mutable std::mutex          mutex_;
  std::map<std::string, Stat> stats_;
};
#endif

class Attr_store_base {
public:
  virtual ~Attr_store_base() = default;

  [[nodiscard]] virtual Attr_storage_kind                storage_kind() const noexcept                  = 0;
  [[nodiscard]] virtual std::type_index                  type_key() const noexcept                      = 0;
  [[nodiscard]] virtual std::string_view                 persistent_id() const noexcept                 = 0;
  [[nodiscard]] virtual bool                             empty() const noexcept                         = 0;
  [[nodiscard]] virtual uint64_t                         size() const noexcept                          = 0;
  virtual void                                           clear_entries() noexcept                       = 0;
  virtual void                                           erase_object(Attr_key key) noexcept            = 0;
  virtual void                                           save_entries(std::ostream& os) const           = 0;
  virtual void                                           load_entries(std::istream& is, uint64_t count) = 0;
  [[nodiscard]] virtual std::unique_ptr<Attr_store_base> clone() const                                  = 0;
};

template <Attribute Tag>
using attr_map_t = std::conditional_t<
    attr_is_dense<Tag>(), Dense_attr_map<typename Tag::value_type>,
    std::conditional_t<std::is_same_v<typename Tag::storage, flat_storage>, std::unordered_map<Attr_key, typename Tag::value_type>,
                       std::unordered_map<Hier_attr_key, typename Tag::value_type, Hier_attr_key_hash>>>;

template <Attribute Tag>
class Attr_store_impl final : public Attr_store_base {
public:
  using map_type   = attr_map_t<Tag>;
  using value_type = typename Tag::value_type;

  explicit Attr_store_impl(std::string persistent_id) : persistent_id_(std::move(persistent_id)) {}

#ifdef HHDS_ATTR_PROFILE
  ~Attr_store_impl() override {
    if constexpr (std::is_same_v<typename Tag::storage, flat_storage>) {
      uint64_t entries  = 0;
      uint64_t key_span = 0;
      for (const auto& [key, value] : map_) {
        ++entries;
        if (static_cast<uint64_t>(key) + 1 > key_span) {
          key_span = static_cast<uint64_t>(key) + 1;
        }
      }
      if (entries != 0) {
        Attr_profile::instance().record(persistent_id_, attr_is_dense<Tag>(), sizeof(value_type), entries, key_span);
      }
    }
  }
#endif

  [[nodiscard]] Attr_storage_kind storage_kind() const noexcept override { return attr_storage_kind<Tag>(); }
  [[nodiscard]] std::type_index   type_key() const noexcept override { return std::type_index(typeid(Tag)); }
  [[nodiscard]] std::string_view  persistent_id() const noexcept override { return persistent_id_; }
  [[nodiscard]] bool              empty() const noexcept override { return map_.empty(); }
  [[nodiscard]] uint64_t          size() const noexcept override { return static_cast<uint64_t>(map_.size()); }
  void                            clear_entries() noexcept override { map_.clear(); }

  void erase_object(Attr_key key) noexcept override {
    if constexpr (std::is_same_v<typename Tag::storage, flat_storage>) {
      map_.erase(key);
    } else {
      for (auto it = map_.begin(); it != map_.end();) {
        if (it->first.flat_key == key) {
          it = map_.erase(it);
        } else {
          ++it;
        }
      }
    }
  }

  void save_entries(std::ostream& os) const override {
    if constexpr (std::is_same_v<typename Tag::storage, flat_storage>) {
      for (const auto& [key, value] : map_) {
        os.write(reinterpret_cast<const char*>(&key), sizeof(key));
        write_value<value_type>(os, value);
      }
    } else {
      for (const auto& [key, value] : map_) {
        os.write(reinterpret_cast<const char*>(&key.hier_pos), sizeof(key.hier_pos));
        os.write(reinterpret_cast<const char*>(&key.flat_key), sizeof(key.flat_key));
        write_value<value_type>(os, value);
      }
    }
  }

  void load_entries(std::istream& is, uint64_t count) override {
    map_.clear();
    for (uint64_t i = 0; i < count; ++i) {
      if constexpr (std::is_same_v<typename Tag::storage, flat_storage>) {
        Attr_key key = 0;
        is.read(reinterpret_cast<char*>(&key), sizeof(key));
        map_.emplace(key, read_value<value_type>(is));
      } else {
        Hier_attr_key key{};
        is.read(reinterpret_cast<char*>(&key.hier_pos), sizeof(key.hier_pos));
        is.read(reinterpret_cast<char*>(&key.flat_key), sizeof(key.flat_key));
        map_.emplace(key, read_value<value_type>(is));
      }
    }
  }

  [[nodiscard]] map_type&       map() noexcept { return map_; }
  [[nodiscard]] const map_type& map() const noexcept { return map_; }

  [[nodiscard]] std::unique_ptr<Attr_store_base> clone() const override {
    auto copy  = std::make_unique<Attr_store_impl<Tag>>(persistent_id_);
    copy->map_ = map_;
    return copy;
  }

private:
  std::string persistent_id_;
  map_type    map_;
};

template <Attribute Tag>
const Attr_tag_registry_entry& Attr_tag_registry::register_tag(std::string_view persistent_id) {
  const auto type_key = std::type_index(typeid(Tag));
  const auto type_it  = by_type_.find(type_key);
  if (type_it != by_type_.end()) {
    if (!persistent_id.empty()) {
      if (type_it->second.persistent_id != persistent_id) {
        const std::string default_id = attr_tag_name<Tag>();
        assert(type_it->second.persistent_id == default_id && "register_tag: conflicting persistent ids for attribute tag");

        const auto current_id_it = by_id_.find(type_it->second.persistent_id);
        if (current_id_it != by_id_.end() && current_id_it->second == type_key) {
          by_id_.erase(current_id_it);
        }

        [[maybe_unused]] const auto new_id_it = by_id_.find(std::string(persistent_id));
        assert((new_id_it == by_id_.end() || new_id_it->second == type_key)
               && "register_tag: persistent id already registered for another attribute tag");

        type_it->second.persistent_id = std::string(persistent_id);
        type_it->second.factory = [id = type_it->second.persistent_id]() { return std::make_unique<Attr_store_impl<Tag>>(id); };
        by_id_.emplace(type_it->second.persistent_id, type_key);
      }
    }
    return type_it->second;
  }

  const std::string           id    = persistent_id.empty() ? attr_tag_name<Tag>() : std::string(persistent_id);
  [[maybe_unused]] const auto id_it = by_id_.find(id);
  assert(id_it == by_id_.end() && "register_tag: persistent id already registered for another attribute tag");

  Attr_tag_registry_entry entry;
  entry.type_key      = type_key;
  entry.storage_kind  = attr_storage_kind<Tag>();
  entry.persistent_id = id;
  entry.factory       = [id]() { return std::make_unique<Attr_store_impl<Tag>>(id); };

  by_id_.emplace(entry.persistent_id, type_key);
  auto [it, inserted] = by_type_.emplace(type_key, std::move(entry));
  assert(inserted && "register_tag: failed to insert attribute registry entry");
  return it->second;
}

}  // namespace detail

template <Attribute Tag>
inline void register_attr_tag(std::string_view persistent_id) {
  (void)detail::Attr_tag_registry::instance().register_tag<Tag>(persistent_id);
}

class Attr_host;

template <Attribute Tag>
class AttrRef {
public:
  using value_type = typename Tag::value_type;

  AttrRef() = default;
  AttrRef(Attr_host* host, Attr_key flat_key) : host_(host), flat_key_(flat_key) {}
  AttrRef(Attr_host* host, Attr_key flat_key, int64_t hier_pos)
      : host_(host), flat_key_(flat_key), hier_pos_(hier_pos), has_hier_(true) {}

  [[nodiscard]] bool               has() const;
  [[nodiscard]] attr_result_t<Tag> get() const;
  void                             set(const value_type& value);
  void                             set(value_type&& value);
  void                             del();

private:
  [[nodiscard]] auto key() const;

  Attr_host* host_     = nullptr;
  Attr_key   flat_key_ = 0;
  int64_t    hier_pos_ = 0;
  bool       has_hier_ = false;
};

class Attr_host {
public:
  virtual ~Attr_host() = default;

  template <Attribute Tag>
  auto& attr_store(Tag = {}) {
    const auto key = std::type_index(typeid(Tag));
    auto       it  = attr_stores_.find(key);
    if (it == attr_stores_.end()) {
      // Cold path only: consult the global tag registry to mint the store the
      // first time this Tag is seen. The steady-state set/del path (store
      // already present) skips the registry lookup entirely.
      const auto& desc        = detail::Attr_tag_registry::instance().ensure_tag<Tag>();
      auto [new_it, inserted] = attr_stores_.emplace(key, desc.factory());
      assert(inserted && "attr_store: failed to create store");
      it = new_it;
    }
    auto* typed = static_cast<detail::Attr_store_impl<Tag>*>(it->second.get());
    return typed->map();
  }

  template <Attribute Tag>
  [[nodiscard]] const auto* find_attr_store(Tag = {}) const {
    const auto it = attr_stores_.find(std::type_index(typeid(Tag)));
    if (it == attr_stores_.end()) {
      return static_cast<const typename detail::Attr_store_impl<Tag>::map_type*>(nullptr);
    }
    const auto* typed = static_cast<const detail::Attr_store_impl<Tag>*>(it->second.get());
    return &typed->map();
  }

  template <Attribute Tag>
  void attr_clear(Tag = {}) {
    auto& store = attr_store(Tag{});
    store.clear();
    attr_note_modified();
  }

  template <Attribute Tag>
  [[nodiscard]] bool has_attr(Tag = {}) const {
    return attr_stores_.find(std::type_index(typeid(Tag))) != attr_stores_.end();
  }

protected:
  void erase_attr_object(Attr_key key) noexcept {
    for (auto& [_, store] : attr_stores_) {
      store->erase_object(key);
    }
  }

  void discard_attr_stores() noexcept { attr_stores_.clear(); }

  void clone_attr_stores_from(const Attr_host& other) {
    discard_attr_stores();
    for (const auto& [key, store] : other.attr_stores_) {
      attr_stores_.emplace(key, store->clone());
    }
  }

  void save_attr_stores(std::ostream& os) const {
    uint64_t store_count = 0;
    for (const auto& [_, store] : attr_stores_) {
      if (!store->empty()) {
        ++store_count;
      }
    }

    os.write(reinterpret_cast<const char*>(&store_count), sizeof(store_count));
    for (const auto& [_, store] : attr_stores_) {
      if (store->empty()) {
        continue;
      }

      const auto id_size = static_cast<uint64_t>(store->persistent_id().size());
      os.write(reinterpret_cast<const char*>(&id_size), sizeof(id_size));
      os.write(store->persistent_id().data(), static_cast<std::streamsize>(id_size));

      const auto storage_kind = static_cast<uint8_t>(store->storage_kind());
      os.write(reinterpret_cast<const char*>(&storage_kind), sizeof(storage_kind));

      const auto entry_count = store->size();
      os.write(reinterpret_cast<const char*>(&entry_count), sizeof(entry_count));
      store->save_entries(os);
    }
  }

  void load_attr_stores(std::istream& is) {
    discard_attr_stores();

    uint64_t store_count = 0;
    is.read(reinterpret_cast<char*>(&store_count), sizeof(store_count));
    for (uint64_t i = 0; i < store_count; ++i) {
      uint64_t id_size = 0;
      is.read(reinterpret_cast<char*>(&id_size), sizeof(id_size));
      std::string persistent_id(id_size, '\0');
      if (id_size != 0) {
        is.read(persistent_id.data(), static_cast<std::streamsize>(id_size));
      }

      uint8_t storage_kind_u8 = 0;
      is.read(reinterpret_cast<char*>(&storage_kind_u8), sizeof(storage_kind_u8));
      [[maybe_unused]] const auto storage_kind = static_cast<Attr_storage_kind>(storage_kind_u8);

      uint64_t entry_count = 0;
      is.read(reinterpret_cast<char*>(&entry_count), sizeof(entry_count));

      const auto* desc = detail::Attr_tag_registry::instance().find(persistent_id);
      assert(desc != nullptr && "load_attr_stores: attribute tag was not registered for deserialization");
      assert(desc->storage_kind == storage_kind && "load_attr_stores: persisted attribute storage kind mismatch");

      auto store = desc->factory();
      store->load_entries(is, entry_count);
      attr_stores_[desc->type_key] = std::move(store);
    }
  }

private:
  virtual void attr_note_modified() noexcept = 0;

  std::unordered_map<std::type_index, std::unique_ptr<detail::Attr_store_base>> attr_stores_;

  template <Attribute Tag>
  friend class AttrRef;
};

template <Attribute Tag>
inline auto AttrRef<Tag>::key() const {
  assert(host_ != nullptr && "AttrRef: no attribute host");
  if constexpr (std::is_same_v<typename Tag::storage, flat_storage>) {
    return flat_key_;
  } else {
    assert(has_hier_ && "AttrRef: hier attribute requires hierarchy context");
    return Hier_attr_key{hier_pos_, flat_key_};
  }
}

template <Attribute Tag>
inline bool AttrRef<Tag>::has() const {
  const auto* map = host_ != nullptr ? host_->find_attr_store(Tag{}) : nullptr;
  if (map == nullptr) {
    if constexpr (std::is_same_v<typename Tag::storage, hier_storage>) {
      assert(has_hier_ && "AttrRef::has: hier attribute requires hierarchy context");
    }
    return false;
  }
  return map->find(key()) != map->end();
}

template <Attribute Tag>
inline attr_result_t<Tag> AttrRef<Tag>::get() const {
  const auto* map = host_ != nullptr ? host_->find_attr_store(Tag{}) : nullptr;
  assert(map != nullptr && "AttrRef::get: attribute store is not registered");
  const auto it = map->find(key());
  assert(it != map->end() && "AttrRef::get: attribute value is not set");
  if constexpr (std::is_same_v<attr_result_t<Tag>, typename Tag::value_type>) {
    return it->second;
  } else {
    return it->second;
  }
}

template <Attribute Tag>
inline void AttrRef<Tag>::set(const value_type& value) {
  if constexpr (attr_is_dense<Tag>()) {
    assert(!(value == value_type{}) && "AttrRef::set: dense_layout reserves value_type{} as not-present; use del()");
  }
  auto& map  = host_->attr_store(Tag{});
  map[key()] = value;
  host_->attr_note_modified();
}

template <Attribute Tag>
inline void AttrRef<Tag>::set(value_type&& value) {
  if constexpr (attr_is_dense<Tag>()) {
    assert(!(value == value_type{}) && "AttrRef::set: dense_layout reserves value_type{} as not-present; use del()");
  }
  auto& map  = host_->attr_store(Tag{});
  map[key()] = std::move(value);
  host_->attr_note_modified();
}

template <Attribute Tag>
inline void AttrRef<Tag>::del() {
  auto& map = host_->attr_store(Tag{});
  map.erase(key());
  host_->attr_note_modified();
}

}  // namespace hhds
