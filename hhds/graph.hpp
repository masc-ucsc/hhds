#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/inlined_vector.h"
#include "attr.hpp"
#include "attrs/name.hpp"
#include "attrs/srcid.hpp"
#include "graph_sizing.hpp"
#include "index.hpp"
#include "rapidhash.h"
#include "source_locator.hpp"
#include "str_hash.hpp"  // Name_hash/Name_eq: module + IO-pin names match case-sensitively
#include "tree.hpp"
#include "unordered_dense.hpp"

namespace hhds {

using OverflowVec = std::vector<ankerl::unordered_dense::set<Vid>>;
using OverflowSet = ankerl::unordered_dense::set<Vid>;

// Writer-preferring shared mutex.
//
// libstdc++'s std::shared_mutex wraps a glibc pthread_rwlock with the default
// PTHREAD_RWLOCK_PREFER_READER policy, under which a continuous stream of
// readers can starve a waiting writer INDEFINITELY. The GraphLibrary registry
// is read on essentially every traversal and written by create_io /
// create_graph / commit; a tight reader poll loop (e.g. find_io in a hot loop,
// or many concurrent readers on a high-core host) then blocks every writer
// forever — observed as a hang in create_io/create_graph_body. (It "works" on
// low-core machines only because readers don't overlap enough to keep the lock
// continuously held.)
//
// This variant gates NEW readers as soon as a writer is waiting, so a waiting
// writer always drains the current readers and makes progress. It exposes the
// BasicLockable + SharedLockable surface, so std::unique_lock / std::shared_lock
// keep working unchanged. It is NOT recursive: a thread must not re-acquire it
// while already holding it (GraphLibrary always takes the registry lock once per
// public call and delegates to *_unlocked helpers, so this invariant holds).
class Prefer_writer_shared_mutex {
public:
  void lock() {  // exclusive
    std::unique_lock<std::mutex> lk(mu_);
    ++writers_waiting_;
    gate_.wait(lk, [this] { return !writer_active_ && readers_ == 0; });
    --writers_waiting_;
    writer_active_ = true;
  }

  void unlock() {
    {
      std::lock_guard<std::mutex> lk(mu_);
      writer_active_ = false;
    }
    gate_.notify_all();
  }

  void lock_shared() {
    std::unique_lock<std::mutex> lk(mu_);
    // Prefer writers: a reader waits while a writer is active OR queued.
    gate_.wait(lk, [this] { return !writer_active_ && writers_waiting_ == 0; });
    ++readers_;
  }

  void unlock_shared() {
    bool last = false;
    {
      std::lock_guard<std::mutex> lk(mu_);
      last = (--readers_ == 0);
    }
    if (last) {
      gate_.notify_all();  // wake a queued writer once the last reader leaves
    }
  }

private:
  std::mutex              mu_;
  std::condition_variable gate_;
  unsigned                readers_         = 0;
  unsigned                writers_waiting_ = 0;
  bool                    writer_active_   = false;
};

// Forward iterator over the edges of a node or pin. Two modes:
//   - Inline: walks a contiguous buffer owned by the EdgeRange (decoded sedges + ledges).
//   - Overflow: walks the OverflowSet (unordered_dense::set<Vid>) in-place, no copy.
class EdgeIterator {
public:
  using iterator_category = std::forward_iterator_tag;
  using value_type        = Vid;
  using reference         = Vid;
  using pointer           = const Vid*;
  using difference_type   = std::ptrdiff_t;
  using OverflowIter      = OverflowSet::const_iterator;

  EdgeIterator() = default;
  explicit EdgeIterator(const Vid* p) noexcept : inline_ptr_(p) {}
  explicit EdgeIterator(OverflowIter it) noexcept : over_it_(it), overflow_(true) {}

  Vid operator*() const noexcept { return overflow_ ? *over_it_ : *inline_ptr_; }

  EdgeIterator& operator++() noexcept {
    if (overflow_) {
      ++over_it_;
    } else {
      ++inline_ptr_;
    }
    return *this;
  }
  EdgeIterator operator++(int) noexcept {
    EdgeIterator tmp = *this;
    ++*this;
    return tmp;
  }

  bool operator==(const EdgeIterator& o) const noexcept {
    return overflow_ ? (over_it_ == o.over_it_) : (inline_ptr_ == o.inline_ptr_);
  }
  bool operator!=(const EdgeIterator& o) const noexcept { return !(*this == o); }

private:
  OverflowIter over_it_{};
  const Vid*   inline_ptr_ = nullptr;
  bool         overflow_   = false;
};

struct OverflowPool {
  OverflowVec&           sets;
  std::vector<uint32_t>& free_list;

  uint32_t alloc() {
    if (!free_list.empty()) {
      uint32_t idx = free_list.back();
      free_list.pop_back();
      sets[idx].clear();
      return idx;
    }
    sets.emplace_back();
    return static_cast<uint32_t>(sets.size() - 1);
  }

  void free(uint32_t idx) {
    sets[idx].clear();
    free_list.push_back(idx);
  }
};

class Graph;
class GraphIO;
class Node_class;
class Pin_class;
class Edge_class;
class Tree;

class FastClassIterator;
class FastClassRange;
class FastFlatIterator;
class FastFlatRange;
class FastHierIterator;
class FastHierRange;
class ForwardClassIterator;
class ForwardClassRange;
class ForwardFlatIterator;
class ForwardFlatRange;
class ForwardHierIterator;
class ForwardHierRange;
class BackwardClassIterator;
class BackwardClassRange;
class BackwardFlatIterator;
class BackwardFlatRange;
class BackwardHierIterator;
class BackwardHierRange;
class Hier_instance;
class HierIterator;
class HierRange;
class OutEdgeIterator;
class OutEdgeRange;

enum class Handle_context : uint8_t { Class, Flat, Hier };

class Pin_class {
public:
  Pin_class() = default;
  Pin_class(Graph* graph_value, Pid pin_pid_value) : graph_(graph_value), pin_pid(pin_pid_value) {}
  explicit Pin_class(Pid pin_pid_value) : pin_pid(pin_pid_value) {}

  [[nodiscard]] Node_class       get_master_node() const;
  [[nodiscard]] Nid              get_debug_nid() const noexcept;
  [[nodiscard]] constexpr Pid    get_debug_pid() const noexcept { return pin_pid; }
  [[nodiscard]] Port_id          get_port_id() const noexcept;
  [[nodiscard]] Graph*           get_graph() const noexcept { return graph_; }
  [[nodiscard]] std::string_view get_pin_name() const;
  [[nodiscard]] bool             is_valid() const noexcept;
  [[nodiscard]] bool             is_invalid() const noexcept { return !is_valid(); }
  [[nodiscard]] bool             is_class() const noexcept { return context_ == Handle_context::Class; }
  [[nodiscard]] bool             is_flat() const noexcept { return context_ == Handle_context::Flat; }
  [[nodiscard]] bool             is_hier() const noexcept { return context_ == Handle_context::Hier; }
  // Pin polarity: bit 1 (mask 2) of pin_pid is set on driver pins, clear on
  // sink pins. Storage-level invariant maintained by create_driver_pin /
  // create_sink_pin in graph.cpp.
  [[nodiscard]] constexpr bool   is_driver() const noexcept { return (pin_pid & static_cast<Pid>(2)) != 0; }
  [[nodiscard]] constexpr bool   is_sink() const noexcept { return (pin_pid & static_cast<Pid>(2)) == 0; }
  [[nodiscard]] Handle_context   get_context() const noexcept { return context_; }
  [[nodiscard]] Gid              get_root_gid() const noexcept;
  [[nodiscard]] Gid              get_current_gid() const noexcept;
  [[nodiscard]] Tree_pos         get_hier_pos() const noexcept { return hier_pos_; }
  // Hier: full instance chain (subnode nids root..immediate-parent) that locates
  // this handle's body unambiguously even when its graph is instantiated more
  // than once. Empty/null for root-level or non-hier handles. Used by the
  // cross-boundary edge resolver; see inp_edges/out_edges.
  [[nodiscard]] const std::shared_ptr<const std::vector<Nid>>& get_hier_path() const noexcept { return hier_path_; }
  // Verilog-style dotted hierarchical name: "inst.inst...node.pin" (the pin port
  // name is appended only for non-zero ports). See Node_class::get_hier_name.
  [[nodiscard]] std::string                                    get_hier_name() const;

  // Opaque, hashable keys for use in user-owned maps. See hhds/index.hpp for
  // semantics. Prefer these over using Pin_class directly as a map key.
  [[nodiscard]] Class_index get_class_index() const noexcept { return Class_index{pin_pid}; }
  [[nodiscard]] Flat_index  get_flat_index() const noexcept {
    assert(graph_ != nullptr && "get_flat_index: pin is not attached to a graph");
    return Flat_index{get_current_gid(), pin_pid};
  }
  [[nodiscard]] Hier_index get_hier_index() const noexcept {
    assert(context_ == Handle_context::Hier && "get_hier_index: requires hier traversal context");
    return Hier_index{get_current_gid(), hier_pos_, pin_pid};
  }

  void                                             connect_driver(Pin_class driver_pin) const;
  void                                             connect_sink(Pin_class sink_pin) const;
  void                                             del_sink(Pin_class driver_pin) const;
  void                                             del_sink() const;
  void                                             del_driver() const;
  void                                             del_pin() const;
  // out_edges() is a LAZY, auto-scaling view (see OutEdgeRange). ~90% of pins
  // have a handful of out edges, but a few (clock/reset/enable) fan out to
  // 100K+; the range walks live storage on demand — inline edges decode cheaply
  // into a small stack buffer, huge overflow sets are iterated in place with no
  // copy — so the *same* call is efficient for both and supports early `break`.
  // It is a view over live storage: snapshot before mutating during iteration
  // (see OutEdgeRange docs).
  [[nodiscard]] OutEdgeRange                       out_edges() const;
  // inp_edges() stays eager: a sink's fan-in is small (usually a single driver),
  // so the heap-free InlinedVector is the right shape; [4] inline covers it.
  [[nodiscard]] absl::InlinedVector<Edge_class, 4> inp_edges() const;
  // Drivers feeding this sink pin (the far end of each inp edge). A sink's
  // fan-in is small — usually a single driver in a well-formed net — so this
  // materializes into the same heap-free InlinedVector the other pin/node
  // accessors use. Asymmetric on purpose: a driver's fanout (out_edges) can be
  // huge, so there is no eager get_sink_pins() companion here.
  [[nodiscard]] absl::InlinedVector<Pin_class, 4>  get_driver_pins() const;

  template <Attribute Tag>
  [[nodiscard]] AttrRef<Tag> attr(Tag = {}) const {
    assert(graph_ != nullptr && "attr: pin is not attached to a graph");
    const auto flat_key = make_pin_attr_key(static_cast<uint64_t>(pin_pid & ~static_cast<Pid>(2)));
    if (context_ == Handle_context::Hier) {
      return AttrRef<Tag>(graph_, flat_key, hier_pos_);
    }
    return AttrRef<Tag>(graph_, flat_key);
  }

  [[nodiscard]] bool operator==(const Pin_class& other) const noexcept { return pin_pid == other.pin_pid; }
  [[nodiscard]] bool operator!=(const Pin_class& other) const noexcept { return !(*this == other); }

  template <typename H>
  friend H AbslHashValue(H h, const Pin_class& pin) {
    return H::combine(std::move(h), pin.pin_pid);
  }

private:
  Graph*                                  graph_    = nullptr;
  Pid                                     pin_pid   = 0;
  Handle_context                          context_  = Handle_context::Class;
  Gid                                     root_gid_ = Gid_invalid;  // Flat & Hier: root graph of the traversal
  Tree_pos                                hier_pos_ = INVALID;      // Hier: per-instance token (parent's structure-tree pos)
  std::shared_ptr<const std::vector<Nid>> hier_path_;               // Hier: full root..parent instance chain

  friend class Graph;
  friend class GraphLibrary;
  friend class Node_class;
  friend class OutEdgeIterator;  // builds/stamps driver+sink pins while walking out edges
  friend void inherit_pin_context(Pin_class& pin, const Node_class& node);
};

class Node_class {
public:
  using Context = Handle_context;

  Node_class() = default;
  Node_class(Graph* graph_value, Nid raw_nid_value) : graph_(graph_value), raw_nid(raw_nid_value) {}
  Node_class(Graph* graph_value, Gid root_gid_value, Nid raw_nid_value)
      : graph_(graph_value), raw_nid(raw_nid_value), context_(Context::Flat), root_gid_(root_gid_value) {}
  Node_class(Graph* graph_value, Gid root_gid_value, Tree_pos hier_pos_value, Nid raw_nid_value,
             std::shared_ptr<const std::vector<Nid>> hier_path_value = nullptr)
      : graph_(graph_value)
      , raw_nid(raw_nid_value)
      , context_(Context::Hier)
      , root_gid_(root_gid_value)
      , hier_pos_(hier_pos_value)
      , hier_path_(std::move(hier_path_value)) {}
  explicit Node_class(Nid raw_nid_value) : raw_nid(raw_nid_value) {}

  [[nodiscard]] constexpr Port_id                              get_port_id() const noexcept { return 0; }
  [[nodiscard]] constexpr Nid                                  get_debug_nid() const noexcept { return raw_nid; }
  [[nodiscard]] Graph*                                         get_graph() const noexcept { return graph_; }
  [[nodiscard]] bool                                           is_valid() const noexcept;
  [[nodiscard]] bool                                           is_invalid() const noexcept { return !is_valid(); }
  [[nodiscard]] bool                                           is_class() const noexcept { return context_ == Context::Class; }
  [[nodiscard]] bool                                           is_flat() const noexcept { return context_ == Context::Flat; }
  [[nodiscard]] bool                                           is_hier() const noexcept { return context_ == Context::Hier; }
  [[nodiscard]] Context                                        get_context() const noexcept { return context_; }
  [[nodiscard]] Gid                                            get_root_gid() const noexcept;
  [[nodiscard]] Gid                                            get_current_gid() const noexcept;
  [[nodiscard]] Tree_pos                                       get_hier_pos() const noexcept { return hier_pos_; }
  // See Pin_class::get_hier_path.
  [[nodiscard]] const std::shared_ptr<const std::vector<Nid>>& get_hier_path() const noexcept { return hier_path_; }
  // See class comment: "inst.inst...node" from the instance chain plus this node.
  [[nodiscard]] std::string                                    get_hier_name() const;

  // Convenience name accessors backed by hhds::attrs::name — the SAME attr that
  // get_hier_name() / hier_local_name() read. LiveHD stamps the RTL instance
  // name (or a flop's register name) here via set_name() so that get_hier_name()
  // yields the Verilog-style hierarchical path instead of the "n<id>" fallback.
  void                           set_name(std::string_view name) const;
  [[nodiscard]] std::string_view get_name() const;

  // Opaque, hashable keys for use in user-owned maps. See hhds/index.hpp for
  // semantics. Prefer these over using Node_class directly as a map key.
  [[nodiscard]] Class_index get_class_index() const noexcept { return Class_index{raw_nid}; }
  [[nodiscard]] Flat_index  get_flat_index() const noexcept {
    assert(graph_ != nullptr && "get_flat_index: node is not attached to a graph");
    return Flat_index{get_current_gid(), raw_nid};
  }
  [[nodiscard]] Hier_index get_hier_index() const noexcept {
    assert(context_ == Context::Hier && "get_hier_index: requires hier traversal context");
    return Hier_index{get_current_gid(), hier_pos_, raw_nid};
  }

  void                                             set_subnode(const std::shared_ptr<GraphIO>& graphio) const;
  // Inverse accessors for set_subnode. All three return the "no subnode"
  // sentinel (nullptr / Gid_invalid) when the node has none.
  [[nodiscard]] Gid                                get_subnode_gid() const;
  [[nodiscard]] std::shared_ptr<GraphIO>           get_subnode_io() const;
  [[nodiscard]] std::shared_ptr<Graph>             get_subnode_graph() const;
  void                                             set_type(Type type) const;
  [[nodiscard]] Type                               get_type() const;
  [[nodiscard]] bool                               is_loop_break() const;
  [[nodiscard]] Pin_class                          create_driver_pin() const;
  [[nodiscard]] Pin_class                          create_driver_pin(Port_id port_id) const;
  [[nodiscard]] Pin_class                          create_driver_pin(std::string_view name) const;
  [[nodiscard]] Pin_class                          create_sink_pin() const;
  [[nodiscard]] Pin_class                          create_sink_pin(Port_id port_id) const;
  [[nodiscard]] Pin_class                          create_sink_pin(std::string_view name) const;
  [[nodiscard]] Pin_class                          get_driver_pin(Port_id port_id) const;
  [[nodiscard]] Pin_class                          get_driver_pin(std::string_view name) const;
  [[nodiscard]] Pin_class                          get_sink_pin(Port_id port_id) const;
  [[nodiscard]] Pin_class                          get_sink_pin(std::string_view name) const;
  void                                             del_node() const;
  // Lazy, auto-scaling out-edge view (see OutEdgeRange / Pin_class::out_edges).
  // In Class/Flat context this walks live storage on demand; in Hier context it
  // resolves cross-boundary edges (materialized) behind the same range type.
  [[nodiscard]] OutEdgeRange                       out_edges() const;
  [[nodiscard]] absl::InlinedVector<Edge_class, 4> inp_edges() const;
  [[nodiscard]] absl::InlinedVector<Pin_class, 4>  out_pins() const;
  [[nodiscard]] absl::InlinedVector<Pin_class, 4>  inp_pins() const;
  // Fast boolean predicates — avoid materializing the full edge vector when
  // callers only need an "any?" answer (LiveHD's Lgraph::has_outputs /
  // has_inputs hot paths).
  [[nodiscard]] bool                               has_out_edges() const;
  [[nodiscard]] bool                               has_inp_edges() const;

  template <Attribute Tag>
  [[nodiscard]] AttrRef<Tag> attr(Tag = {}) const {
    assert(graph_ != nullptr && "attr: node is not attached to a graph");
    const auto flat_key = make_node_attr_key(static_cast<uint64_t>(raw_nid & ~static_cast<Nid>(3)));
    if (context_ == Context::Hier) {
      return AttrRef<Tag>(graph_, flat_key, hier_pos_);
    }
    return AttrRef<Tag>(graph_, flat_key);
  }

  [[nodiscard]] bool operator==(const Node_class& other) const noexcept { return raw_nid == other.raw_nid; }
  [[nodiscard]] bool operator!=(const Node_class& other) const noexcept { return !(*this == other); }

  template <typename H>
  friend H AbslHashValue(H h, const Node_class& node) {
    return H::combine(std::move(h), node.raw_nid);
  }

private:
  Graph*                                  graph_    = nullptr;
  Nid                                     raw_nid   = 0;
  Context                                 context_  = Context::Class;
  Gid                                     root_gid_ = Gid_invalid;  // Flat & Hier: root graph of the traversal
  Tree_pos                                hier_pos_ = INVALID;      // Hier: per-instance token (parent's structure-tree pos)
  std::shared_ptr<const std::vector<Nid>> hier_path_;               // Hier: full root..parent instance chain

  friend class Graph;
  friend void inherit_pin_context(Pin_class& pin, const Node_class& node);
};

class Edge_class {
public:
  void del_edge() const;

  Pin_class driver;
  Pin_class sink;

private:
  friend class Graph;
};

// Handle for one subnode instance in the structure-tree walk driven by
// Graph::hier_range(). Unlike Node_class (which yields every graph node),
// Hier_instance yields exactly once per subnode — so hierarchy traversals
// pay O(hier_size), not O(graph_size). Carries enough state to both
// identify the instance (parent_graph + tree_pos) and resolve what it
// expands into (target_gid). hier_pos matches fast_hier/forward_hier's
// per-instance token so per-instance attribute lookups agree across APIs.
class Hier_instance {
public:
  Hier_instance() = default;
  Hier_instance(Graph* parent_graph, Gid root_gid, Tree_pos hier_pos, Tree_pos tree_pos, Nid parent_nid)
      : parent_graph_(parent_graph), root_gid_(root_gid), hier_pos_(hier_pos), tree_pos_(tree_pos), parent_nid_(parent_nid) {}

  [[nodiscard]] Graph*                 get_parent_graph() const noexcept { return parent_graph_; }
  [[nodiscard]] Gid                    get_root_gid() const noexcept { return root_gid_; }
  [[nodiscard]] Tree_pos               get_tree_pos() const noexcept { return tree_pos_; }
  [[nodiscard]] Tree_pos               get_hier_pos() const noexcept { return hier_pos_; }
  [[nodiscard]] Nid                    get_parent_nid() const noexcept { return parent_nid_; }
  [[nodiscard]] Gid                    get_target_gid() const;
  [[nodiscard]] std::shared_ptr<Graph> get_target_graph() const;
  [[nodiscard]] Node_class             get_parent_node() const;
  [[nodiscard]] bool                   is_valid() const noexcept;
  [[nodiscard]] bool                   is_invalid() const noexcept { return !is_valid(); }

  [[nodiscard]] bool operator==(const Hier_instance& other) const noexcept {
    return parent_graph_ == other.parent_graph_ && tree_pos_ == other.tree_pos_ && hier_pos_ == other.hier_pos_;
  }
  [[nodiscard]] bool operator!=(const Hier_instance& other) const noexcept { return !(*this == other); }

  template <typename H>
  friend H AbslHashValue(H h, const Hier_instance& inst) {
    return H::combine(std::move(h), reinterpret_cast<uintptr_t>(inst.parent_graph_), inst.tree_pos_, inst.hier_pos_);
  }

private:
  Graph*   parent_graph_ = nullptr;
  Gid      root_gid_     = Gid_invalid;
  Tree_pos hier_pos_     = INVALID;
  Tree_pos tree_pos_     = INVALID;
  Nid      parent_nid_   = 0;

  friend class Graph;
  friend class HierIterator;
};

using Node = Node_class;
using Pin  = Pin_class;

class GraphLibrary;

class Graph : public Attr_host {
  class __attribute__((packed)) PinEntry {
    friend class Graph;
    friend class ForwardClassIterator;

  public:
    PinEntry();
    PinEntry(Nid master_nid_value, Port_id port_id_value);

    [[nodiscard]] Nid      get_master_nid() const { return master_nid; }
    [[nodiscard]] Port_id  get_port_id() const { return port_id; }
    auto                   add_edge(Pid self_id, Pid other_id, OverflowPool& pool) -> bool;
    auto                   delete_edge(Pid self_id, Pid other_id, OverflowPool& pool) -> bool;
    [[nodiscard]] bool     has_edges() const;
    [[nodiscard]] Pid      get_next_pin_id() const { return next_pin_id; }
    void                   set_next_pin_id(Pid id) { next_pin_id = id; }
    [[nodiscard]] bool     check_overflow() const { return use_overflow; }
    [[nodiscard]] uint32_t get_overflow_idx() const { return sedges_.overflow_idx; }

    static constexpr size_t MAX_EDGES = 8;

    class EdgeRange {
    public:
      // Inline capacity: 4 packed sedge slots + ledge0 + ledge1.
      static constexpr size_t kInlineMax = 6;
      using iterator                     = EdgeIterator;

      EdgeRange(const PinEntry* pin, Pid pid, const OverflowVec& overflow) noexcept;
      // begin()/end() hand out iterators into inline_buf_, which lives inside
      // this object. Copying/moving an EdgeRange would dangle those iterators,
      // so forbid it — range-for and `auto r = get_edges(...)` still compile via
      // guaranteed copy elision (get_edges returns a prvalue).
      EdgeRange(const EdgeRange&)            = delete;
      EdgeRange& operator=(const EdgeRange&) = delete;
      EdgeRange(EdgeRange&&)                 = delete;
      EdgeRange& operator=(EdgeRange&&)      = delete;

      iterator begin() const noexcept { return overflow_set_ ? iterator(overflow_set_->begin()) : iterator(inline_buf_.data()); }
      iterator end() const noexcept {
        return overflow_set_ ? iterator(overflow_set_->end()) : iterator(inline_buf_.data() + inline_count_);
      }

    private:
      std::array<Vid, kInlineMax> inline_buf_{};
      const OverflowSet*          overflow_set_ = nullptr;
      uint8_t                     inline_count_ = 0;
    };
    [[nodiscard]] auto get_edges(Pid pid, const OverflowVec& overflow) const noexcept -> EdgeRange;

  private:
    auto overflow_handling(Pid self_id, Vid other_id, OverflowPool& pool) -> bool;

    Nid     master_nid   : Nid_bits;   // 42 bits
    Port_id port_id      : Port_bits;  // 22 bits    => 64 bits (8 bytes)
    Pid     next_pin_id  : Nid_bits;   // 42 bits
    Nid     ledge0       : Nid_bits;   // 42 bits to too far node/pin (does not fit in sedge) => 64 bits (8 bytes)
    Nid     ledge1       : Nid_bits;   // 42 bits to too far node/pin (does not fit in sedge) => 64 bits (8 bytes)
    uint8_t use_overflow : 1;          // 1 bit      => 64 bits (8 bytes)

    union {
      int64_t  sedges;        // 4 × 16-bit packed slots (when use_overflow == 0)
      uint32_t overflow_idx;  // index into Graph::overflow_sets_ (when use_overflow == 1)
    } sedges_;                // Total: 8 bytes
  };

  static_assert(sizeof(PinEntry) == 32, "PinEntry size mismatch");

  class __attribute__((packed)) NodeEntry {
    friend class Graph;
    friend class ForwardClassIterator;

  public:
    NodeEntry();
    explicit NodeEntry(bool alive_value);

    [[nodiscard]] bool     is_alive() const noexcept { return alive != 0; }
    [[nodiscard]] Type     get_type() const { return static_cast<Type>(type); }
    void                   set_type(Type t) { type = t; }
    [[nodiscard]] bool     is_loop_break() const noexcept { return (type & 1u) != 0u; }
    [[nodiscard]] Pid      get_next_pin_id() const { return next_pin_id; }
    void                   set_next_pin_id(Pid id) { next_pin_id = id; }
    [[nodiscard]] bool     has_edges(const OverflowVec& overflow) const;
    auto                   add_edge(Pid self_id, Pid other_id, OverflowPool& pool) -> bool;
    auto                   delete_edge(Pid self_id, Pid other_id, OverflowPool& pool) -> bool;
    [[nodiscard]] bool     check_overflow() const { return use_overflow; }
    [[nodiscard]] uint32_t get_overflow_idx() const { return sedges_.overflow_idx; }

    void               set_subnode(Nid self_nid, Gid gid, OverflowPool& pool);
    [[nodiscard]] Gid  get_subnode() const noexcept;
    [[nodiscard]] bool has_subnode() const noexcept;

    static constexpr size_t MAX_EDGES = 8;

    class EdgeRange {
    public:
      // Inline capacity: 4 packed sedge slots + 3 extra slots in sedges_extra + ledge0 + ledge1.
      static constexpr size_t kInlineMax = 9;
      using iterator                     = EdgeIterator;

      EdgeRange(const NodeEntry* node, Nid nid, const OverflowVec& overflow) noexcept;
      // See PinEntry::EdgeRange: iterators point into inline_buf_, so copy/move
      // would dangle. Prvalue return + copy elision keep all callers valid.
      EdgeRange(const EdgeRange&)            = delete;
      EdgeRange& operator=(const EdgeRange&) = delete;
      EdgeRange(EdgeRange&&)                 = delete;
      EdgeRange& operator=(EdgeRange&&)      = delete;

      iterator begin() const noexcept { return overflow_set_ ? iterator(overflow_set_->begin()) : iterator(inline_buf_.data()); }
      iterator end() const noexcept {
        return overflow_set_ ? iterator(overflow_set_->end()) : iterator(inline_buf_.data() + inline_count_);
      }

    private:
      std::array<Vid, kInlineMax> inline_buf_{};
      const OverflowSet*          overflow_set_ = nullptr;
      uint8_t                     inline_count_ = 0;
    };

    [[nodiscard]] auto get_edges(Nid nid, const OverflowVec& overflow) const noexcept -> EdgeRange;

  private:
    void clear_node();
    auto overflow_handling(Nid self_id, Vid other_id, OverflowPool& pool) -> bool;

    // Layout (packed, 32 bytes):
    //   type            : 16 bits
    //   next_pin_id     : 42 bits
    //   ledge0          : 42 bits
    //   ledge1          : 42 bits
    //   use_overflow    :  1 bit
    //   alive           :  1 bit   (tombstone = 0)
    //   sedges_extra    : 48 bits  (3 extra 16-bit slots, exact fit)
    // -> 192 bits = 24 bytes; sedges_ begins at byte 24 (8-byte aligned).
    Type     type         : 16;
    Pid      next_pin_id  : Nid_bits;
    Nid      ledge0       : Nid_bits;
    Nid      ledge1       : Nid_bits;
    uint8_t  use_overflow : 1;
    uint8_t  alive        : 1;
    uint64_t sedges_extra : 48;
    union {
      int64_t  sedges;        // low-4 slots of 16 bits (fills 64 bits exactly)
      uint32_t overflow_idx;  // index into Graph::overflow_sets_ (when use_overflow == 1)
    } sedges_;                // 8 bytes
  };

  static_assert(sizeof(NodeEntry) == 32, "NodeEntry size mismatch");

public:
  Graph();
  // Graphs are owned via handles; copying or moving would break library identity and traversal caches.
  Graph(const Graph&)            = delete;
  Graph& operator=(const Graph&) = delete;
  Graph(Graph&&)                 = delete;
  Graph& operator=(Graph&&)      = delete;

  [[nodiscard]] Node                     create_node();
  void                                   clear();
  [[nodiscard]] Gid                      get_gid() const noexcept { return self_gid_; }
  [[nodiscard]] std::string_view         get_name() const noexcept { return name_; }
  [[nodiscard]] std::shared_ptr<GraphIO> get_io() const { return graphio_owner_.lock(); }

  // Per-graph source-provenance table (hhds-srcloc). Single-writer like the
  // body itself; resolution chains to the owning library's base table. The
  // library-level union is built at GraphLibrary::save()/load_merge() time, at
  // which point this delta is folded in and cleared.
  [[nodiscard]] Source_locator&       source_locator() noexcept { return srcloc_; }
  [[nodiscard]] const Source_locator& source_locator() const noexcept { return srcloc_; }

  // Publish a writable graph (create_graph or find_graph_rw) ahead of the
  // writable handle going out of scope. Transitions the owning library slot
  // from Writing -> Public so that find_graph(name) starts returning a
  // read-only handle. After commit, mutations through the writable handle
  // iassert (best-effort: only flagged via is_frozen()). No-op if the graph
  // has no library slot or is already committed/aborted.
  void commit();

  // Mark the writable handle as aborted. The slot reverts to Empty when the
  // last writable handle is released (partially built graph is discarded).
  // Useful in error/unwind paths. No-op for find_graph_rw or detached
  // graphs.
  void abort();

  [[nodiscard]] bool      is_frozen() const noexcept { return frozen_; }
  [[nodiscard]] Pin_class get_input_pin(std::string_view name) const;
  [[nodiscard]] Pin_class get_output_pin(std::string_view name) const;

  // Reverse lookup from an opaque index key to a Node_class / Pin_class,
  // mirroring Tree::get_node(index). Class_index is scoped to this graph
  // body; Hier_index carries hier_pos for per-instance attribute access but
  // omits the traversal's expansion tree. For cross-graph Flat_index
  // lookups, see GraphLibrary::get_node / GraphLibrary::get_pin.
  [[nodiscard]] Node_class get_node(Class_index idx) const {
    if (!is_node_valid(idx.value)) {
      return Node_class();
    }
    return Node_class(const_cast<Graph*>(this), idx.value);
  }
  [[nodiscard]] Node_class get_node(Hier_index idx) const {
    if (!is_node_valid(idx.value)) {
      return Node_class();
    }
    // root_gid unknown from a bare Hier_index — reconstructing a Node for
    // a key outside an active traversal yields a Node valid for identity
    // and attr lookups but with get_root_gid() == invalid.
    return Node_class(const_cast<Graph*>(this), static_cast<Gid>(Gid_invalid), idx.hier_pos, idx.value);
  }
  [[nodiscard]] Pin_class get_pin(Class_index idx) const {
    if (!is_pin_valid(idx.value)) {
      return Pin_class();
    }
    return make_pin_class(idx.value);
  }
  [[nodiscard]] Pin_class get_pin(Hier_index idx) const {
    if (!is_pin_valid(idx.value)) {
      return Pin_class();
    }
    Pin_class pin = make_pin_class(idx.value);
    pin.context_  = Handle_context::Hier;
    pin.hier_pos_ = idx.hier_pos;
    return pin;
  }

  // Built-in nodes reserved by graph storage initialization. These three
  // singletons are never emitted by class/flat/hier traversals — reach them
  // via the accessors below. INPUT/OUTPUT host the declared IO pins; CONST
  // hosts driver pins for constant values (any pin whose master node is
  // CONST_NODE is a constant, no further check required).
  static constexpr Nid INPUT_NODE  = (static_cast<Nid>(1) << 2);
  static constexpr Nid OUTPUT_NODE = (static_cast<Nid>(2) << 2);
  static constexpr Nid CONST_NODE  = (static_cast<Nid>(3) << 2);

  [[nodiscard]] Node_class get_input_node() const noexcept { return Node_class(const_cast<Graph*>(this), INPUT_NODE); }
  [[nodiscard]] Node_class get_output_node() const noexcept { return Node_class(const_cast<Graph*>(this), OUTPUT_NODE); }
  [[nodiscard]] Node_class get_constant_node() const noexcept { return Node_class(const_cast<Graph*>(this), CONST_NODE); }
  // Fresh driver pin on CONST_NODE. The caller attaches whatever value
  // representation it wants via the standard pin attr() API; the iterator
  // skips CONST_NODE itself, so this pin is only seen as a driver on its
  // sink's inp_edges().
  [[nodiscard]] Pin_class  create_constant() { return get_constant_node().create_driver_pin(); }

  [[nodiscard]] FastClassRange     fast_class() const noexcept;
  // Topological traversals. loop_break nodes (flops/registers — Type bit 0
  // set; forward sources / backward sinks) break the combinational cycle
  // regardless of these flags. The flags only control *when* the loop_break
  // node itself is emitted:
  //   loop_break_first (default true)  — emit it up front (forward: as a
  //                                       source; backward: as a sink).
  //   loop_break_last  (default false) — also/instead emit it after every
  //                                       combinational node.
  // The four combinations cover: see-first (default), see-last, see-both
  // (T,T — emitted twice), and never (F,F — skip flops entirely). The default
  // (true,false) preserves the historical "loop_break visited first" order.
  [[nodiscard]] ForwardClassRange  forward_class(bool loop_break_first = true, bool loop_break_last = false) const noexcept;
  [[nodiscard]] FastFlatRange      fast_flat() const noexcept;
  [[nodiscard]] ForwardFlatRange   forward_flat(bool loop_break_first = true, bool loop_break_last = false) const noexcept;
  [[nodiscard]] FastHierRange      fast_hier() const noexcept;
  // `opaque` (optional): subnode Gids the hierarchical walk must NOT descend into
  // — they are yielded as leaf Sub nodes even though their body lives in the
  // library (a caller that wants to treat a proven/blackboxed instance as opaque,
  // e.g. pass/lec's --collapse). nullptr keeps the default "descend into every
  // resolvable subnode". The pointed-to set must outlive the returned range.
  [[nodiscard]] ForwardHierRange   forward_hier(bool loop_break_first = true, bool loop_break_last = false,
                                                const ankerl::unordered_dense::set<Gid>* opaque = nullptr) const noexcept;
  [[nodiscard]] BackwardClassRange backward_class(bool loop_break_first = true, bool loop_break_last = false) const noexcept;
  [[nodiscard]] BackwardFlatRange  backward_flat(bool loop_break_first = true, bool loop_break_last = false) const noexcept;
  [[nodiscard]] BackwardHierRange  backward_hier(bool loop_break_first = true, bool loop_break_last = false) const noexcept;
  // Hierarchy-only traversal: yields one Hier_instance per subnode in the
  // structure tree, recursing into each instance's target graph (cycle-
  // guarded by active_graphs). Walks tree_ alone — it never iterates
  // node_table, so cost is proportional to the hierarchy size (≪ number
  // of graph nodes). Use this for instance counts, module-tree walks, and
  // path resolution rather than fast_hier (which visits every graph node).
  [[nodiscard]] HierRange          hier_range() const noexcept;
  void                             display_graph() const;
  void                             display_next_pin_of_node() const;

  void                      print(std::ostream& os) const;
  [[nodiscard]] std::string print() const;

private:
  void                       attr_note_modified() noexcept override { dirty_ = true; }
  [[nodiscard]] OverflowPool get_overflow_pool() { return {overflow_sets(), overflow_free_}; }
  // Overflow-set CONTENTS accessor: materializes the deferred overflow read on
  // first use, so every edge path (get_edges / EdgeRange / has_edges) sees full
  // adjacency. The raw member is overflow_storage_ (load/save/clear only).
  [[nodiscard]] OverflowVec&       overflow_sets() {
    if (overflow_deferred_) {
      ensure_overflow_loaded();
    }
    return overflow_storage_;
  }
  [[nodiscard]] const OverflowVec& overflow_sets() const {
    if (overflow_deferred_) {
      ensure_overflow_loaded();
    }
    return overflow_storage_;
  }
  void ensure_overflow_loaded() const;  // reads the deferred overflow.bin / overflow_<i>.bin
  void                       assert_accessible() const noexcept { assert(!deleted_ && "graph is no longer valid"); }
  void                       assert_node_exists(const Node_class& node) const noexcept;
  void                       assert_pin_exists(const Pin_class& pin) const noexcept;
  [[nodiscard]] bool         is_node_valid(Nid nid) const noexcept;
  [[nodiscard]] bool         is_pin_valid(Pid pid) const noexcept;
  [[nodiscard]] NodeEntry*   ref_node(Nid id) const {
    assert_accessible();
    const Nid actual_id = id >> 2;
    assert(actual_id < node_table.size());
    return const_cast<NodeEntry*>(&node_table[actual_id]);
  }
  [[nodiscard]] PinEntry* ref_pin(Pid id) const {
    assert_accessible();
    const Pid actual_id = id >> 2;
    assert(actual_id < pin_table.size());
    return const_cast<PinEntry*>(&pin_table[actual_id]);
  }
  void              invalidate_from_library() noexcept;
  void              release_storage() noexcept;
  void              clear_graph();
  // Binary persistence — saves/loads body data (node_table, pin_table, overflow sets).
  // dir_path is the graph-specific directory (e.g., "db/graph_1/").
  void              save_body(const std::string& dir_path) const;
  void              load_body(const std::string& dir_path);
  void              delete_node(Nid nid);
  [[nodiscard]] Pid materialize_declared_io_pin(std::string_view name, Port_id port_id, Nid owner_nid,
                                                ankerl::unordered_dense::map<std::string, Pid, Name_hash, Name_eq>& pins_by_name);
  void erase_declared_io_pin(std::string_view name, ankerl::unordered_dense::map<std::string, Pid, Name_hash, Name_eq>& pins_by_name);
  void delete_pin(Pid pin_pid);
  [[nodiscard]] Pin_class        create_pin(Node_class node, Port_id port_id);
  [[nodiscard]] Pid              create_pin(Nid nid, Port_id port_id);
  [[nodiscard]] Pin_class        find_pin(Node_class node, Port_id port_id, bool driver) const;
  [[nodiscard]] Pin_class        find_or_create_pin(Node_class node, Port_id port_id);
  [[nodiscard]] Port_id          resolve_driver_port(Node_class node, std::string_view name) const;
  [[nodiscard]] Port_id          resolve_sink_port(Node_class node, std::string_view name) const;
  [[nodiscard]] std::string_view pin_name(Pin_class pin) const;
  void                           set_subnode(Node_class node, Gid gid);
  void                           set_subnode(Nid nid, Gid gid);
  // Debug-only structural cycle check used by set_subnode. Returns true iff
  // making this graph contain an instance of `target_gid` would form a
  // structure-tree cycle (target_gid transitively contains self_gid_).
  // BFS over subnode_tree_pos_ entries, so cost scales with hierarchy size,
  // not graph size. Compiled out under NDEBUG via the call site.
  [[nodiscard]] bool             would_create_cycle(Gid target_gid) const noexcept;
  void                           add_edge(Pid driver_id, Pid sink_id);
  void add_edge(Pin_class driver_pin, Pin_class sink_pin) { add_edge(driver_pin.get_debug_pid(), sink_pin.get_debug_pid()); }
  void del_edge(Pin_class driver_pin, Pin_class sink_pin);
  [[nodiscard]] OutEdgeRange                       out_edges(Node_class node);
  [[nodiscard]] absl::InlinedVector<Edge_class, 4> inp_edges(Node_class node);
  [[nodiscard]] OutEdgeRange                       out_edges(Pin_class pin);
  [[nodiscard]] absl::InlinedVector<Edge_class, 4> inp_edges(Pin_class pin);
  [[nodiscard]] absl::InlinedVector<Pin_class, 4>  get_pins(Node_class node);
  [[nodiscard]] absl::InlinedVector<Pin_class, 4>  get_driver_pins(Node_class node);
  [[nodiscard]] absl::InlinedVector<Pin_class, 4>  get_sink_pins(Node_class node);
  void                                             del_edge_int(Vid driver_id, Vid sink_id);
  void                                             add_edge_int(Pid self_id, Pid other_id);
  void                                             set_next_pin(Nid nid, Pid next_pin);
  [[nodiscard]] Pin_class                          make_pin_class(Pid pin_pid) const;
  void                                             bind_library(const GraphLibrary* owner, Gid self_gid) noexcept;
  void                                             set_name(std::string_view name) { name_ = name; }
  void                                             invalidate_traversal_caches() noexcept;  // defined inline at end of header
  // Incremental patch for a single edge add/delete. delta = +1 for add, -1 for
  // delete. Bumps forward_remaining_in_cache_[sink_idx] and
  // backward_remaining_out_cache_[driver_idx] using the same filters the cache
  // builder applies. Pass-2 caches are left intact — stale entries are already
  // filtered by is_emitted() during replay. Falls back to full invalidation on
  // unexpected underflow.
  void               patch_traversal_caches_for_edge(Vid driver_id, Vid sink_id, int32_t delta) noexcept;
  // Build (or refresh) the Pass-2 deferred list and the initial in-edge counts
  // used by the forward_class streaming iterator. The full emission ordering
  // is never materialized — only these two small caches persist.
  void               ensure_forward_caches() const;
  // Exposed to the Forward iterator classes (which are friends).
  [[nodiscard]] bool forward_is_source(size_t idx) const noexcept;

  void               ensure_backward_caches() const;
  // Exposed to the Backward iterator classes (which are friends).
  [[nodiscard]] bool backward_is_sink(size_t idx) const noexcept;

  // --- Cross-boundary (hierarchical) edge resolution -----------------------
  // In a HIER traversal, inp_edges()/out_edges() must not stop at a sub-module's
  // GraphIO boundary pin: the reported driver/sink hops through the wrapping
  // instance(s) — up to the caller and/or down into a callee — until it reaches
  // a real driver/sink leaf. Only the root (starting) graph's own IO pins
  // surface. These helpers implement that walk.
  struct HierInst {
    Graph*   parent;         // graph that instantiates inst_nid
    Nid      inst_nid;       // subnode instance node within parent
    Tree_pos inst_tree_pos;  // inst_nid's structure-tree position in parent (== a child's hier_pos_)
  };
  // A resolved leaf endpoint: a real pin in `graph`, with its own root..parent
  // instance chain (for stamping the result handle's hier_path_).
  struct HierLeaf {
    Graph*                                  graph;
    Pid                                     pid;
    Tree_pos                                hier_pos;
    std::shared_ptr<const std::vector<Nid>> path;
  };
  // Expand a stored instance-nid chain (Node/Pin::get_hier_path) into HierInst
  // entries by walking down from root. Returns the body graph at the chain end,
  // or nullptr if the chain is inconsistent with the library.
  [[nodiscard]] static Graph* hier_path_to_insts(Graph* root, const std::vector<Nid>& chain, std::vector<HierInst>& path);
  // Fallback DFS reconstruction for handles that carry no chain (e.g. hand-built).
  // Ambiguous for reused sub-graphs — prefer the stored chain.
  [[nodiscard]] static bool   reconstruct_hier_path(Graph* root, Gid body_gid, Tree_pos body_hier_pos, std::vector<HierInst>& path);
  // Follow a local driver (inp) / sink (out) pin across module boundaries until
  // real leaves are reached, appending each to `out`. `path` is the instance
  // chain from root to `g` (passed by value: pushed/popped per crossing).
  static void resolve_hier_driver(Graph* g, std::vector<HierInst> path, Pid driver_pid, std::vector<HierLeaf>& out, int depth);
  static void resolve_hier_sink(Graph* g, std::vector<HierInst> path, Pid sink_pid, std::vector<HierLeaf>& out, int depth);
  // Non-asserting pin lookup by port_id (0 == node-as-pin). Returns 0 if absent.
  [[nodiscard]] Pid     find_pin_or_zero(Nid nid, Port_id port_id, bool driver) const;
  // Master node nid (role bits cleared) and port_id of an arbitrary pin pid.
  [[nodiscard]] Nid     master_nid_of_pid(Pid pid) const;
  [[nodiscard]] Port_id port_of_pid(Pid pid) const;
  static constexpr int  kHierResolveMaxDepth = 4096;  // runaway guard for malformed nets

  // Local (single-graph) edge readers — the historical behavior, used directly
  // for Class/Flat handles and as the per-graph primitive by the hier readers.
  [[nodiscard]] absl::InlinedVector<Edge_class, 4> inp_edges_local(Node_class node);
  [[nodiscard]] absl::InlinedVector<Edge_class, 4> out_edges_local(Node_class node);
  // Hier readers: resolve each far endpoint across module boundaries.
  [[nodiscard]] absl::InlinedVector<Edge_class, 4> inp_edges_hier(Node_class node);
  [[nodiscard]] absl::InlinedVector<Edge_class, 4> out_edges_hier(Node_class node);
  // Starting instance chain (root..node's body) for the hier resolvers.
  [[nodiscard]] bool                               hier_base_path(Node_class node, std::vector<HierInst>& base_path);
  // get_hier_name building blocks. hier_local_name: a node's `name` attr, else
  // the instantiated module name, else "n<id>". build_hier_name: join the
  // instance chain's local names with the body node's local name.
  [[nodiscard]] static std::string                 hier_local_name(Graph* g, Nid nid);
  [[nodiscard]] static std::string build_hier_name(Graph* graph, Gid root_gid, const std::shared_ptr<const std::vector<Nid>>& path,
                                                   Nid raw_nid);

  std::vector<NodeEntry>                                         node_table;
  std::vector<PinEntry>                                          pin_table;
  // Edge-adjacency overflow sets. LAZY: load_body sizes this vector but defers
  // reading the set CONTENTS (the overflow.bin / overflow_<i>.bin files) until an
  // edge is actually traversed — a pure structure walk (fast_class + subnode +
  // node count, e.g. `lhd tools tree`) never touches edges, so it never pays to
  // read them. Access the CONTENTS only via overflow_sets() (which ensures the
  // deferred read has happened); overflow_storage_ is the raw backing store used
  // by load/save/clear, which must NOT trigger a re-read.
  OverflowVec                                                    overflow_storage_;
  std::vector<uint32_t>                                          overflow_free_;
  mutable bool                                                   overflow_deferred_ = false;
  std::string                                                    overflow_src_dir_;
  // Persistent hierarchy: one Tree per Graph, populated by set_subnode and
  // torn down in clear()/load_body rebuild. The tree's children correspond
  // 1:1 with live subnode NodeEntries. `subnode_tree_pos_` maps a subnode
  // Nid back to its Tree_pos so del_node / debug cycle checks can find it.
  std::shared_ptr<Tree>                                          tree_;
  ankerl::unordered_dense::map<Nid, Tree_pos>                    subnode_tree_pos_;
  // Reverse map so hier_range can resolve a Tree_pos back to its owning Nid
  // in O(1) during tree-pre-order iteration. Kept in lockstep with
  // subnode_tree_pos_ — every set_subnode / load_body insertion updates
  // both, and all three clear sites (release_storage, clear_graph, clear)
  // clear both.
  ankerl::unordered_dense::map<Tree_pos, Nid>                    tree_pos_to_nid_;
  // Forward-traversal caches, shared across forward_class / forward_flat /
  // forward_hier for this graph body. Only the Pass-2 deferral list and the
  // initial in-edge counts are kept — the Pass-1 emission order is replayed
  // from storage order and the Tail from alive-but-unemitted survivors. No
  // Node_class objects are cached, so memory is O(Pass2) + O(N × 4 bytes)
  // rather than O(N × sizeof(Node_class)).
  mutable std::vector<Nid>                                       forward_pass2_cache_;
  mutable std::vector<uint32_t>                                  forward_remaining_in_cache_;
  mutable bool                                                   forward_caches_valid_ = false;
  mutable std::vector<Nid>                                       backward_pass2_cache_;
  mutable std::vector<uint32_t>                                  backward_remaining_out_cache_;
  mutable bool                                                   backward_caches_valid_ = false;
  ankerl::unordered_dense::map<std::string, Pid, Name_hash, Name_eq> input_pins_;
  ankerl::unordered_dense::map<std::string, Pid, Name_hash, Name_eq> output_pins_;
  const GraphLibrary*                                            owner_lib_ = nullptr;
  std::weak_ptr<GraphIO>                                         graphio_owner_;
  Gid                                                            self_gid_ = Gid_invalid;
  bool                                                           deleted_  = false;
  mutable bool                                                   dirty_    = true;
  // Set by commit(). When true, the writer asked to publish the graph;
  // single-threaded — only the writer has a writable handle.
  bool                                                           frozen_   = false;
  std::string                                                    name_;
  // Source-provenance delta (hhds-srcloc). Provenance is body content, so it
  // is cleared with the attr stores (clear()/clear_graph()/
  // invalidate_from_library()); the base pointer to the owning library's
  // shared source map (*srcmap_sp_, set in bind_library) survives everything but detach.
  Source_locator                                                 srcloc_;

  friend class Node_class;
  friend class Pin_class;
  friend class Edge_class;
  friend class GraphIO;
  friend class GraphLibrary;
  friend class FastClassIterator;
  friend class FastClassRange;
  friend class FastFlatIterator;
  friend class FastFlatRange;
  friend class FastHierIterator;
  friend class FastHierRange;
  friend class ForwardClassIterator;
  friend class ForwardClassRange;
  friend class ForwardFlatIterator;
  friend class ForwardFlatRange;
  friend class ForwardHierIterator;
  friend class ForwardHierRange;
  friend class BackwardClassIterator;
  friend class BackwardClassRange;
  friend class BackwardFlatIterator;
  friend class BackwardFlatRange;
  friend class BackwardHierIterator;
  friend class BackwardHierRange;
  friend class HierIterator;
  friend class HierRange;
  friend class Hier_instance;
  friend class OutEdgeIterator;
  friend class OutEdgeRange;
};

// Lazy, auto-scaling view over the OUT edges of a pin or node. Yields Edge_class
// on demand instead of materializing a vector: a pin with a huge fanout
// (clock/reset/enable -> 100K+ sinks) is walked in place over its overflow set
// with NO copy and supports early `break`, while the common small case decodes a
// handful of inline edges into a tiny stack buffer. For a node in Hier context
// the range is backed by the cross-boundary-resolved snapshot behind the same API.
//
// LIFETIME / MUTATION: this is a VIEW over live edge storage. Any graph mutation
// (add_edge/del_edge/del_pin/del_node) invalidates an in-flight iterator. To
// iterate-and-mutate, snapshot first:
//     auto r = pin.out_edges();
//     absl::InlinedVector<Edge_class, 4> snap(r.begin(), r.end());
//     for (auto& e : snap) e.del_edge();
// size() is O(n) and front()/empty() each re-walk from begin(); do not call
// size() per-pin in hot loops.
class OutEdgeIterator {
public:
  using iterator_category = std::input_iterator_tag;
  using value_type        = Edge_class;
  using reference         = Edge_class;
  using pointer           = void;
  using difference_type   = std::ptrdiff_t;

  OutEdgeIterator() = default;  // End sentinel (phase_ == End)

  [[nodiscard]] Edge_class operator*() const;
  OutEdgeIterator&         operator++();
  OutEdgeIterator          operator++(int) {
    OutEdgeIterator tmp = *this;
    ++*this;
    return tmp;
  }
  [[nodiscard]] bool operator==(const OutEdgeIterator& o) const noexcept { return phase_ == Phase::End && o.phase_ == Phase::End; }
  [[nodiscard]] bool operator!=(const OutEdgeIterator& o) const noexcept { return !(*this == o); }

private:
  enum class Phase : uint8_t { NodeAsPin, PinList, Materialized, End };

  void                     start();              // seed -> first outgoing edge (or End)
  void                     skip_and_position();  // advance to next outgoing edge across entries
  bool                     open_next_entry();    // node-as-pin -> pin list -> next pins; false when done
  bool                     load_next_pin();
  void                     bind_node_as_pin();
  void                     bind_pin();
  void                     set_driver(Pid driver_pid);
  [[nodiscard]] Edge_class build_edge(Vid vid) const;

  [[nodiscard]] bool entry_at_end() const noexcept { return is_overflow_ ? (ovf_it_ == ovf_end_) : (idx_ >= n_); }
  [[nodiscard]] Vid  entry_cur_vid() const noexcept { return is_overflow_ ? *ovf_it_ : buf_[idx_]; }
  void               entry_step() noexcept {
    if (is_overflow_) {
      ++ovf_it_;
    } else {
      ++idx_;
    }
  }

  Graph* graph_        = nullptr;
  Phase  phase_        = Phase::End;
  bool   is_node_src_  = false;  // true: node source (walk node-as-pin then pin list)
  bool   src_is_port0_ = false;  // pin source whose single entry is the node-as-pin(0)
  bool   is_overflow_  = false;  // current entry uses the overflow set (the 100K case)

  // Source identity + context template (copied from the range; stamped onto
  // every emitted driver/sink to reproduce the eager out_edges behavior exactly).
  Nid                                     self_nid_ = 0;  // node base (& ~2)
  Handle_context                          context_  = Handle_context::Class;
  Gid                                     root_gid_ = Gid_invalid;
  Tree_pos                                hier_pos_ = INVALID;
  std::shared_ptr<const std::vector<Nid>> hier_path_;

  // Current entry being walked.
  const Graph::NodeEntry* node_entry_     = nullptr;
  const Graph::PinEntry*  pin_entry_      = nullptr;
  Pid                     cur_pin_lookup_ = 0;  // canonical ((pid&~2)|1) of pin_entry_
  Pid                     next_pin_id_    = 0;  // next pin in the node's linked list

  // Inline cursor: the entry's small decoded edge set, copied out of its
  // EdgeRange. Sized for the larger entry kind (NodeEntry kInlineMax 9 >=
  // PinEntry 6); bind_node_as_pin/bind_pin static_assert this stays valid.
  static constexpr size_t  kBufCap = 9;
  std::array<Vid, kBufCap> buf_{};
  uint8_t                  n_   = 0;
  uint8_t                  idx_ = 0;

  // Overflow cursor: borrowed live set, iterated in place (no copy).
  const OverflowSet*          ovf_ = nullptr;
  OverflowSet::const_iterator ovf_it_{};
  OverflowSet::const_iterator ovf_end_{};

  // Hier-materialized backing (node in Hier context).
  std::shared_ptr<absl::InlinedVector<Edge_class, 4>> mat_;
  size_t                                              mat_idx_ = 0;

  // Driver pin for the active entry (built once per entry, already stamped).
  Pin_class cur_driver_{};

  friend class OutEdgeRange;
  friend class Graph;
};

// Movable handle returned by out_edges(); begin() seeds a fresh iterator.
class OutEdgeRange {
public:
  using iterator = OutEdgeIterator;

  [[nodiscard]] OutEdgeIterator begin() const;
  [[nodiscard]] OutEdgeIterator end() const noexcept { return OutEdgeIterator{}; }
  [[nodiscard]] bool            empty() const { return begin() == end(); }
  [[nodiscard]] size_t          size() const;   // O(n): walks the range
  [[nodiscard]] Edge_class      front() const;  // precondition: !empty()

private:
  Graph*                                              graph_        = nullptr;
  bool                                                is_node_src_  = false;
  bool                                                src_is_port0_ = false;
  Nid                                                 self_nid_     = 0;  // node base, or pin-port0 node base
  Pid                                                 src_pid_      = 0;  // non-port0 pin source canonical pid (else 0)
  Handle_context                                      context_      = Handle_context::Class;
  Gid                                                 root_gid_     = Gid_invalid;
  Tree_pos                                            hier_pos_     = INVALID;
  std::shared_ptr<const std::vector<Nid>>             hier_path_;
  std::shared_ptr<absl::InlinedVector<Edge_class, 4>> mat_;  // non-null => hier-materialized

  friend class Graph;
  friend class OutEdgeIterator;
};

// Lazy, single-pass iterator over a graph's live nodes (node_table scan,
// tombstones skipped). No materialization; no per-node shared_ptr overhead.
class FastClassIterator {
public:
  using iterator_category = std::input_iterator_tag;
  using value_type        = Node_class;
  using difference_type   = std::ptrdiff_t;
  using pointer           = void;
  using reference         = Node_class;

  FastClassIterator() noexcept = default;

  [[nodiscard]] Node_class operator*() const noexcept;
  FastClassIterator&       operator++() noexcept;
  FastClassIterator        operator++(int) noexcept {
    FastClassIterator tmp = *this;
    ++*this;
    return tmp;
  }
  [[nodiscard]] bool operator==(const FastClassIterator& o) const noexcept { return graph_ == o.graph_ && idx_ == o.idx_; }
  [[nodiscard]] bool operator!=(const FastClassIterator& o) const noexcept { return !(*this == o); }

private:
  FastClassIterator(Graph* graph, size_t idx, size_t end) noexcept;
  void skip_tombstones() noexcept;

  Graph* graph_ = nullptr;
  size_t idx_   = 0;
  size_t end_   = 0;

  friend class FastClassRange;
};

class FastClassRange {
public:
  explicit FastClassRange(Graph* graph) noexcept : graph_(graph) {}
  [[nodiscard]] FastClassIterator begin() const noexcept;
  [[nodiscard]] FastClassIterator end() const noexcept;

private:
  Graph* graph_;
};

// Instance-level flat traversal (sub-graphs visited per instance).
class FastFlatIterator {
public:
  using iterator_category = std::input_iterator_tag;
  using value_type        = Node_class;
  using difference_type   = std::ptrdiff_t;
  using pointer           = void;
  using reference         = Node_class;

  FastFlatIterator() noexcept                          = default;
  FastFlatIterator(const FastFlatIterator&)            = default;
  FastFlatIterator(FastFlatIterator&&)                 = default;
  FastFlatIterator& operator=(const FastFlatIterator&) = default;
  FastFlatIterator& operator=(FastFlatIterator&&)      = default;
  ~FastFlatIterator()                                  = default;

  [[nodiscard]] Node_class operator*() const;
  FastFlatIterator&        operator++();
  FastFlatIterator         operator++(int) {
    FastFlatIterator tmp = *this;
    ++*this;
    return tmp;
  }
  [[nodiscard]] bool operator==(const FastFlatIterator& o) const noexcept { return stack_.empty() && o.stack_.empty(); }
  [[nodiscard]] bool operator!=(const FastFlatIterator& o) const noexcept { return !(*this == o); }

private:
  struct Frame {
    Graph* graph;
    size_t node_idx;
    size_t end;
  };

  explicit FastFlatIterator(Graph* root_graph);
  void advance();

  Gid                               top_graph_ = Gid_invalid;
  std::vector<Frame>                stack_;
  ankerl::unordered_dense::set<Gid> active_graphs_;

  friend class FastFlatRange;
};

class FastFlatRange {
public:
  explicit FastFlatRange(Graph* graph) noexcept : graph_(graph) {}
  [[nodiscard]] FastFlatIterator begin() const;
  [[nodiscard]] FastFlatIterator end() const noexcept { return FastFlatIterator{}; }

private:
  Graph* graph_;
};

// Hierarchical traversal with per-instance hier_pos (unique token that maps
// through `hier_gids_` back to the owning Gid for downstream Node APIs).
class FastHierIterator {
public:
  using iterator_category = std::input_iterator_tag;
  using value_type        = Node_class;
  using difference_type   = std::ptrdiff_t;
  using pointer           = void;
  using reference         = Node_class;

  FastHierIterator() noexcept                          = default;
  FastHierIterator(const FastHierIterator&)            = delete;
  FastHierIterator(FastHierIterator&&)                 = default;
  FastHierIterator& operator=(const FastHierIterator&) = delete;
  FastHierIterator& operator=(FastHierIterator&&)      = default;
  ~FastHierIterator()                                  = default;

  [[nodiscard]] Node_class operator*() const;
  FastHierIterator&        operator++();
  [[nodiscard]] bool       operator==(const FastHierIterator& o) const noexcept { return stack_.empty() && o.stack_.empty(); }
  [[nodiscard]] bool       operator!=(const FastHierIterator& o) const noexcept { return !(*this == o); }

private:
  struct Frame {
    Graph*                                  graph;
    size_t                                  node_idx;
    size_t                                  end;
    Tree_pos                                hier_pos;  // position in the parent graph's structure tree
    std::shared_ptr<const std::vector<Nid>> path;      // root..this-frame instance chain
  };

  explicit FastHierIterator(Graph* root_graph);
  void advance();

  Gid                               root_gid_ = Gid_invalid;
  std::vector<Frame>                stack_;
  ankerl::unordered_dense::set<Gid> active_graphs_;

  friend class FastHierRange;
};

class FastHierRange {
public:
  explicit FastHierRange(Graph* graph) noexcept : graph_(graph) {}
  [[nodiscard]] FastHierIterator begin() const;
  [[nodiscard]] FastHierIterator end() const noexcept { return FastHierIterator{}; }

private:
  Graph* graph_;
};

// Forward topological iterator for a single graph body. Emits sources first,
// then storage-order combinational nodes (Pass 1), then deferred back-edge
// targets (Pass 2 replayed from Graph::forward_pass2_cache_), then any cycle
// survivors (Tail). No Node_class objects are cached — per-iteration scratch
// (a working copy of in-edge counts and an emitted bitset) is the only
// per-walk allocation. Move-only to keep that scratch unique.
class ForwardClassIterator {
public:
  using iterator_category = std::input_iterator_tag;
  using value_type        = Node_class;
  using difference_type   = std::ptrdiff_t;
  using pointer           = void;
  using reference         = Node_class;

  ForwardClassIterator() noexcept                              = default;
  ForwardClassIterator(const ForwardClassIterator&)            = delete;
  ForwardClassIterator& operator=(const ForwardClassIterator&) = delete;
  ForwardClassIterator(ForwardClassIterator&&) noexcept;
  ForwardClassIterator& operator=(ForwardClassIterator&&) noexcept;
  ~ForwardClassIterator() = default;

  [[nodiscard]] Node_class operator*() const;
  ForwardClassIterator&    operator++();
  [[nodiscard]] bool       operator==(const ForwardClassIterator& o) const noexcept {
    // End sentinel: both iterators at Phase::End compare equal regardless of
    // graph_, so a default-constructed end iterator terminates range-for.
    if (phase_ == Phase::End && o.phase_ == Phase::End) {
      return true;
    }
    return graph_ == o.graph_ && phase_ == o.phase_ && current_idx_ == o.current_idx_;
  }
  [[nodiscard]] bool operator!=(const ForwardClassIterator& o) const noexcept { return !(*this == o); }

private:
  // Phase order: Pass1 (sources + storage-order combinational), Pass2 (cached
  // back-edge replay), Tail (cycle survivors), LoopLast (loop_break replay,
  // only entered when loop_break_last_), End.
  enum class Phase : uint8_t { Pass1, Pass2, Tail, LoopLast, End };

  explicit ForwardClassIterator(Graph* graph, bool loop_break_first = true, bool loop_break_last = false);
  void               advance();
  void               propagate(size_t driver_idx, size_t cursor);
  [[nodiscard]] bool is_source(size_t idx) const noexcept;
  [[nodiscard]] bool is_emitted(size_t idx) const noexcept;
  void               mark_emitted(size_t idx) noexcept;
  // True while the current emission is a loop_break_last replay. Used by the
  // flat/hier wrappers to avoid descending into a loop_break subnode twice
  // when it is emitted both first and last.
  [[nodiscard]] bool current_is_loop_break_replay() const noexcept { return phase_ == Phase::LoopLast; }

  Graph* graph_            = nullptr;
  Phase  phase_            = Phase::End;
  size_t idx_              = 0;
  size_t pass2_head_       = 0;
  size_t node_count_       = 0;
  size_t current_idx_      = 0;
  bool   loop_break_first_ = true;
  bool   loop_break_last_  = false;

  std::vector<uint32_t> working_remaining_in_;
  std::vector<uint64_t> emitted_bits_;

  friend class ForwardClassRange;
  friend class ForwardFlatIterator;
  friend class ForwardHierIterator;
};

class ForwardClassRange {
public:
  explicit ForwardClassRange(Graph* graph, bool loop_break_first = true, bool loop_break_last = false) noexcept
      : graph_(graph), loop_break_first_(loop_break_first), loop_break_last_(loop_break_last) {}
  [[nodiscard]] ForwardClassIterator begin() const;
  [[nodiscard]] ForwardClassIterator end() const noexcept { return ForwardClassIterator{}; }

  // Backward-compat helpers for callers that previously used std::span.
  // size()/front() each walk the iteration once (O(N)) — avoid in hot paths.
  [[nodiscard]] size_t     size() const;
  [[nodiscard]] Node_class front() const;
  [[nodiscard]] bool       empty() const;

private:
  Graph* graph_;
  bool   loop_break_first_ = true;
  bool   loop_break_last_  = false;
};

// Forward flat traversal: local graph's forward order, with subgraph bodies
// visited once in the order their owning subnodes emit (global active_graphs
// dedup — a subgraph seen via multiple instances expands only the first time).
class ForwardFlatIterator {
public:
  using iterator_category = std::input_iterator_tag;
  using value_type        = Node_class;
  using difference_type   = std::ptrdiff_t;
  using pointer           = void;
  using reference         = Node_class;

  ForwardFlatIterator() noexcept                                 = default;
  ForwardFlatIterator(const ForwardFlatIterator&)                = delete;
  ForwardFlatIterator& operator=(const ForwardFlatIterator&)     = delete;
  ForwardFlatIterator(ForwardFlatIterator&&) noexcept            = default;
  ForwardFlatIterator& operator=(ForwardFlatIterator&&) noexcept = default;
  ~ForwardFlatIterator()                                         = default;

  [[nodiscard]] Node_class operator*() const;
  ForwardFlatIterator&     operator++();
  [[nodiscard]] bool       operator==(const ForwardFlatIterator& o) const noexcept { return stack_.empty() && o.stack_.empty(); }
  [[nodiscard]] bool       operator!=(const ForwardFlatIterator& o) const noexcept { return !(*this == o); }

private:
  struct Frame {
    Graph*               graph;
    ForwardClassIterator it;
  };

  explicit ForwardFlatIterator(Graph* root_graph, bool loop_break_first = true, bool loop_break_last = false);
  void advance();

  Gid                               top_graph_        = Gid_invalid;
  bool                              loop_break_first_ = true;
  bool                              loop_break_last_  = false;
  std::vector<Frame>                stack_;
  ankerl::unordered_dense::set<Gid> active_graphs_;

  friend class ForwardFlatRange;
};

class ForwardFlatRange {
public:
  explicit ForwardFlatRange(Graph* graph, bool loop_break_first = true, bool loop_break_last = false) noexcept
      : graph_(graph), loop_break_first_(loop_break_first), loop_break_last_(loop_break_last) {}
  [[nodiscard]] ForwardFlatIterator begin() const;
  [[nodiscard]] ForwardFlatIterator end() const noexcept { return ForwardFlatIterator{}; }

private:
  Graph* graph_;
  bool   loop_break_first_ = true;
  bool   loop_break_last_  = false;
};

// Forward hier traversal: per-instance hier_pos token, subgraph bodies visited
// once per subnode instance (active_graphs is push/pop scoped to the current
// recursion path, not the whole walk).
class ForwardHierIterator {
public:
  using iterator_category = std::input_iterator_tag;
  using value_type        = Node_class;
  using difference_type   = std::ptrdiff_t;
  using pointer           = void;
  using reference         = Node_class;

  ForwardHierIterator() noexcept                                 = default;
  ForwardHierIterator(const ForwardHierIterator&)                = delete;
  ForwardHierIterator& operator=(const ForwardHierIterator&)     = delete;
  ForwardHierIterator(ForwardHierIterator&&) noexcept            = default;
  ForwardHierIterator& operator=(ForwardHierIterator&&) noexcept = default;
  ~ForwardHierIterator()                                         = default;

  [[nodiscard]] Node_class operator*() const;
  ForwardHierIterator&     operator++();
  [[nodiscard]] bool       operator==(const ForwardHierIterator& o) const noexcept { return stack_.empty() && o.stack_.empty(); }
  [[nodiscard]] bool       operator!=(const ForwardHierIterator& o) const noexcept { return !(*this == o); }

private:
  struct Frame {
    Graph*                                  graph;
    ForwardClassIterator                    it;
    Tree_pos                                hier_pos;
    std::shared_ptr<const std::vector<Nid>> path;  // root..this-frame instance chain
  };

  explicit ForwardHierIterator(Graph* root_graph, bool loop_break_first = true, bool loop_break_last = false,
                               const ankerl::unordered_dense::set<Gid>* opaque = nullptr);
  void advance();

  Gid                                      root_gid_         = Gid_invalid;
  bool                                     loop_break_first_ = true;
  bool                                     loop_break_last_  = false;
  std::vector<Frame>                       stack_;
  ankerl::unordered_dense::set<Gid>        active_graphs_;
  const ankerl::unordered_dense::set<Gid>* opaque_ = nullptr;  // subnodes to NOT descend into

  friend class ForwardHierRange;
};

class ForwardHierRange {
public:
  explicit ForwardHierRange(Graph* graph, bool loop_break_first = true, bool loop_break_last = false,
                            const ankerl::unordered_dense::set<Gid>* opaque = nullptr) noexcept
      : graph_(graph), loop_break_first_(loop_break_first), loop_break_last_(loop_break_last), opaque_(opaque) {}
  [[nodiscard]] ForwardHierIterator begin() const;
  [[nodiscard]] ForwardHierIterator end() const noexcept { return ForwardHierIterator{}; }

private:
  Graph*                                   graph_;
  bool                                     loop_break_first_ = true;
  bool                                     loop_break_last_  = false;
  const ankerl::unordered_dense::set<Gid>* opaque_           = nullptr;
};

// ── Ambient hierarchical-walk opacity ───────────────────────────────────────
// Subnode Gids in the thread-local opacity set are treated as LEAVES by BOTH the
// hierarchical iterators (forward_hier — not descended into) AND the cross-
// boundary edge resolver (a read of such a subnode's output stops at the
// instance boundary instead of threading down to the real internal driver). The
// two must agree, or a consumer would read an un-encoded internal pin. Used by
// pass/lec --collapse to black-box a proven submodule that lives in the SAME
// library as its parent (so it cannot simply be omitted from the library). Set
// with the RAII Hier_opaque_scope; nullptr (default) = descend into everything.
const ankerl::unordered_dense::set<Gid>*& hier_opaque_ref() noexcept;
[[nodiscard]] inline bool                 hier_is_opaque(Gid g) noexcept {
  const auto* s = hier_opaque_ref();
  return s != nullptr && s->find(g) != s->end();
}

class Hier_opaque_scope {
public:
  explicit Hier_opaque_scope(const ankerl::unordered_dense::set<Gid>* s) noexcept : prev_(hier_opaque_ref()) {
    hier_opaque_ref() = s;
  }
  Hier_opaque_scope(const Hier_opaque_scope&)            = delete;
  Hier_opaque_scope& operator=(const Hier_opaque_scope&) = delete;
  ~Hier_opaque_scope() { hier_opaque_ref() = prev_; }

private:
  const ankerl::unordered_dense::set<Gid>* prev_;
};

// Backward topological iterator for a single graph body. Emits sinks first,
// then reverse storage-order combinational nodes (Pass 1), then deferred back-edge
// sources (Pass 2 replayed from Graph::backward_pass2_cache_), then any cycle
// survivors (Tail).
class BackwardClassIterator {
public:
  using iterator_category = std::input_iterator_tag;
  using value_type        = Node_class;
  using difference_type   = std::ptrdiff_t;
  using pointer           = void;
  using reference         = Node_class;

  BackwardClassIterator() noexcept                               = default;
  BackwardClassIterator(const BackwardClassIterator&)            = delete;
  BackwardClassIterator& operator=(const BackwardClassIterator&) = delete;
  BackwardClassIterator(BackwardClassIterator&&) noexcept;
  BackwardClassIterator& operator=(BackwardClassIterator&&) noexcept;
  ~BackwardClassIterator() = default;

  [[nodiscard]] Node_class operator*() const;
  BackwardClassIterator&   operator++();
  [[nodiscard]] bool       operator==(const BackwardClassIterator& o) const noexcept {
    if (phase_ == Phase::End && o.phase_ == Phase::End) {
      return true;
    }
    return graph_ == o.graph_ && phase_ == o.phase_ && current_idx_ == o.current_idx_;
  }
  [[nodiscard]] bool operator!=(const BackwardClassIterator& o) const noexcept { return !(*this == o); }

private:
  // Phase order mirrors ForwardClassIterator: Pass1 (sinks + reverse
  // storage-order combinational), Pass2 (cached replay), Tail (cycle
  // survivors), LoopLast (loop_break replay, only when loop_break_last_), End.
  enum class Phase : uint8_t { Pass1, Pass2, Tail, LoopLast, End };

  explicit BackwardClassIterator(Graph* graph, bool loop_break_first = true, bool loop_break_last = false);
  void               advance();
  void               propagate(size_t sink_idx, size_t cursor);
  [[nodiscard]] bool is_sink(size_t idx) const noexcept;
  [[nodiscard]] bool is_emitted(size_t idx) const noexcept;
  void               mark_emitted(size_t idx) noexcept;
  [[nodiscard]] bool current_is_loop_break_replay() const noexcept { return phase_ == Phase::LoopLast; }

  Graph* graph_            = nullptr;
  Phase  phase_            = Phase::End;
  size_t idx_              = 0;
  size_t pass2_head_       = 0;
  size_t node_count_       = 0;
  size_t current_idx_      = 0;
  bool   loop_break_first_ = true;
  bool   loop_break_last_  = false;

  std::vector<uint32_t> working_remaining_out_;
  std::vector<uint64_t> emitted_bits_;

  friend class BackwardClassRange;
  friend class BackwardFlatIterator;
  friend class BackwardHierIterator;
};

class BackwardClassRange {
public:
  explicit BackwardClassRange(Graph* graph, bool loop_break_first = true, bool loop_break_last = false) noexcept
      : graph_(graph), loop_break_first_(loop_break_first), loop_break_last_(loop_break_last) {}
  [[nodiscard]] BackwardClassIterator begin() const;
  [[nodiscard]] BackwardClassIterator end() const noexcept { return BackwardClassIterator{}; }

  [[nodiscard]] size_t     size() const;
  [[nodiscard]] Node_class front() const;
  [[nodiscard]] bool       empty() const;

private:
  Graph* graph_;
  bool   loop_break_first_ = true;
  bool   loop_break_last_  = false;
};

class BackwardFlatIterator {
public:
  using iterator_category = std::input_iterator_tag;
  using value_type        = Node_class;
  using difference_type   = std::ptrdiff_t;
  using pointer           = void;
  using reference         = Node_class;

  BackwardFlatIterator() noexcept                                  = default;
  BackwardFlatIterator(const BackwardFlatIterator&)                = delete;
  BackwardFlatIterator& operator=(const BackwardFlatIterator&)     = delete;
  BackwardFlatIterator(BackwardFlatIterator&&) noexcept            = default;
  BackwardFlatIterator& operator=(BackwardFlatIterator&&) noexcept = default;
  ~BackwardFlatIterator()                                          = default;

  [[nodiscard]] Node_class operator*() const;
  BackwardFlatIterator&    operator++();
  [[nodiscard]] bool       operator==(const BackwardFlatIterator& o) const noexcept { return stack_.empty() && o.stack_.empty(); }
  [[nodiscard]] bool       operator!=(const BackwardFlatIterator& o) const noexcept { return !(*this == o); }

private:
  struct Frame {
    Graph*                graph;
    BackwardClassIterator it;
  };

  explicit BackwardFlatIterator(Graph* root_graph, bool loop_break_first = true, bool loop_break_last = false);
  void advance();

  Gid                               top_graph_        = Gid_invalid;
  bool                              loop_break_first_ = true;
  bool                              loop_break_last_  = false;
  std::vector<Frame>                stack_;
  ankerl::unordered_dense::set<Gid> active_graphs_;

  friend class BackwardFlatRange;
};

class BackwardFlatRange {
public:
  explicit BackwardFlatRange(Graph* graph, bool loop_break_first = true, bool loop_break_last = false) noexcept
      : graph_(graph), loop_break_first_(loop_break_first), loop_break_last_(loop_break_last) {}
  [[nodiscard]] BackwardFlatIterator begin() const;
  [[nodiscard]] BackwardFlatIterator end() const noexcept { return BackwardFlatIterator{}; }

private:
  Graph* graph_;
  bool   loop_break_first_ = true;
  bool   loop_break_last_  = false;
};

class BackwardHierIterator {
public:
  using iterator_category = std::input_iterator_tag;
  using value_type        = Node_class;
  using difference_type   = std::ptrdiff_t;
  using pointer           = void;
  using reference         = Node_class;

  BackwardHierIterator() noexcept                                  = default;
  BackwardHierIterator(const BackwardHierIterator&)                = delete;
  BackwardHierIterator& operator=(const BackwardHierIterator&)     = delete;
  BackwardHierIterator(BackwardHierIterator&&) noexcept            = default;
  BackwardHierIterator& operator=(BackwardHierIterator&&) noexcept = default;
  ~BackwardHierIterator()                                          = default;

  [[nodiscard]] Node_class operator*() const;
  BackwardHierIterator&    operator++();
  [[nodiscard]] bool       operator==(const BackwardHierIterator& o) const noexcept { return stack_.empty() && o.stack_.empty(); }
  [[nodiscard]] bool       operator!=(const BackwardHierIterator& o) const noexcept { return !(*this == o); }

private:
  struct Frame {
    Graph*                                  graph;
    BackwardClassIterator                   it;
    Tree_pos                                hier_pos;
    std::shared_ptr<const std::vector<Nid>> path;  // root..this-frame instance chain
  };

  explicit BackwardHierIterator(Graph* root_graph, bool loop_break_first = true, bool loop_break_last = false);
  void advance();

  Gid                               root_gid_         = Gid_invalid;
  bool                              loop_break_first_ = true;
  bool                              loop_break_last_  = false;
  std::vector<Frame>                stack_;
  ankerl::unordered_dense::set<Gid> active_graphs_;

  friend class BackwardHierRange;
};

class BackwardHierRange {
public:
  explicit BackwardHierRange(Graph* graph, bool loop_break_first = true, bool loop_break_last = false) noexcept
      : graph_(graph), loop_break_first_(loop_break_first), loop_break_last_(loop_break_last) {}
  [[nodiscard]] BackwardHierIterator begin() const;
  [[nodiscard]] BackwardHierIterator end() const noexcept { return BackwardHierIterator{}; }

private:
  Graph* graph_;
  bool   loop_break_first_ = true;
  bool   loop_break_last_  = false;
};

// Structure-tree pre-order iterator. Walks tree_ (skipping ROOT) and, when
// a tree position corresponds to a live subnode, descends into the subnode's
// target graph's tree to continue recursively. active_graphs_ guards
// against structural cycles (which are disallowed but not enforced in
// release builds). Move-only: holds per-walk working state (frame stack +
// active_graphs set) that we don't want copied implicitly.
class HierIterator {
public:
  using iterator_category = std::input_iterator_tag;
  using value_type        = Hier_instance;
  using difference_type   = std::ptrdiff_t;
  using pointer           = void;
  using reference         = Hier_instance;

  HierIterator() noexcept                          = default;
  HierIterator(const HierIterator&)                = delete;
  HierIterator(HierIterator&&) noexcept            = default;
  HierIterator& operator=(const HierIterator&)     = delete;
  HierIterator& operator=(HierIterator&&) noexcept = default;
  ~HierIterator()                                  = default;

  [[nodiscard]] Hier_instance operator*() const;
  HierIterator&               operator++();
  [[nodiscard]] bool          operator==(const HierIterator& o) const noexcept { return stack_.empty() && o.stack_.empty(); }
  [[nodiscard]] bool          operator!=(const HierIterator& o) const noexcept { return !(*this == o); }

private:
  struct Frame {
    Graph*                   graph;     // the graph whose tree is being walked
    Tree::pre_order_iterator cur;       // cursor into graph->tree_->pre_order()
    Tree::pre_order_iterator end;       // matching end sentinel
    Tree_pos                 hier_pos;  // parent subnode's tree_pos within its graph, or ROOT for the top frame
  };

  explicit HierIterator(Graph* root_graph);
  // Advance cur_ until it lands on a Tree_pos that corresponds to a live
  // subnode in the current frame's graph. Pops exhausted frames (unwinding
  // active_graphs_ as we go) and keeps walking until either a yieldable
  // instance is found or the stack is empty.
  void advance_to_next_instance();

  Gid                               root_gid_ = Gid_invalid;
  std::vector<Frame>                stack_;
  ankerl::unordered_dense::set<Gid> active_graphs_;

  friend class HierRange;
};

class HierRange {
public:
  explicit HierRange(Graph* graph) noexcept : graph_(graph) {}
  [[nodiscard]] HierIterator begin() const;
  [[nodiscard]] HierIterator end() const noexcept { return HierIterator{}; }

private:
  Graph* graph_;
};

class GraphIO : public std::enable_shared_from_this<GraphIO> {
public:
  // GraphIO owns declared IO-pin metadata. Concrete Pid values only exist once a Graph body is materialized.
  enum class IoDirection : uint8_t { Input, Output };

  struct DeclaredIoPin {
    std::string name;
    Port_id     port_id    = 0;
    bool        loop_break = false;
    // Per-declared-pin bitwidth. 0 means unspecified (defaulted by the
    // consumer). Stored on GraphIO rather than PinEntry so that declared
    // IO bits survive even when the body has not been materialized.
    uint32_t    bits       = 0;
    // Sign hint: true == unsigned, false == signed/unspecified. Mirrors
    // LiveHD's `is_unsign()` predicate on graph IO pins.
    bool        unsign     = false;
  };

private:
  struct DeclaredIoPinRef {
    IoDirection direction = IoDirection::Input;
    size_t      index     = 0;
  };

  GraphLibrary* owner_lib_ = nullptr;
  Gid           gid_       = Gid_invalid;
  std::string   name_;

  std::vector<DeclaredIoPin>                                                  input_pin_decls_;
  std::vector<DeclaredIoPin>                                                  output_pin_decls_;
  ankerl::unordered_dense::map<std::string, DeclaredIoPinRef, Name_hash, Name_eq> declared_io_pins_;

  GraphIO(GraphLibrary* owner_lib, Gid gid, std::string name) : owner_lib_(owner_lib), gid_(gid), name_(std::move(name)) {}
  void reindex_declared_io_pins(IoDirection direction, size_t start_index);
  void invalidate_from_library() noexcept;

public:
  GraphIO(const GraphIO&)            = delete;
  GraphIO& operator=(const GraphIO&) = delete;
  GraphIO(GraphIO&&)                 = delete;
  GraphIO& operator=(GraphIO&&)      = delete;

  [[nodiscard]] Gid                          get_gid() const noexcept { return gid_; }
  [[nodiscard]] std::string_view             get_name() const noexcept { return name_; }
  [[nodiscard]] GraphLibrary*                get_library() const noexcept { return owner_lib_; }
  [[nodiscard]] std::shared_ptr<Graph>       get_graph();
  [[nodiscard]] std::shared_ptr<const Graph> get_graph() const;
  [[nodiscard]] std::shared_ptr<Graph>       create_graph();
  [[nodiscard]] bool                         has_graph() const;
  void                                       add_input(std::string_view name, Port_id port_id, bool loop_break = false);
  void                                       add_output(std::string_view name, Port_id port_id, bool loop_break = false);
  void                                       delete_input(std::string_view name);
  void                                       delete_output(std::string_view name);
  void                                       clear();
  // Drop all declared IO pins (and any materialized counterpart pins on the
  // body) but keep this GraphIO attached to its owning GraphLibrary, with
  // its Gid and name preserved. Distinct from clear(), which tombstones the
  // entire GraphIO+Graph entry. Used by callers that want "reset all IO
  // declarations on this slot" without invalidating the slot itself.
  void                                       reset_declarations();
  [[nodiscard]] bool                         has_input(std::string_view name) const;
  [[nodiscard]] bool                         has_output(std::string_view name) const;
  [[nodiscard]] bool                         is_loop_break(std::string_view name) const;
  [[nodiscard]] Port_id                      get_input_port_id(std::string_view name) const;
  [[nodiscard]] Port_id                      get_output_port_id(std::string_view name) const;
  // Reverse lookup: does any declared input/output have this port_id?
  // O(input_count + output_count) — linear scan, no internal index. Add an
  // index if a hot path needs frequent calls.
  [[nodiscard]] bool                         has_pin_with_port_id(Port_id port_id) const;
  [[nodiscard]] bool                         has_input_with_port_id(Port_id port_id) const;
  [[nodiscard]] bool                         has_output_with_port_id(Port_id port_id) const;

  // Per-declared-pin bitwidth. `set_bits` stamps the value; `get_bits`
  // returns the stored bits (0 = unspecified). Asserts the pin name exists.
  void                   set_bits(std::string_view name, uint32_t bits);
  [[nodiscard]] uint32_t get_bits(std::string_view name) const;

  // Per-declared-pin sign. `set_unsign(name, true)` marks unsigned;
  // `set_unsign(name, false)` marks signed. `is_unsign` reads it back.
  void               set_unsign(std::string_view name, bool unsign_value);
  [[nodiscard]] bool is_unsign(std::string_view name) const;

  // Public iteration over declared inputs / outputs. Returns const refs to
  // the underlying vectors so callers can range-for without exposing the
  // DeclaredIoPinRef hash map. Order is declaration order (matches Verilog
  // positional argument order via DeclaredIoPin::port_id).
  [[nodiscard]] const std::vector<DeclaredIoPin>& get_input_pin_decls() const { return input_pin_decls_; }
  [[nodiscard]] const std::vector<DeclaredIoPin>& get_output_pin_decls() const { return output_pin_decls_; }

  friend class Graph;
  friend class Node_class;
  friend class Pin_class;
  friend class GraphLibrary;
};

class GraphLibrary {
public:
  static constexpr Gid invalid_id = Gid_invalid;

  GraphLibrary(const GraphLibrary&)            = delete;
  GraphLibrary& operator=(const GraphLibrary&) = delete;
  GraphLibrary(GraphLibrary&&)                 = delete;
  GraphLibrary& operator=(GraphLibrary&&)      = delete;

  GraphLibrary() = default;  // gid 0 (Gid_invalid) is simply never a map key

  ~GraphLibrary() {
    for (auto& [gid, graph] : graphs_) {
      if (graph) {
        graph->invalidate_from_library();
      }
    }
  }

  [[nodiscard]] std::shared_ptr<GraphIO> create_io(std::string_view name) {
    assert(!name.empty() && "create_io: name is required");
    std::unique_lock lock(registry_mu_);
    const auto       it = deleted_name_to_id_.find(std::string(name));
    if (it != deleted_name_to_id_.end()) {
      const auto reused_gid = it->second;
      deleted_name_to_id_.erase(it);
      // Reuse the original gid (so parent bodies' subnode refs resolve) UNLESS a
      // fresh name's hash has since claimed it — then fall through to a new gid.
      if (graph_ios_.find(reused_gid) == graph_ios_.end()) {
        return create_io_impl_unlocked(reused_gid, name);
      }
    }
    return create_io_impl_unlocked(pick_gid_for_name_unlocked(name), name);
  }

  [[nodiscard]] std::shared_ptr<GraphIO> find_io(std::string_view name) {
    std::shared_lock lock(registry_mu_);
    return find_io_unlocked(name);
  }

  [[nodiscard]] std::shared_ptr<const GraphIO> find_io(std::string_view name) const {
    std::shared_lock lock(registry_mu_);
    return find_io_unlocked(name);
  }

  // Gid-keyed lookup. Returns nullptr for invalid or unknown Gids.
  [[nodiscard]] std::shared_ptr<GraphIO> find_io(Gid id) {
    std::shared_lock lock(registry_mu_);
    return find_io_unlocked(id);
  }

  [[nodiscard]] std::shared_ptr<const GraphIO> find_io(Gid id) const {
    std::shared_lock lock(registry_mu_);
    return find_io_unlocked(id);
  }

  [[nodiscard]] bool has_graph(Gid id) const noexcept {
    std::shared_lock lock(registry_mu_);
    // A pending body exists on disk even though it is not yet materialized.
    return has_graph_unlocked(id) || pending_body_dir_.find(id) != pending_body_dir_.end();
  }

  [[nodiscard]] std::shared_ptr<Graph> get_graph(Gid id) {
    {
      std::shared_lock lock(registry_mu_);
      if (has_graph_unlocked(id)) {
        return graph_at_unlocked(id);  // already materialized (fast reader path)
      }
      if (pending_body_dir_.find(id) == pending_body_dir_.end()) {
        assert(has_graph_unlocked(id) && "get_graph: unknown gid");
        return graph_at_unlocked(id);  // unknown / deleted (preserve old contract)
      }
    }
    // Pending: read the body under the writer lock. The registry mutex is
    // non-recursive and non-upgradeable, so we released the reader lock above
    // before taking the writer lock; materialize_body_unlocked re-checks the race.
    std::unique_lock lock(registry_mu_);
    return materialize_body_unlocked(id);
  }

  [[nodiscard]] std::shared_ptr<const Graph> get_graph(Gid id) const {
    return const_cast<GraphLibrary*>(this)->get_graph(id);  // lazy-load cache fill
  }

  // Read-only handle lookup. Returns nullptr unless the slot is Public —
  // i.e., the graph has been created and the writable handle has been
  // committed or released. While a writer (from create_graph or
  // find_graph_rw) is alive, this returns nullptr as if the graph did not
  // exist.
  [[nodiscard]] std::shared_ptr<const Graph> find_graph(std::string_view name) const {
    std::shared_lock lock(registry_mu_);
    auto             gio = find_io_unlocked(name);
    if (!gio) {
      return {};
    }
    const Gid  gid = gio->get_gid();
    const auto g   = graph_at_unlocked(gid);
    if (!g || g->deleted_) {
      return {};
    }
    const auto* state = slot_state_at_unlocked(gid);
    if (!state || state->load(std::memory_order_acquire) != static_cast<uint8_t>(SlotState::Public)) {
      return {};
    }
    return g;
  }

  // Acquire a writable handle on an existing public graph. CAS-transitions
  // the slot Public -> Writing only when no outstanding read-only handles
  // exist; otherwise returns nullptr (caller retries). While the returned
  // handle is alive, find_graph(name) returns nullptr to all callers; on
  // last-release the slot transitions back to Public. Mutations happen in
  // place — there is no abort semantic for find_graph_rw.
  [[nodiscard]] std::shared_ptr<Graph> find_graph_rw(std::string_view name) {
    std::shared_lock lock(registry_mu_);
    auto             gio = find_io_unlocked(name);
    if (!gio) {
      return {};
    }
    const Gid  gid = gio->get_gid();
    const auto it  = graphs_.find(gid);
    if (it == graphs_.end() || !it->second || it->second->deleted_) {
      return {};
    }
    auto* state = slot_state_at_unlocked(gid);
    if (!state) {
      return {};
    }
    uint8_t expected = static_cast<uint8_t>(SlotState::Public);
    if (!state->compare_exchange_strong(expected,
                                        static_cast<uint8_t>(SlotState::Writing),
                                        std::memory_order_acquire,
                                        std::memory_order_relaxed)) {
      return {};
    }
    if (it->second.use_count() > 1) {  // slot is the only owner unless a reader holds one
      state->store(static_cast<uint8_t>(SlotState::Public), std::memory_order_release);
      return {};
    }
    if (auto* ab = abort_pending_at_unlocked(gid)) {
      ab->store(false, std::memory_order_relaxed);
    }
    return make_writer_handle_unlocked(gid, /*from_create=*/false);
  }

  // Library-wide reverse lookup for an opaque Flat_index key. The gid selects
  // the graph body; the index's raw value is the Nid / Pid. Returns an empty
  // handle if the graph is tombstone-deleted or the raw id is invalid for
  // that body.
  [[nodiscard]] Node_class get_node(Flat_index idx) {
    std::shared_ptr<Graph> graph;
    {
      std::shared_lock lock(registry_mu_);
      if (!has_graph_unlocked(idx.gid)) {
        return Node_class();
      }
      graph = graph_at_unlocked(idx.gid);
    }
    if (!graph->is_node_valid(idx.value)) {
      return Node_class();
    }
    return Node_class(graph.get(), idx.gid, idx.value);
  }
  [[nodiscard]] Pin_class get_pin(Flat_index idx) {
    std::shared_ptr<Graph> graph;
    {
      std::shared_lock lock(registry_mu_);
      if (!has_graph_unlocked(idx.gid)) {
        return Pin_class();
      }
      graph = graph_at_unlocked(idx.gid);
    }
    if (!graph->is_pin_valid(idx.value)) {
      return Pin_class();
    }
    Pin_class pin = graph->make_pin_class(idx.value);
    pin.context_  = Handle_context::Flat;
    pin.root_gid_ = idx.gid;
    return pin;
  }

  [[nodiscard]] uint64_t mutation_epoch() const noexcept { return mutation_epoch_.load(std::memory_order_acquire); }

  // Tombstone-delete (IDs are not reused).
  void delete_graph(Gid id) noexcept {
    std::unique_lock lock(registry_mu_);
    delete_graph_unlocked(id);
  }

  void delete_graph(const std::shared_ptr<Graph>& graph) noexcept {
    if (!graph) {
      return;
    }
    delete_graph(graph->get_gid());
  }

  void delete_graphio(const std::shared_ptr<GraphIO>& graphio) noexcept {
    if (!graphio) {
      return;
    }
    std::unique_lock lock(registry_mu_);
    delete_graphio_unlocked(graphio);
  }

  void delete_graphio(std::string_view name) noexcept {
    std::unique_lock lock(registry_mu_);
    auto             gio = find_io_unlocked(name);
    if (!gio) {
      return;
    }
    delete_graphio_unlocked(gio);
  }

  // Number of live graph-body entries. NOTE: gids are sparse name-hashes, so
  // this is NOT an upper bound on gid values — do not iterate `gid<capacity()`;
  // use all_gids() / all_io_gids() instead.
  [[nodiscard]] size_t capacity() const noexcept {
    std::shared_lock lock(registry_mu_);
    return graphs_.size() + pending_body_dir_.size();  // materialized + pending-on-disk
  }

  // Count of live graphs.
  [[nodiscard]] Gid live_count() const noexcept {
    std::shared_lock lock(registry_mu_);
    return live_count_ + static_cast<Gid>(pending_body_dir_.size());  // materialized + pending
  }

  // gids of all live graph BODIES, ascending. Gids are sparse name-hashes now,
  // so callers must iterate this set rather than `for (gid=1; gid<capacity())`.
  [[nodiscard]] std::vector<Gid> all_gids() const {
    std::shared_lock lock(registry_mu_);
    std::vector<Gid> out;
    out.reserve(graphs_.size() + pending_body_dir_.size());
    for (const auto& [gid, g] : graphs_) {
      if (g && !g->deleted_) {
        out.push_back(gid);
      }
    }
    for (const auto& [gid, dir] : pending_body_dir_) {  // persisted, not yet materialized
      out.push_back(gid);                               // (a gid is never in both maps)
    }
    std::sort(out.begin(), out.end());
    return out;
  }

  // gids of all live GraphIOs (declared interfaces, with or without a body),
  // ascending. Use for IO-level iteration (e.g. cross-library black-box stubs).
  [[nodiscard]] std::vector<Gid> all_io_gids() const {
    std::shared_lock lock(registry_mu_);
    std::vector<Gid> out;
    out.reserve(graph_ios_.size());
    for (const auto& [gid, gio] : graph_ios_) {
      if (gio) {
        out.push_back(gid);
      }
    }
    std::sort(out.begin(), out.end());
    return out;
  }

  // Library-level source-provenance base (hhds-srcloc): the loaded srcmap.txt
  // table plus the save-time union of the per-graph locators. Graphs chain to
  // it for resolution; it is only mutated under the EXCLUSIVE registry lock by
  // load(), load_merge() and save(). Per-graph srcid resolution chains to it
  // lock-free, so resolution must not run concurrently with those three.
  [[nodiscard]] const Source_locator& source_map() const noexcept { return *srcmap_sp_; }
  // Non-const access is for single-threaded setup/tests only — never while
  // other threads hold graphs from this library.
  [[nodiscard]] Source_locator&       source_map() noexcept { return *srcmap_sp_; }

  // Shared in-memory source map (hhds-srcloc). A Forest and a GraphLibrary that
  // persist into the SAME db directory must share ONE table (LNAST and LGraph
  // come from the same source, so their content-addressed ids coincide and a
  // single srcmap.txt writer avoids the clobber). source_map_shared() hands out
  // this library's table; share_source_map() adopts another's. Call BEFORE
  // creating graphs in the typical flow; for safety any existing graphs are
  // re-based here under the registry lock. `persist` selects whether THIS object
  // writes/reads srcmap.txt on save()/load() — exactly one sharer should persist
  // (single-process; save() remains a single-threaded barrier — do not save two
  // sharers of one map concurrently). Prefer making the GraphLibrary the
  // persister: its save() folds the per-graph deltas into the shared table, so
  // letting it write srcmap.txt guarantees those deltas reach disk. If instead
  // the Forest persists, save the library FIRST so its fold lands before the
  // Forest writes the table (else graph bodies would reference srcids absent
  // from srcmap.txt).
  [[nodiscard]] std::shared_ptr<Source_locator> source_map_shared() const noexcept { return srcmap_sp_; }
  void                                          share_source_map(std::shared_ptr<Source_locator> sp, bool persist) {
    assert(sp != nullptr && "share_source_map: null source map");
    std::unique_lock lock(registry_mu_);
    srcmap_sp_      = std::move(sp);
    persist_srcmap_ = persist;
    for (auto& [gid, g] : graphs_) {
      if (g) {
        g->source_locator().set_base(srcmap_sp_.get());
      }
    }
  }

  // Persistence — saves all declarations (text) and bodies (binary).
  // db_path is the root directory (e.g., "my_db/").
  void save(const std::string& db_path) const;
  void load(const std::string& db_path);

  // Merge another saved library at db_path INTO this one (no clear) — the
  // graph-library linker primitive (task 1m-C). Conflict policy:
  //   - name already present here  → keep ours (dedup); load the incoming body
  //     only if ours is an IO-only stub. (Same name with a different body is a
  //     genuine ambiguity; not deduped away silently — see assert.)
  //   - name new                   → assign its canonical gid (hash of the name,
  //     probed on collision). When both libraries use name-hash gids the gid is
  //     identical, so the merge is conflict-free; otherwise an incoming gid is
  //     remapped and every Sub (subnode) reference in the loaded bodies is
  //     rewritten through the remap table.
  void load_merge(const std::string& db_path);

private:
  [[nodiscard]] std::shared_ptr<GraphIO> find_io_unlocked(Gid id) const {
    if (id == Gid_invalid) {
      return {};
    }
    return io_at_unlocked(id);
  }

  [[nodiscard]] std::shared_ptr<GraphIO> find_io_unlocked(std::string_view name) const {
    if (name.empty()) {
      return {};
    }

    const auto it = graph_name_to_id_.find(name);  // transparent Name_hash/Name_eq: no std::string alloc
    if (it == graph_name_to_id_.end()) {
      return {};
    }
    return io_at_unlocked(it->second);
  }

  [[nodiscard]] bool has_graph_unlocked(Gid id) const noexcept {
    const auto g = graph_at_unlocked(id);
    return g != nullptr && !g->deleted_;
  }

  [[nodiscard]] std::shared_ptr<GraphIO> create_io_impl_unlocked(Gid id, std::string_view name) {
    assert(id != invalid_id && "create_io: graph id 0 is reserved");
    assert(!name.empty() && "create_io: name is required");
    assert_name_available_unlocked(name);
    assert(graph_ios_.find(id) == graph_ios_.end() && "create_io: explicit id already exists");

    auto graphio   = std::shared_ptr<GraphIO>(new GraphIO(this, id, std::string(name)));
    graph_ios_[id] = graphio;  // body created lazily by create_graph_body
    ensure_slot_atomics_unlocked(id);
    graph_slot_states_.at(id)->store(static_cast<uint8_t>(SlotState::Empty), std::memory_order_release);
    graph_slot_abort_pending_.at(id)->store(false, std::memory_order_relaxed);

    graph_name_to_id_.emplace(std::string(name), id);
    note_graph_mutation();
    return graphio;
  }

  [[nodiscard]] std::shared_ptr<Graph> create_graph_body(const std::shared_ptr<GraphIO>& graphio) {
    std::unique_lock lock(registry_mu_);
    return create_graph_body_unlocked(graphio);
  }

  // Populating entry point. CAS the slot Empty -> Writing; if not Empty
  // (already being written, already public), returns nullptr. The returned
  // handle aliases a GraphWriterCleanup whose destructor publishes the slot
  // on last-release (or tears it back to Empty if Graph::abort() was
  // called).
  [[nodiscard]] std::shared_ptr<Graph> create_graph_body_unlocked(const std::shared_ptr<GraphIO>& graphio) {
    assert(graphio != nullptr && "create_graph_body: null GraphIO");

    const Gid gid = graphio->get_gid();
    assert(io_at_unlocked(gid) == graphio && "create_graph_body: GraphIO is not owned by this library");

    ensure_slot_atomics_unlocked(gid);
    auto&   state    = *graph_slot_states_.at(gid);
    uint8_t expected = static_cast<uint8_t>(SlotState::Empty);
    if (!state.compare_exchange_strong(expected,
                                       static_cast<uint8_t>(SlotState::Writing),
                                       std::memory_order_acquire,
                                       std::memory_order_relaxed)) {
      // Slot is already Writing or Public — caller must use find_graph /
      // find_graph_rw.
      return {};
    }
    graph_slot_abort_pending_.at(gid)->store(false, std::memory_order_relaxed);
    // A fresh body supersedes any lazily-pending on-disk body for this gid (e.g.
    // emit-dir reuse: delete_graph then create_graph over a persisted library).
    pending_body_dir_.erase(gid);

    if (auto existing = graph_at_unlocked(gid); !existing || existing->deleted_) {
      std::shared_ptr<Graph> graph = std::make_shared<Graph>();
      graph->bind_library(this, gid);
      graph->set_name(graphio->get_name());
      graph->graphio_owner_ = graphio;
      for (const auto& input : graphio->input_pin_decls_) {
        (void)graph->materialize_declared_io_pin(input.name, input.port_id, Graph::INPUT_NODE, graph->input_pins_);
      }
      for (const auto& output : graphio->output_pin_decls_) {
        (void)graph->materialize_declared_io_pin(output.name, output.port_id, Graph::OUTPUT_NODE, graph->output_pins_);
      }

      graphs_[gid] = graph;
      ++live_count_;
    } else {
      existing->frozen_ = false;
    }
    note_graph_mutation();
    return make_writer_handle_unlocked(gid, /*from_create=*/true);
  }

  // Internal load path: directly publish a freshly deserialized body to
  // the Public state, bypassing the Writing window. Caller must hold
  // unique_lock(registry_mu_).
  [[nodiscard]] std::shared_ptr<Graph> create_graph_body_loaded_unlocked(const std::shared_ptr<GraphIO>& graphio) {
    assert(graphio != nullptr && "create_graph_body_loaded: null GraphIO");
    const Gid gid = graphio->get_gid();
    assert(io_at_unlocked(gid) == graphio && "create_graph_body_loaded: GraphIO is not owned by this library");
    ensure_slot_atomics_unlocked(gid);
    pending_body_dir_.erase(gid);  // this body is now (being) materialized in memory

    if (auto existing = graph_at_unlocked(gid); !existing || existing->deleted_) {
      std::shared_ptr<Graph> graph = std::make_shared<Graph>();
      graph->bind_library(this, gid);
      graph->set_name(graphio->get_name());
      graph->graphio_owner_ = graphio;
      for (const auto& input : graphio->input_pin_decls_) {
        (void)graph->materialize_declared_io_pin(input.name, input.port_id, Graph::INPUT_NODE, graph->input_pins_);
      }
      for (const auto& output : graphio->output_pin_decls_) {
        (void)graph->materialize_declared_io_pin(output.name, output.port_id, Graph::OUTPUT_NODE, graph->output_pins_);
      }
      graphs_[gid] = graph;
      ++live_count_;
    }
    graph_slot_states_.at(gid)->store(static_cast<uint8_t>(SlotState::Public), std::memory_order_release);
    graph_slot_abort_pending_.at(gid)->store(false, std::memory_order_relaxed);
    note_graph_mutation();
    return graph_at_unlocked(gid);
  }

  // Read a pending (persisted-but-unloaded) body into memory. Caller MUST hold
  // the UNIQUE (writer) lock — this mutates graphs_/live_count_/slot state. Safe
  // to call after swapping a reader lock for the writer lock: it double-checks
  // whether another thread materialized the gid in the gap before doing the read.
  // Returns the (now materialized) body, or the existing body / nullptr if the
  // gid was not actually pending.
  [[nodiscard]] std::shared_ptr<Graph> materialize_body_unlocked(Gid id) {
    if (auto g = graph_at_unlocked(id); g && !g->deleted_) {
      return g;  // materialized by a racing thread while we swapped locks
    }
    const auto pit = pending_body_dir_.find(id);
    if (pit == pending_body_dir_.end()) {
      return graph_at_unlocked(id);  // not pending (raced-and-erased, or unknown)
    }
    const std::string dir = pit->second;  // copy before the map is mutated
    const auto        gio = io_at_unlocked(id);
    assert(gio && "materialize_body: pending gid without a GraphIO");
    auto graph = create_graph_body_loaded_unlocked(gio);
    graph->load_body(dir);
    pending_body_dir_.erase(id);
    return graph;
  }

  void delete_graph_unlocked(Gid id) noexcept {
    // Drop any lazily-pending on-disk body too, else it would still resolve via
    // get_graph() after the delete (e.g. emit-dir reuse deletes a persisted graph
    // before recreating it).
    pending_body_dir_.erase(id);
    if (auto it = graphs_.find(id); it != graphs_.end() && it->second && !it->second->deleted_) {
      it->second->invalidate_from_library();
      it->second.reset();
      --live_count_;
      note_graph_mutation();
    }
    if (auto* state = slot_state_at_unlocked(id)) {
      state->store(static_cast<uint8_t>(SlotState::Empty), std::memory_order_release);
    }
    if (auto* ab = abort_pending_at_unlocked(id)) {
      ab->store(false, std::memory_order_relaxed);
    }
  }

  void delete_graphio_unlocked(const std::shared_ptr<GraphIO>& graphio) noexcept {
    if (!graphio) {
      return;
    }

    const Gid gid = graphio->get_gid();
    if (io_at_unlocked(gid) != graphio) {
      return;
    }

    const auto name = std::string(graphio->get_name());
    delete_graph_unlocked(gid);
    if (!name.empty()) {
      graph_name_to_id_.erase(name);
      deleted_name_to_id_[name] = gid;
    }
    graphio->invalidate_from_library();
    graph_ios_.erase(gid);
    note_graph_mutation();
  }

  void assert_name_available_unlocked(std::string_view name) const noexcept {
    if (name.empty()) {
      return;
    }

    assert(graph_name_to_id_.find(name) == graph_name_to_id_.end() && "create_graph: graph name already exists");
  }

  void note_graph_mutation() const noexcept { mutation_epoch_.fetch_add(1, std::memory_order_acq_rel); }

  // gid value space: a name hashes into [1, kGidModulus). kGidModulus stays well
  // under 2^Nid_bits because a Sub stores the callee gid packed into a Nid_bits
  // field (Graph::NodeEntry::set_subnode). 2^40 gives a huge, sparse space so
  // distinct names rarely collide, while leaving headroom below the 2^42 cap.
  static constexpr Gid kGidModulus = (Gid{1} << 40);

  // Deterministic hash over the name → a stable gid, identical across runs,
  // platforms and libraries (std::hash is NONE of these, so it must not be used
  // here — cross-library gid agreement is the whole point). Names are
  // case-sensitive, so `MODULE_FO` and `ModuLe_Fo` are DISTINCT and map to
  // different gids, matching the case-sensitive graph_name_to_id_ lookup.
  [[nodiscard]] static Gid hash_name_to_gid(std::string_view name) noexcept {
    const uint64_t h = name_hash(name);
    const Gid      g = static_cast<Gid>(h % (kGidModulus - 1)) + 1;  // ∈ [1, kGidModulus)
    return g;
  }

  // Choose a free gid for a fresh name: the name hash, linear-probed (wrapping
  // inside [1, kGidModulus)) past any colliding occupied slot. Caller holds the
  // unique_lock. The common (collision-free) case returns the bare name hash, so
  // the same name maps to the same gid in every library.
  [[nodiscard]] Gid pick_gid_for_name_unlocked(std::string_view name) const {
    Gid g = hash_name_to_gid(name);
    while (graph_ios_.find(g) != graph_ios_.end()) {
      g = (g + 1 >= kGidModulus) ? Gid{1} : g + 1;
    }
    return g;
  }

  // gid-keyed reads (nullptr when absent). MUST be used on read paths — never
  // operator[], which would insert a null entry (and mutate under a shared_lock).
  [[nodiscard]] std::shared_ptr<GraphIO> io_at_unlocked(Gid id) const noexcept {
    const auto it = graph_ios_.find(id);
    return it == graph_ios_.end() ? nullptr : it->second;
  }
  [[nodiscard]] std::shared_ptr<Graph> graph_at_unlocked(Gid id) const noexcept {
    const auto it = graphs_.find(id);
    return it == graphs_.end() ? nullptr : it->second;
  }
  // Raw slot-atomic pointers (nullptr when the gid has no slot). The atomic
  // objects are heap-allocated (unique_ptr), so the pointer stays valid across
  // map rehashes as long as the slot is not deleted.
  [[nodiscard]] std::atomic<uint8_t>* slot_state_at_unlocked(Gid id) const noexcept {
    const auto it = graph_slot_states_.find(id);
    return it == graph_slot_states_.end() ? nullptr : it->second.get();
  }
  [[nodiscard]] std::atomic<bool>* abort_pending_at_unlocked(Gid id) const noexcept {
    const auto it = graph_slot_abort_pending_.find(id);
    return it == graph_slot_abort_pending_.end() ? nullptr : it->second.get();
  }

  // Multi-reader / single-writer lock guarding the registry containers below.
  // Graph bodies remain single-threaded per pointer; this lock protects only
  // the slot vectors and name maps so concurrent find_io / find_graph callers
  // from different threads can proceed in parallel.
  mutable Prefer_writer_shared_mutex                             registry_mu_;
  // gid-keyed maps (Task 1m / hhds gid refactor): gids are a deterministic hash
  // of the graph name (see pick_gid_for_name_unlocked) rather than a positional
  // counter, so two libraries assign the SAME gid to the SAME name — making a
  // future cross-library merge a no-op for matching names (only true hash
  // collisions need remapping). The map storage tolerates the resulting sparse,
  // large gids that a vector could not. gid 0 (Gid_invalid) is never a key.
  absl::flat_hash_map<Gid, std::shared_ptr<GraphIO>>             graph_ios_;
  absl::flat_hash_map<Gid, std::shared_ptr<Graph>>               graphs_;
  // Lazy body materialization (hhds lazy-load): load() records every persisted
  // graph_<gid>/ dir here instead of eagerly reading its body. The body is read
  // on first get_graph(id) / GraphIO::get_graph() and the gid is erased. A gid is
  // NEVER simultaneously in `graphs_` (materialized) and here (pending). Consumers
  // that touch only a sub-hierarchy pay for just the graphs they visit.
  absl::flat_hash_map<Gid, std::string>                          pending_body_dir_;
  ankerl::unordered_dense::map<std::string, Gid, Name_hash, Name_eq> graph_name_to_id_;
  ankerl::unordered_dense::map<std::string, Gid, Name_hash, Name_eq> deleted_name_to_id_;
  // Source-provenance base (hhds-srcloc): see source_map(). Held by shared_ptr
  // so a Forest and a GraphLibrary sharing a db directory can share ONE
  // in-memory table (share_source_map); standalone libraries own a private one.
  // The const save() folds the per-graph deltas into the pointee — allowed
  // through the shared_ptr without `mutable`.
  std::shared_ptr<Source_locator>                                srcmap_sp_      = std::make_shared<Source_locator>();
  // false on a borrower of a shared map: its save()/load() skip srcmap.txt and
  // defer persistence to the owning sharer.
  bool                                                           persist_srcmap_ = true;
  // count of live graphs
  Gid                                                            live_count_     = 0;
  mutable std::atomic<uint64_t>                                  mutation_epoch_ = 1;

  // Per-slot state machine for the body in graphs_[idx]:
  //   Empty   -> no body; create_graph may CAS to Writing
  //   Writing -> a writable handle is alive; find_graph returns nullptr
  //   Public  -> body is publicly findable
  enum class SlotState : uint8_t { Empty = 0, Writing = 1, Public = 2 };

  // Heap-allocated atomics so the vector can grow under unique_lock without
  // invalidating addresses observed by concurrent readers/writers.
  absl::flat_hash_map<Gid, std::unique_ptr<std::atomic<uint8_t>>> graph_slot_states_;
  // Latched by Graph::abort(); read by the WriterCleanup deleter on
  // last-release of the writable handle.
  absl::flat_hash_map<Gid, std::unique_ptr<std::atomic<bool>>>    graph_slot_abort_pending_;

  // Cleanup payload that lives on the control block of every writable Graph
  // handle returned by create_graph / find_graph_rw. When the last copy of
  // the aliased writable shared_ptr drops, ~GraphWriterCleanup runs and
  // performs the slot transition (Writing -> Public, or Writing -> Empty
  // for an aborted create_graph).
  struct GraphWriterCleanup {
    GraphLibrary*          library;
    Gid                    gid;
    bool                   from_create;
    std::shared_ptr<Graph> graph_keepalive;
    ~GraphWriterCleanup();
  };
  friend struct GraphWriterCleanup;

  // Ensure the heap-allocated slot atomics exist for `id` (idempotent). The
  // atomic objects have stable addresses across map rehashes (unique_ptr), so a
  // pointer captured under the lock stays valid until the slot is deleted.
  void ensure_slot_atomics_unlocked(Gid id) {
    if (graph_slot_states_.find(id) == graph_slot_states_.end()) {
      graph_slot_states_.emplace(id, std::make_unique<std::atomic<uint8_t>>(static_cast<uint8_t>(SlotState::Empty)));
    }
    if (graph_slot_abort_pending_.find(id) == graph_slot_abort_pending_.end()) {
      graph_slot_abort_pending_.emplace(id, std::make_unique<std::atomic<bool>>(false));
    }
  }

  std::shared_ptr<Graph> make_writer_handle_unlocked(Gid gid, bool from_create) {
    auto g = graph_at_unlocked(gid);
    assert(g && "make_writer_handle: empty slot");
    auto cleanup             = std::make_shared<GraphWriterCleanup>();
    cleanup->library         = this;
    cleanup->gid             = gid;
    cleanup->from_create     = from_create;
    cleanup->graph_keepalive = std::move(g);
    return std::shared_ptr<Graph>(cleanup, cleanup->graph_keepalive.get());
  }

  friend class Graph;
  friend class GraphIO;
};

inline std::shared_ptr<Graph> GraphIO::get_graph() {
  if (owner_lib_ == nullptr) {
    return {};
  }
  {
    std::shared_lock lock(owner_lib_->registry_mu_);
    const auto       graph = owner_lib_->graph_at_unlocked(gid_);
    if (graph) {
      return graph->deleted_ ? std::shared_ptr<Graph>{} : graph;  // already materialized
    }
    if (owner_lib_->pending_body_dir_.find(gid_) == owner_lib_->pending_body_dir_.end()) {
      return {};  // no body materialized and none pending on disk
    }
  }
  // Pending: read the body under the writer lock (see GraphLibrary::get_graph).
  auto*            lib = const_cast<GraphLibrary*>(owner_lib_);
  std::unique_lock lock(lib->registry_mu_);
  return lib->materialize_body_unlocked(gid_);
}

inline std::shared_ptr<const Graph> GraphIO::get_graph() const {
  return const_cast<GraphIO*>(this)->get_graph();  // lazy-load cache fill
}

inline std::shared_ptr<Graph> GraphIO::create_graph() {
  assert(owner_lib_ != nullptr && "create_graph: GraphIO is no longer attached to a library");
  return owner_lib_->create_graph_body(shared_from_this());
}

inline bool GraphIO::has_graph() const {
  if (owner_lib_ == nullptr) {
    return false;
  }
  return owner_lib_->has_graph(gid_);
}

inline void GraphIO::reindex_declared_io_pins(IoDirection direction, size_t start_index) {
  auto& pins = direction == IoDirection::Input ? input_pin_decls_ : output_pin_decls_;
  for (size_t index = start_index; index < pins.size(); ++index) {
    auto it = declared_io_pins_.find(pins[index].name);
    assert(it != declared_io_pins_.end() && "reindex_declared_io_pins: missing IO-pin lookup entry");
    it->second.index = index;
  }
}

inline void GraphIO::invalidate_from_library() noexcept {
  owner_lib_ = nullptr;
  input_pin_decls_.clear();
  output_pin_decls_.clear();
  declared_io_pins_.clear();
}

inline GraphLibrary::GraphWriterCleanup::~GraphWriterCleanup() {
  // graph_keepalive->deleted_ is set by GraphLibrary's destructor (and by
  // delete_graph). If the graph is gone, the library may be too — bail
  // without touching the library pointer.
  if (!graph_keepalive || graph_keepalive->deleted_) {
    return;
  }
  if (library == nullptr) {
    return;
  }
  std::unique_lock lock(library->registry_mu_);
  auto*            state = library->slot_state_at_unlocked(gid);
  if (!state) {
    return;
  }
  if (state->load(std::memory_order_acquire) != static_cast<uint8_t>(GraphLibrary::SlotState::Writing)) {
    // commit() already flipped to Public, or the slot was deleted.
    return;
  }
  auto*      ab      = library->abort_pending_at_unlocked(gid);
  const bool aborted = from_create && ab && ab->load(std::memory_order_acquire);
  if (aborted) {
    if (auto g = library->graph_at_unlocked(gid)) {
      g->invalidate_from_library();
      library->graphs_.erase(gid);
      --library->live_count_;
    }
    ab->store(false, std::memory_order_relaxed);
    state->store(static_cast<uint8_t>(GraphLibrary::SlotState::Empty), std::memory_order_release);
    library->note_graph_mutation();
    return;
  }
  state->store(static_cast<uint8_t>(GraphLibrary::SlotState::Public), std::memory_order_release);
}

inline void Graph::commit() {
  if (frozen_) {
    return;
  }
  if (owner_lib_ == nullptr || self_gid_ == Gid_invalid) {
    frozen_ = true;
    return;
  }
  // const-cast: owner_lib_ is intentionally const within Graph, but commit
  // must mutate the library's slot state. The writer holds the only
  // writable handle here so the library is alive.
  auto*            lib = const_cast<GraphLibrary*>(owner_lib_);
  std::unique_lock lock(lib->registry_mu_);
  auto*            state = lib->slot_state_at_unlocked(self_gid_);
  if (!state) {
    frozen_ = true;
    return;
  }
  uint8_t expected = static_cast<uint8_t>(GraphLibrary::SlotState::Writing);
  state->compare_exchange_strong(expected,
                                 static_cast<uint8_t>(GraphLibrary::SlotState::Public),
                                 std::memory_order_release,
                                 std::memory_order_relaxed);
  frozen_ = true;
}

inline void Graph::abort() {
  if (owner_lib_ == nullptr || self_gid_ == Gid_invalid) {
    return;
  }
  auto*            lib = const_cast<GraphLibrary*>(owner_lib_);
  std::unique_lock lock(lib->registry_mu_);
  auto*            ab = lib->abort_pending_at_unlocked(self_gid_);
  if (!ab) {
    return;
  }
  ab->store(true, std::memory_order_release);
}

inline void GraphIO::add_input(std::string_view name, Port_id port_id, bool loop_break) {
  assert(owner_lib_ != nullptr && "add_input: GraphIO is no longer attached to a library");
  assert(!name.empty() && "add_input: name is required");

  const std::string key(name);
  assert(declared_io_pins_.find(key) == declared_io_pins_.end() && "add_input: input pin name already exists");
  input_pin_decls_.push_back(DeclaredIoPin{key, port_id, loop_break});
  declared_io_pins_.emplace(input_pin_decls_.back().name, DeclaredIoPinRef{IoDirection::Input, input_pin_decls_.size() - 1});

  if (auto graph = get_graph()) {
    (void)graph->materialize_declared_io_pin(name, port_id, Graph::INPUT_NODE, graph->input_pins_);
  } else if (owner_lib_ != nullptr) {
    owner_lib_->note_graph_mutation();
  }
}

inline void GraphIO::add_output(std::string_view name, Port_id port_id, bool loop_break) {
  assert(owner_lib_ != nullptr && "add_output: GraphIO is no longer attached to a library");
  assert(!name.empty() && "add_output: name is required");

  const std::string key(name);
  assert(declared_io_pins_.find(key) == declared_io_pins_.end() && "add_output: output pin name already exists");
  output_pin_decls_.push_back(DeclaredIoPin{key, port_id, loop_break});
  declared_io_pins_.emplace(output_pin_decls_.back().name, DeclaredIoPinRef{IoDirection::Output, output_pin_decls_.size() - 1});

  if (auto graph = get_graph()) {
    (void)graph->materialize_declared_io_pin(name, port_id, Graph::OUTPUT_NODE, graph->output_pins_);
  } else if (owner_lib_ != nullptr) {
    owner_lib_->note_graph_mutation();
  }
}

inline void GraphIO::delete_input(std::string_view name) {
  assert(owner_lib_ != nullptr && "delete_input: GraphIO is no longer attached to a library");

  const auto it = declared_io_pins_.find(std::string(name));
  assert(it != declared_io_pins_.end() && "delete_input: input pin name not found");
  assert(it->second.direction == IoDirection::Input && "delete_input: declared pin is not an input");

  const size_t index = it->second.index;
  if (auto graph = get_graph()) {
    const auto pin_it = graph->input_pins_.find(std::string(name));
    if (pin_it != graph->input_pins_.end()) {
      assert(graph->out_edges(graph->make_pin_class(pin_it->second | static_cast<Pid>(2))).empty()
             && "delete_input: input pin is still connected — disconnect before delete");
    }
    graph->erase_declared_io_pin(name, graph->input_pins_);
  } else if (owner_lib_ != nullptr) {
    owner_lib_->note_graph_mutation();
  }

  declared_io_pins_.erase(it);
  input_pin_decls_.erase(input_pin_decls_.begin() + static_cast<std::ptrdiff_t>(index));
  reindex_declared_io_pins(IoDirection::Input, index);
}

inline void GraphIO::delete_output(std::string_view name) {
  assert(owner_lib_ != nullptr && "delete_output: GraphIO is no longer attached to a library");

  const auto it = declared_io_pins_.find(std::string(name));
  assert(it != declared_io_pins_.end() && "delete_output: output pin name not found");
  assert(it->second.direction == IoDirection::Output && "delete_output: declared pin is not an output");

  const size_t index = it->second.index;
  if (auto graph = get_graph()) {
    const auto pin_it = graph->output_pins_.find(std::string(name));
    if (pin_it != graph->output_pins_.end()) {
      assert(graph->inp_edges(graph->make_pin_class(pin_it->second)).empty()
             && "delete_output: output pin is still connected — disconnect before delete");
    }
    graph->erase_declared_io_pin(name, graph->output_pins_);
  } else if (owner_lib_ != nullptr) {
    owner_lib_->note_graph_mutation();
  }

  declared_io_pins_.erase(it);
  output_pin_decls_.erase(output_pin_decls_.begin() + static_cast<std::ptrdiff_t>(index));
  reindex_declared_io_pins(IoDirection::Output, index);
}

inline void GraphIO::clear() {
  assert(owner_lib_ != nullptr && "clear: GraphIO is no longer attached to a library");
  owner_lib_->delete_graphio(shared_from_this());
}

inline void GraphIO::reset_declarations() {
  assert(owner_lib_ != nullptr && "reset_declarations: GraphIO is no longer attached to a library");
  // Drop body-side counterpart pins first, while we still know the names.
  if (auto graph = get_graph()) {
    for (const auto& input : input_pin_decls_) {
      graph->erase_declared_io_pin(input.name, graph->input_pins_);
    }
    for (const auto& output : output_pin_decls_) {
      graph->erase_declared_io_pin(output.name, graph->output_pins_);
    }
  }
  input_pin_decls_.clear();
  output_pin_decls_.clear();
  declared_io_pins_.clear();
  if (owner_lib_ != nullptr) {
    owner_lib_->note_graph_mutation();
  }
}

inline bool GraphIO::has_input(std::string_view name) const {
  const auto it = declared_io_pins_.find(std::string(name));
  return it != declared_io_pins_.end() && it->second.direction == IoDirection::Input;
}

inline bool GraphIO::has_output(std::string_view name) const {
  const auto it = declared_io_pins_.find(std::string(name));
  return it != declared_io_pins_.end() && it->second.direction == IoDirection::Output;
}

inline bool GraphIO::has_input_with_port_id(Port_id port_id) const {
  for (const auto& pin : input_pin_decls_) {
    if (pin.port_id == port_id) {
      return true;
    }
  }
  return false;
}

inline bool GraphIO::has_output_with_port_id(Port_id port_id) const {
  for (const auto& pin : output_pin_decls_) {
    if (pin.port_id == port_id) {
      return true;
    }
  }
  return false;
}

inline bool GraphIO::has_pin_with_port_id(Port_id port_id) const {
  return has_input_with_port_id(port_id) || has_output_with_port_id(port_id);
}

inline void GraphIO::set_bits(std::string_view name, uint32_t bits) {
  const auto it = declared_io_pins_.find(std::string(name));
  assert(it != declared_io_pins_.end() && "set_bits: declared pin name not found");
  auto& pins                  = it->second.direction == IoDirection::Input ? input_pin_decls_ : output_pin_decls_;
  pins[it->second.index].bits = bits;
}

inline uint32_t GraphIO::get_bits(std::string_view name) const {
  const auto it = declared_io_pins_.find(std::string(name));
  assert(it != declared_io_pins_.end() && "get_bits: declared pin name not found");
  const auto& pins = it->second.direction == IoDirection::Input ? input_pin_decls_ : output_pin_decls_;
  return pins[it->second.index].bits;
}

inline void GraphIO::set_unsign(std::string_view name, bool unsign_value) {
  const auto it = declared_io_pins_.find(std::string(name));
  assert(it != declared_io_pins_.end() && "set_unsign: declared pin name not found");
  auto& pins                    = it->second.direction == IoDirection::Input ? input_pin_decls_ : output_pin_decls_;
  pins[it->second.index].unsign = unsign_value;
}

inline bool GraphIO::is_unsign(std::string_view name) const {
  const auto it = declared_io_pins_.find(std::string(name));
  assert(it != declared_io_pins_.end() && "is_unsign: declared pin name not found");
  const auto& pins = it->second.direction == IoDirection::Input ? input_pin_decls_ : output_pin_decls_;
  return pins[it->second.index].unsign;
}

inline bool GraphIO::is_loop_break(std::string_view name) const {
  const auto it = declared_io_pins_.find(std::string(name));
  assert(it != declared_io_pins_.end() && "is_loop_break: declared pin name not found");
  if (it == declared_io_pins_.end()) {
    return false;
  }

  if (it->second.direction == IoDirection::Input) {
    return input_pin_decls_[it->second.index].loop_break;
  }
  return output_pin_decls_[it->second.index].loop_break;
}

inline Port_id GraphIO::get_input_port_id(std::string_view name) const {
  const auto it = declared_io_pins_.find(std::string(name));
  assert(it != declared_io_pins_.end() && "get_input_port_id: input pin name not found");
  assert(it == declared_io_pins_.end() || it->second.direction == IoDirection::Input);
  if (it == declared_io_pins_.end() || it->second.direction != IoDirection::Input) {
    return 0;
  }
  return input_pin_decls_[it->second.index].port_id;
}

inline Port_id GraphIO::get_output_port_id(std::string_view name) const {
  const auto it = declared_io_pins_.find(std::string(name));
  assert(it != declared_io_pins_.end() && "get_output_port_id: output pin name not found");
  assert(it == declared_io_pins_.end() || it->second.direction == IoDirection::Output);
  if (it == declared_io_pins_.end() || it->second.direction != IoDirection::Output) {
    return 0;
  }
  return output_pin_decls_[it->second.index].port_id;
}

inline void Graph::invalidate_traversal_caches() noexcept {
  dirty_                 = true;
  forward_caches_valid_  = false;
  backward_caches_valid_ = false;
  if (owner_lib_ != nullptr) {
    owner_lib_->note_graph_mutation();
  }
}

}  // namespace hhds
