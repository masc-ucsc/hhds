#pragma once

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "attr.hpp"
#include "attrs/name.hpp"
#include "graph_sizing.hpp"
#include "index.hpp"
#include "tree.hpp"
#include "unordered_dense.hpp"

namespace hhds {

constexpr int NUM_NODES         = 10;
constexpr int NUM_TYPES         = 3;
constexpr int MAX_PINS_PER_NODE = 10;

using OverflowVec = std::vector<ankerl::unordered_dense::set<Vid>>;

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

enum class Handle_context : uint8_t { Class, Flat, Hier };

class Pin_class {
public:
  Pin_class() = default;
  Pin_class(Graph* graph_value, Nid raw_nid_value, Port_id port_id_value, Pid pin_pid_value)
      : graph_(graph_value), raw_nid(raw_nid_value & ~static_cast<Nid>(2)), port_id(port_id_value), pin_pid(pin_pid_value) {}
  Pin_class(Nid raw_nid_value, Port_id port_id_value, Pid pin_pid_value)
      : raw_nid(raw_nid_value & ~static_cast<Nid>(2)), port_id(port_id_value), pin_pid(pin_pid_value) {}

  [[nodiscard]] Node_class        get_master_node() const;
  [[nodiscard]] constexpr Nid     get_debug_nid() const noexcept { return raw_nid; }
  [[nodiscard]] constexpr Pid     get_debug_pid() const noexcept { return pin_pid; }
  [[nodiscard]] constexpr Port_id get_port_id() const noexcept { return port_id; }
  [[nodiscard]] Graph*            get_graph() const noexcept { return graph_; }
  [[nodiscard]] std::string_view  get_pin_name() const;
  [[nodiscard]] bool              is_valid() const noexcept;
  [[nodiscard]] bool              is_invalid() const noexcept { return !is_valid(); }
  [[nodiscard]] bool              is_class() const noexcept { return context_ == Handle_context::Class; }
  [[nodiscard]] bool              is_flat() const noexcept { return context_ == Handle_context::Flat; }
  [[nodiscard]] bool              is_hier() const noexcept { return context_ == Handle_context::Hier; }
  [[nodiscard]] Handle_context    get_context() const noexcept { return context_; }
  [[nodiscard]] Gid               get_root_gid() const noexcept;
  [[nodiscard]] Gid               get_current_gid() const noexcept;
  [[nodiscard]] Tid               get_hier_tid() const noexcept { return hier_tid_; }
  [[nodiscard]] Tree_pos          get_hier_pos() const noexcept { return hier_pos_; }

  // Opaque, hashable keys for use in user-owned maps. See hhds/index.hpp for
  // semantics. Prefer these over using Pin_class directly as a map key.
  [[nodiscard]] Class_index get_class_index() const noexcept { return Class_index{pin_pid}; }
  [[nodiscard]] Flat_index  get_flat_index() const noexcept {
    assert(graph_ != nullptr && "get_flat_index: pin is not attached to a graph");
    return Flat_index{get_current_gid(), pin_pid};
  }
  [[nodiscard]] Hier_index get_hier_index() const noexcept {
    assert(context_ == Handle_context::Hier && "get_hier_index: requires hier traversal context");
    return Hier_index{hier_pos_, pin_pid};
  }

  void                                  connect_driver(Pin_class driver_pin) const;
  void                                  connect_sink(Pin_class sink_pin) const;
  void                                  del_sink(Pin_class driver_pin) const;
  void                                  del_sink() const;
  void                                  del_driver() const;
  void                                  del_pin() const;
  [[nodiscard]] std::vector<Edge_class> out_edges() const;
  [[nodiscard]] std::vector<Edge_class> inp_edges() const;

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
  Graph*                            graph_       = nullptr;
  Nid                               raw_nid      = 0;
  Port_id                           port_id      = 0;
  Pid                               pin_pid      = 0;
  Handle_context                    context_     = Handle_context::Class;
  Gid                               root_gid_    = Gid_invalid;
  Gid                               current_gid_ = Gid_invalid;
  std::shared_ptr<std::vector<Gid>> hier_gids_;
  Tid                               hier_tid_ = INVALID;
  Tree_pos                          hier_pos_ = INVALID;

  friend class Graph;
  friend class GraphLibrary;
  friend class Node_class;
  friend void inherit_pin_context(Pin_class& pin, const Node_class& node);
};

class Node_class {
public:
  using Context = Handle_context;

  Node_class() = default;
  Node_class(Graph* graph_value, Nid raw_nid_value) : graph_(graph_value), raw_nid(raw_nid_value) {}
  Node_class(Graph* graph_value, Gid root_gid_value, Gid current_gid_value, Nid raw_nid_value)
      : graph_(graph_value)
      , raw_nid(raw_nid_value)
      , context_(Context::Flat)
      , root_gid_(root_gid_value)
      , current_gid_(current_gid_value) {}
  Node_class(Graph* graph_value, Tid hier_tid_value, std::shared_ptr<std::vector<Gid>> hier_gids_value, Tree_pos hier_pos_value,
             Nid raw_nid_value)
      : graph_(graph_value)
      , raw_nid(raw_nid_value)
      , context_(Context::Hier)
      , hier_gids_(std::move(hier_gids_value))
      , hier_tid_(hier_tid_value)
      , hier_pos_(hier_pos_value) {}
  explicit Node_class(Nid raw_nid_value) : raw_nid(raw_nid_value) {}

  [[nodiscard]] constexpr Port_id get_port_id() const noexcept { return 0; }
  [[nodiscard]] constexpr Nid     get_debug_nid() const noexcept { return raw_nid; }
  [[nodiscard]] Graph*            get_graph() const noexcept { return graph_; }
  [[nodiscard]] bool              is_valid() const noexcept;
  [[nodiscard]] bool              is_invalid() const noexcept { return !is_valid(); }
  [[nodiscard]] bool              is_class() const noexcept { return context_ == Context::Class; }
  [[nodiscard]] bool              is_flat() const noexcept { return context_ == Context::Flat; }
  [[nodiscard]] bool              is_hier() const noexcept { return context_ == Context::Hier; }
  [[nodiscard]] Context           get_context() const noexcept { return context_; }
  [[nodiscard]] Gid               get_root_gid() const noexcept;
  [[nodiscard]] Gid               get_current_gid() const noexcept;
  [[nodiscard]] Tid               get_hier_tid() const noexcept { return hier_tid_; }
  [[nodiscard]] Tree_pos          get_hier_pos() const noexcept { return hier_pos_; }

  // Opaque, hashable keys for use in user-owned maps. See hhds/index.hpp for
  // semantics. Prefer these over using Node_class directly as a map key.
  [[nodiscard]] Class_index get_class_index() const noexcept { return Class_index{raw_nid}; }
  [[nodiscard]] Flat_index  get_flat_index() const noexcept {
    assert(graph_ != nullptr && "get_flat_index: node is not attached to a graph");
    return Flat_index{get_current_gid(), raw_nid};
  }
  [[nodiscard]] Hier_index get_hier_index() const noexcept {
    assert(context_ == Context::Hier && "get_hier_index: requires hier traversal context");
    return Hier_index{hier_pos_, raw_nid};
  }

  void                                  set_subnode(const std::shared_ptr<GraphIO>& graphio) const;
  void                                  set_type(Type type) const;
  [[nodiscard]] Pin_class               create_driver_pin() const;
  [[nodiscard]] Pin_class               create_driver_pin(Port_id port_id) const;
  [[nodiscard]] Pin_class               create_driver_pin(std::string_view name) const;
  [[nodiscard]] Pin_class               create_sink_pin() const;
  [[nodiscard]] Pin_class               create_sink_pin(Port_id port_id) const;
  [[nodiscard]] Pin_class               create_sink_pin(std::string_view name) const;
  [[nodiscard]] Pin_class               get_driver_pin(Port_id port_id) const;
  [[nodiscard]] Pin_class               get_driver_pin(std::string_view name) const;
  [[nodiscard]] Pin_class               get_sink_pin(Port_id port_id) const;
  [[nodiscard]] Pin_class               get_sink_pin(std::string_view name) const;
  void                                  del_node() const;
  [[nodiscard]] std::vector<Edge_class> out_edges() const;
  [[nodiscard]] std::vector<Edge_class> inp_edges() const;
  [[nodiscard]] std::vector<Pin_class>  out_pins() const;
  [[nodiscard]] std::vector<Pin_class>  inp_pins() const;

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
  Graph*                            graph_       = nullptr;
  Nid                               raw_nid      = 0;
  Context                           context_     = Context::Class;
  Gid                               root_gid_    = Gid_invalid;
  Gid                               current_gid_ = Gid_invalid;
  std::shared_ptr<std::vector<Gid>> hier_gids_;
  Tid                               hier_tid_ = INVALID;
  Tree_pos                          hier_pos_ = INVALID;

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

using Node = Node_class;
using Pin  = Pin_class;

class GraphLibrary;

class Graph : public Attr_host {
  class __attribute__((packed)) PinEntry {
    friend class Graph;

  public:
    PinEntry();
    PinEntry(Nid master_nid_value, Port_id port_id_value);

    [[nodiscard]] Nid      get_master_nid() const;
    [[nodiscard]] Port_id  get_port_id() const;
    auto                   add_edge(Pid self_id, Pid other_id, OverflowPool& pool) -> bool;
    auto                   delete_edge(Pid self_id, Pid other_id, OverflowPool& pool) -> bool;
    [[nodiscard]] bool     has_edges() const;
    [[nodiscard]] Pid      get_next_pin_id() const;
    void                   set_next_pin_id(Pid id);
    [[nodiscard]] bool     check_overflow() const { return use_overflow; }
    [[nodiscard]] uint32_t get_overflow_idx() const { return sedges_.overflow_idx; }

    static constexpr size_t MAX_EDGES = 8;

    class EdgeRange {
    public:
      using iterator = typename ankerl::unordered_dense::set<Vid>::const_iterator;
      EdgeRange(const PinEntry* pin, Pid pid, const OverflowVec& overflow) noexcept;
      EdgeRange(EdgeRange&& o) noexcept;
      ~EdgeRange() noexcept;

      EdgeRange(const EdgeRange&)            = delete;
      EdgeRange& operator=(const EdgeRange&) = delete;

      iterator begin() const noexcept { return set_->begin(); }
      iterator end() const noexcept { return set_->end(); }

    private:
      const PinEntry*                    pin_;
      ankerl::unordered_dense::set<Vid>* set_;
      bool                               own_;

      static ankerl::unordered_dense::set<Vid>* acquire_set() noexcept;
      static void                               release_set(ankerl::unordered_dense::set<Vid>*) noexcept;
      static void                               populate_set(const PinEntry*, ankerl::unordered_dense::set<Vid>&, Pid) noexcept;
    };
    [[nodiscard]] auto get_edges(Pid pid, const OverflowVec& overflow) const noexcept -> EdgeRange;

  private:
    auto overflow_handling(Pid self_id, Vid other_id, OverflowPool& pool) -> bool;

    Nid     master_nid : Nid_bits;   // 42 bits
    Port_id port_id : Port_bits;     // 22 bits    => 64 bits (8 bytes)
    Pid     next_pin_id : Nid_bits;  // 42 bits
    Nid     ledge0 : Nid_bits;       // 42 bits to too far node/pin (does not fit in sedge) => 64 bits (8 bytes)
    Nid     ledge1 : Nid_bits;       // 42 bits to too far node/pin (does not fit in sedge) => 64 bits (8 bytes)
    uint8_t use_overflow : 1;        // 1 bit      => 64 bits (8 bytes)

    union {
      int64_t  sedges;        // 48 bits (when use_overflow == 0)
      uint32_t overflow_idx;  // index into Graph::overflow_sets_ (when use_overflow == 1)
    } sedges_;                // Total: 8 bytes
  };

  static_assert(sizeof(PinEntry) == 32, "PinEntry size mismatch");

  class __attribute__((packed)) NodeEntry {
    friend class Graph;

  public:
    NodeEntry();
    explicit NodeEntry(Nid nid_value);

    [[nodiscard]] Nid      get_nid() const;
    [[nodiscard]] Type     get_type() const;
    void                   set_type(Type t);
    [[nodiscard]] Pid      get_next_pin_id() const;
    void                   set_next_pin_id(Pid id);
    [[nodiscard]] bool     has_edges(const OverflowVec& overflow) const;
    auto                   add_edge(Pid self_id, Pid other_id, OverflowPool& pool) -> bool;
    auto                   delete_edge(Pid self_id, Pid other_id, OverflowPool& pool) -> bool;
    [[nodiscard]] bool     check_overflow() const { return use_overflow; }
    [[nodiscard]] uint32_t get_overflow_idx() const { return sedges_.overflow_idx; }

    void               set_subnode(Gid gid, OverflowPool& pool);
    [[nodiscard]] Gid  get_subnode() const noexcept;
    [[nodiscard]] bool has_subnode() const noexcept;

    static constexpr size_t MAX_EDGES = 8;

    class EdgeRange {
    public:
      using iterator = typename ankerl::unordered_dense::set<Vid>::const_iterator;
      EdgeRange(const NodeEntry* node, Nid nid, const OverflowVec& overflow) noexcept;
      EdgeRange(EdgeRange&& o) noexcept;
      ~EdgeRange() noexcept;

      EdgeRange(const EdgeRange&)            = delete;
      EdgeRange& operator=(const EdgeRange&) = delete;

      iterator begin() const noexcept { return set_->begin(); }
      iterator end() const noexcept { return set_->end(); }

    private:
      const NodeEntry*                          node_;
      ankerl::unordered_dense::set<Vid>*        set_;
      bool                                      own_;
      static ankerl::unordered_dense::set<Vid>* acquire_set() noexcept;
      static void                               release_set(ankerl::unordered_dense::set<Vid>*) noexcept;
      static void                               populate_set(const NodeEntry*, ankerl::unordered_dense::set<Vid>&, Nid) noexcept;
    };

    [[nodiscard]] auto get_edges(Nid nid, const OverflowVec& overflow) const noexcept -> EdgeRange;

  private:
    void clear_node();
    auto overflow_handling(Nid self_id, Vid other_id, OverflowPool& pool) -> bool;

    Nid     nid : Nid_bits;
    Type    type : 16;
    Pid     next_pin_id : Nid_bits;
    Nid     ledge0 : Nid_bits;
    Nid     ledge1 : Nid_bits;
    uint8_t use_overflow : 1;
    uint8_t padding : 7;
    union {
      int64_t  sedges;        // 48 bits (when use_overflow == 0)
      uint32_t overflow_idx;  // index into Graph::overflow_sets_ (when use_overflow == 1)
    } sedges_;                // Total: 8 bytes
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
  [[nodiscard]] Pin_class                get_input_pin(std::string_view name) const;
  [[nodiscard]] Pin_class                get_output_pin(std::string_view name) const;

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
    return Node_class(const_cast<Graph*>(this), INVALID, nullptr, idx.hier_pos, idx.value);
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

  // Built-in nodes reserved by graph storage initialization.
  static constexpr Nid INPUT_NODE  = (static_cast<Nid>(1) << 2);
  static constexpr Nid OUTPUT_NODE = (static_cast<Nid>(2) << 2);

  [[nodiscard]] auto fast_class() const -> std::span<const Node>;
  [[nodiscard]] auto forward_class() const -> std::span<const Node>;
  [[nodiscard]] auto fast_flat() const -> std::span<const Node>;
  [[nodiscard]] auto forward_flat() const -> std::span<const Node>;
  [[nodiscard]] auto fast_hier() const -> std::span<const Node>;
  [[nodiscard]] auto forward_hier() const -> std::span<const Node>;
  void               display_graph() const;
  void               display_next_pin_of_node() const;

  void                      print(std::ostream& os) const;
  [[nodiscard]] std::string print() const;

private:
  class FastIterator {
  public:
    FastIterator(Nid node_id_value, Gid top_graph_value, Gid curr_graph_value, uint32_t tree_node_num_value) noexcept
        : node_id(node_id_value), top_graph(top_graph_value), curr_graph(curr_graph_value), tree_node_num(tree_node_num_value) {}

    [[nodiscard]] Nid      get_debug_nid() const noexcept { return node_id; }
    [[nodiscard]] uint32_t get_debug_hier_pos() const noexcept { return tree_node_num; }

  private:
    Nid      node_id;        // encoded nid vid: (nid << 2) | 0
    Gid      top_graph;      // root graph of traversal
    Gid      curr_graph;     // graph that owns node_id
    uint32_t tree_node_num;  // DFS tree-node id (root graph == 1)

    friend class Graph;
  };

  void                       attr_note_modified() noexcept override { dirty_ = true; }
  [[nodiscard]] OverflowPool get_overflow_pool() { return {overflow_sets_, overflow_free_}; }
  void                       assert_accessible() const noexcept;
  void                       assert_node_exists(const Node_class& node) const noexcept;
  void                       assert_pin_exists(const Pin_class& pin) const noexcept;
  [[nodiscard]] bool         is_node_valid(Nid nid) const noexcept;
  [[nodiscard]] bool         is_pin_valid(Pid pid) const noexcept;
  [[nodiscard]] auto         ref_node(Nid id) const -> NodeEntry*;
  [[nodiscard]] auto         ref_pin(Pid id) const -> PinEntry*;
  void                       invalidate_from_library() noexcept;
  void                       release_storage() noexcept;
  void                       clear_graph();
  // Binary persistence — saves/loads body data (node_table, pin_table, overflow sets).
  // dir_path is the graph-specific directory (e.g., "db/graph_1/").
  void              save_body(const std::string& dir_path) const;
  void              load_body(const std::string& dir_path);
  void              delete_node(Nid nid);
  [[nodiscard]] Pid materialize_declared_io_pin(std::string_view name, Port_id port_id, Nid owner_nid,
                                                ankerl::unordered_dense::map<std::string, Pid>& pins_by_name);
  void              erase_declared_io_pin(std::string_view name, ankerl::unordered_dense::map<std::string, Pid>& pins_by_name);
  void              delete_pin(Pid pin_pid);
  [[nodiscard]] Pin_class        create_pin(Node_class node, Port_id port_id);
  [[nodiscard]] Pid              create_pin(Nid nid, Port_id port_id);
  [[nodiscard]] Pin_class        find_pin(Node_class node, Port_id port_id, bool driver) const;
  [[nodiscard]] Pin_class        find_or_create_pin(Node_class node, Port_id port_id);
  [[nodiscard]] Port_id          resolve_driver_port(Node_class node, std::string_view name) const;
  [[nodiscard]] Port_id          resolve_sink_port(Node_class node, std::string_view name) const;
  [[nodiscard]] std::string_view pin_name(Pin_class pin) const;
  void                           set_subnode(Node_class node, Gid gid);
  void                           set_subnode(Nid nid, Gid gid);
  void                           add_edge(Pid driver_id, Pid sink_id);
  void add_edge(Pin_class driver_pin, Pin_class sink_pin) { add_edge(driver_pin.get_debug_pid(), sink_pin.get_debug_pid()); }
  void del_edge(Pin_class driver_pin, Pin_class sink_pin);
  [[nodiscard]] std::vector<Edge_class>   out_edges(Node_class node);
  [[nodiscard]] std::vector<Edge_class>   inp_edges(Node_class node);
  [[nodiscard]] std::vector<Edge_class>   out_edges(Pin_class pin);
  [[nodiscard]] std::vector<Edge_class>   inp_edges(Pin_class pin);
  [[nodiscard]] std::vector<Pin_class>    get_pins(Node_class node);
  [[nodiscard]] std::vector<Pin_class>    get_driver_pins(Node_class node);
  [[nodiscard]] std::vector<Pin_class>    get_sink_pins(Node_class node);
  void                                    del_edge_int(Vid driver_id, Vid sink_id);
  void                                    add_edge_int(Pid self_id, Pid other_id);
  void                                    set_next_pin(Nid nid, Pid next_pin);
  [[nodiscard]] Pin_class                 make_pin_class(Pid pin_pid) const;
  void                                    bind_library(const GraphLibrary* owner, Gid self_gid) noexcept;
  void                                    set_name(std::string_view name) { name_ = name; }
  void                                    invalidate_traversal_caches() noexcept;
  void                                    rebuild_fast_class_cache() const;
  void                                    rebuild_fast_flat_cache() const;
  void                                    rebuild_fast_hier_cache() const;
  void                                    rebuild_forward_class_cache() const;
  void                                    rebuild_forward_flat_cache() const;
  void                                    rebuild_forward_hier_cache() const;
  [[nodiscard]] std::vector<FastIterator> fast_iter(bool hierarchy, Gid top_graph = 0, uint32_t tree_node_num = 0) const;
  void fast_iter_impl(bool hierarchy, Gid top_graph, uint32_t tree_node_num, uint32_t& next_tree_node_num,
                      ankerl::unordered_dense::set<Gid>& active_graphs, std::vector<FastIterator>& out) const;
  void fast_hier_impl(std::shared_ptr<Tree> hier_tree, Tid hier_tid, std::shared_ptr<std::vector<Gid>> hier_gids, Tree_pos hier_pos,
                      ankerl::unordered_dense::set<Gid>& active_graphs, std::vector<Node>& out) const;
  void forward_flat_impl(Gid top_graph, ankerl::unordered_dense::set<Gid>& active_graphs, std::vector<Node>& out) const;
  void forward_hier_impl(std::shared_ptr<Tree> hier_tree, Tid hier_tid, std::shared_ptr<std::vector<Gid>> hier_gids,
                         Tree_pos hier_pos, ankerl::unordered_dense::set<Gid>& active_graphs, std::vector<Node>& out) const;

  std::vector<NodeEntry>                         node_table;
  std::vector<PinEntry>                          pin_table;
  OverflowVec                                    overflow_sets_;
  std::vector<uint32_t>                          overflow_free_;
  mutable std::vector<Node>                      fast_class_cache_;
  mutable std::vector<Node>                      fast_flat_cache_;
  mutable std::vector<Node>                      fast_hier_cache_;
  mutable std::vector<Node>                      forward_class_cache_;
  mutable std::vector<Node>                      forward_flat_cache_;
  mutable std::vector<Node>                      forward_hier_cache_;
  mutable std::shared_ptr<Tree>                  fast_hier_tree_cache_;
  mutable std::shared_ptr<std::vector<Gid>>      fast_hier_gid_cache_;
  mutable std::shared_ptr<Tree>                  forward_hier_tree_cache_;
  mutable std::shared_ptr<std::vector<Gid>>      forward_hier_gid_cache_;
  mutable bool                                   fast_class_cache_valid_    = false;
  mutable bool                                   fast_hier_cache_valid_     = false;
  mutable bool                                   forward_class_cache_valid_ = false;
  mutable bool                                   forward_flat_cache_valid_  = false;
  mutable bool                                   forward_hier_cache_valid_  = false;
  mutable uint64_t                               fast_hier_cache_epoch_     = 0;
  mutable uint64_t                               forward_flat_cache_epoch_  = 0;
  mutable uint64_t                               forward_hier_cache_epoch_  = 0;
  ankerl::unordered_dense::map<std::string, Pid> input_pins_;
  ankerl::unordered_dense::map<std::string, Pid> output_pins_;
  const GraphLibrary*                            owner_lib_ = nullptr;
  std::weak_ptr<GraphIO>                         graphio_owner_;
  Gid                                            self_gid_ = Gid_invalid;
  bool                                           deleted_  = false;
  mutable bool                                   dirty_    = true;
  std::string                                    name_;

  friend class Node_class;
  friend class Pin_class;
  friend class Edge_class;
  friend class GraphIO;
  friend class GraphLibrary;
};

class GraphIO : public std::enable_shared_from_this<GraphIO> {
private:
  // GraphIO owns declared IO-pin metadata. Concrete Pid values only exist once a Graph body is materialized.
  enum class IoDirection : uint8_t { Input, Output };

  struct DeclaredIoPin {
    std::string name;
    Port_id     port_id   = 0;
    bool        loop_last = false;
  };

  struct DeclaredIoPinRef {
    IoDirection direction = IoDirection::Input;
    size_t      index     = 0;
  };

  GraphLibrary* owner_lib_ = nullptr;
  Gid           gid_       = Gid_invalid;
  std::string   name_;

  std::vector<DeclaredIoPin>                                  input_pin_decls_;
  std::vector<DeclaredIoPin>                                  output_pin_decls_;
  ankerl::unordered_dense::map<std::string, DeclaredIoPinRef> declared_io_pins_;

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
  void                                       add_input(std::string_view name, Port_id port_id, bool loop_last = false);
  void                                       add_output(std::string_view name, Port_id port_id, bool loop_last = false);
  void                                       delete_input(std::string_view name);
  void                                       delete_output(std::string_view name);
  void                                       clear();
  [[nodiscard]] bool                         has_input(std::string_view name) const;
  [[nodiscard]] bool                         has_output(std::string_view name) const;
  [[nodiscard]] bool                         is_loop_last(std::string_view name) const;
  [[nodiscard]] Port_id                      get_input_port_id(std::string_view name) const;
  [[nodiscard]] Port_id                      get_output_port_id(std::string_view name) const;

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

  GraphLibrary() {
    // reserve slot 0 for invalid_id
    graph_ios_.push_back(nullptr);
    graphs_.push_back(nullptr);
  }

  ~GraphLibrary() {
    for (auto& graph : graphs_) {
      if (graph) {
        graph->invalidate_from_library();
      }
    }
  }

  [[nodiscard]] std::shared_ptr<GraphIO> create_io(std::string_view name) {
    assert(!name.empty() && "create_io: name is required");
    const auto it = deleted_name_to_id_.find(std::string(name));
    if (it != deleted_name_to_id_.end()) {
      const auto reused_gid = it->second;
      deleted_name_to_id_.erase(it);
      return create_io_impl(reused_gid, name);
    }
    return create_io_impl(static_cast<Gid>(graph_ios_.size()), name);
  }

  [[nodiscard]] std::shared_ptr<GraphIO> find_io(std::string_view name) {
    if (name.empty()) {
      return {};
    }

    const auto it = graph_name_to_id_.find(std::string(name));
    if (it == graph_name_to_id_.end()) {
      return {};
    }

    const size_t idx = static_cast<size_t>(it->second);
    if (idx >= graph_ios_.size()) {
      return {};
    }

    return graph_ios_[idx];
  }

  [[nodiscard]] std::shared_ptr<const GraphIO> find_io(std::string_view name) const {
    if (name.empty()) {
      return {};
    }

    const auto it = graph_name_to_id_.find(std::string(name));
    if (it == graph_name_to_id_.end()) {
      return {};
    }

    const size_t idx = static_cast<size_t>(it->second);
    if (idx >= graph_ios_.size()) {
      return {};
    }

    return graph_ios_[idx];
  }

  [[nodiscard]] bool has_graph(Gid id) const noexcept {
    const size_t idx = static_cast<size_t>(id);
    return idx < graphs_.size() && graphs_[idx] != nullptr && !graphs_[idx]->deleted_;
  }

  [[nodiscard]] std::shared_ptr<Graph> get_graph(Gid id) {
    assert(has_graph(id));
    return graphs_[static_cast<size_t>(id)];
  }

  [[nodiscard]] std::shared_ptr<const Graph> get_graph(Gid id) const {
    assert(has_graph(id));
    return graphs_[static_cast<size_t>(id)];
  }

  [[nodiscard]] std::shared_ptr<Graph> find_graph(std::string_view name) {
    auto gio = find_io(name);
    if (!gio) {
      return {};
    }
    return gio->get_graph();
  }

  [[nodiscard]] std::shared_ptr<const Graph> find_graph(std::string_view name) const {
    auto gio = find_io(name);
    if (!gio) {
      return {};
    }
    return gio->get_graph();
  }

  // Library-wide reverse lookup for an opaque Flat_index key. The gid selects
  // the graph body; the index's raw value is the Nid / Pid. Returns an empty
  // handle if the graph is tombstone-deleted or the raw id is invalid for
  // that body.
  [[nodiscard]] Node_class get_node(Flat_index idx) {
    if (!has_graph(idx.gid)) {
      return Node_class();
    }
    auto graph = get_graph(idx.gid);
    if (!graph->is_node_valid(idx.value)) {
      return Node_class();
    }
    return Node_class(graph.get(), idx.gid, idx.gid, idx.value);
  }
  [[nodiscard]] Pin_class get_pin(Flat_index idx) {
    if (!has_graph(idx.gid)) {
      return Pin_class();
    }
    auto graph = get_graph(idx.gid);
    if (!graph->is_pin_valid(idx.value)) {
      return Pin_class();
    }
    Pin_class pin       = graph->make_pin_class(idx.value);
    pin.context_        = Handle_context::Flat;
    pin.root_gid_       = idx.gid;
    pin.current_gid_    = idx.gid;
    return pin;
  }

  [[nodiscard]] uint64_t mutation_epoch() const noexcept { return mutation_epoch_; }

  // Tombstone-delete (IDs are not reused).
  void delete_graph(Gid id) noexcept {
    if (static_cast<size_t>(id) >= graphs_.size()) {
      return;
    }
    auto& slot = graphs_[static_cast<size_t>(id)];
    if (slot && !slot->deleted_) {
      slot->invalidate_from_library();
      slot.reset();
      --live_count_;
      note_graph_mutation();
    }
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

    const size_t idx = static_cast<size_t>(graphio->get_gid());
    if (idx >= graph_ios_.size() || graph_ios_[idx] != graphio) {
      return;
    }

    const auto gid  = graphio->get_gid();
    const auto name = std::string(graphio->get_name());
    delete_graph(gid);
    if (!name.empty()) {
      graph_name_to_id_.erase(name);
      deleted_name_to_id_[name] = gid;
    }
    graphio->invalidate_from_library();
    graph_ios_[idx].reset();
    note_graph_mutation();
  }

  void delete_graphio(std::string_view name) noexcept {
    auto gio = find_io(name);
    if (!gio) {
      return;
    }
    delete_graphio(gio);
  }

  // Slots (including tombstones).
  [[nodiscard]] size_t capacity() const noexcept { return graphs_.size(); }

  // Count of live graphs.
  [[nodiscard]] Gid live_count() const noexcept { return live_count_; }

  // Persistence — saves all declarations (text) and bodies (binary).
  // db_path is the root directory (e.g., "my_db/").
  void save(const std::string& db_path) const;
  void load(const std::string& db_path);

private:
  [[nodiscard]] std::shared_ptr<GraphIO> create_io_impl(Gid id, std::string_view name) {
    assert(id != invalid_id && "create_io: graph id 0 is reserved");
    assert(!name.empty() && "create_io: name is required");
    assert_name_available(name);

    const size_t idx = static_cast<size_t>(id);
    if (idx < graph_ios_.size()) {
      assert(graph_ios_[idx] == nullptr && "create_io: explicit id already exists or is reserved");
    }

    std::shared_ptr<GraphIO> graphio = std::shared_ptr<GraphIO>(new GraphIO(this, id, std::string(name)));

    if (idx < graph_ios_.size()) {
      graph_ios_[idx] = graphio;
      if (idx >= graphs_.size()) {
        graphs_.resize(idx + 1);
      }
    } else if (idx == graph_ios_.size()) {
      graph_ios_.push_back(graphio);
      if (idx >= graphs_.size()) {
        graphs_.push_back(nullptr);
      }
    } else {
      graph_ios_.resize(idx + 1);
      graph_ios_[idx] = graphio;
      graphs_.resize(idx + 1);
    }

    graph_name_to_id_.emplace(std::string(name), id);
    note_graph_mutation();
    return graphio;
  }

  [[nodiscard]] std::shared_ptr<Graph> create_graph_body(const std::shared_ptr<GraphIO>& graphio) {
    assert(graphio != nullptr && "create_graph_body: null GraphIO");

    const size_t idx = static_cast<size_t>(graphio->get_gid());
    assert(idx < graph_ios_.size() && graph_ios_[idx] == graphio && "create_graph_body: GraphIO is not owned by this library");

    if (idx < graphs_.size() && graphs_[idx] != nullptr && !graphs_[idx]->deleted_) {
      return graphs_[idx];
    }

    std::shared_ptr<Graph> graph = std::make_shared<Graph>();
    graph->bind_library(this, graphio->get_gid());
    graph->set_name(graphio->get_name());
    graph->graphio_owner_ = graphio;
    for (const auto& input : graphio->input_pin_decls_) {
      (void)graph->materialize_declared_io_pin(input.name, input.port_id, Graph::INPUT_NODE, graph->input_pins_);
    }
    for (const auto& output : graphio->output_pin_decls_) {
      (void)graph->materialize_declared_io_pin(output.name, output.port_id, Graph::OUTPUT_NODE, graph->output_pins_);
    }

    if (idx >= graphs_.size()) {
      graphs_.resize(idx + 1);
    }
    graphs_[idx] = graph;

    ++live_count_;
    note_graph_mutation();
    return graph;
  }

  void assert_name_available(std::string_view name) const noexcept {
    if (name.empty()) {
      return;
    }

    const auto it = graph_name_to_id_.find(std::string(name));
    assert(it == graph_name_to_id_.end() && "create_graph: graph name already exists");
  }

  void note_graph_mutation() const noexcept { ++mutation_epoch_; }

  std::vector<std::shared_ptr<GraphIO>>          graph_ios_;
  std::vector<std::shared_ptr<Graph>>            graphs_;
  ankerl::unordered_dense::map<std::string, Gid> graph_name_to_id_;
  ankerl::unordered_dense::map<std::string, Gid> deleted_name_to_id_;
  // count of live graphs
  Gid              live_count_     = 0;
  mutable uint64_t mutation_epoch_ = 1;

  friend class Graph;
  friend class GraphIO;
};

inline std::shared_ptr<Graph> GraphIO::get_graph() {
  if (owner_lib_ == nullptr) {
    return {};
  }
  const size_t idx = static_cast<size_t>(gid_);
  if (idx >= owner_lib_->graphs_.size()) {
    return {};
  }
  const auto& graph = owner_lib_->graphs_[idx];
  if (!graph || graph->deleted_) {
    return {};
  }
  return graph;
}

inline std::shared_ptr<const Graph> GraphIO::get_graph() const {
  if (owner_lib_ == nullptr) {
    return {};
  }
  const size_t idx = static_cast<size_t>(gid_);
  if (idx >= owner_lib_->graphs_.size()) {
    return {};
  }
  const auto& graph = owner_lib_->graphs_[idx];
  if (!graph || graph->deleted_) {
    return {};
  }
  return graph;
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

inline void GraphIO::add_input(std::string_view name, Port_id port_id, bool loop_last) {
  assert(owner_lib_ != nullptr && "add_input: GraphIO is no longer attached to a library");
  assert(!name.empty() && "add_input: name is required");

  const std::string key(name);
  assert(declared_io_pins_.find(key) == declared_io_pins_.end() && "add_input: input pin name already exists");
  input_pin_decls_.push_back(DeclaredIoPin{key, port_id, loop_last});
  declared_io_pins_.emplace(input_pin_decls_.back().name, DeclaredIoPinRef{IoDirection::Input, input_pin_decls_.size() - 1});

  if (auto graph = get_graph()) {
    (void)graph->materialize_declared_io_pin(name, port_id, Graph::INPUT_NODE, graph->input_pins_);
  } else if (owner_lib_ != nullptr) {
    owner_lib_->note_graph_mutation();
  }
}

inline void GraphIO::add_output(std::string_view name, Port_id port_id, bool loop_last) {
  assert(owner_lib_ != nullptr && "add_output: GraphIO is no longer attached to a library");
  assert(!name.empty() && "add_output: name is required");

  const std::string key(name);
  assert(declared_io_pins_.find(key) == declared_io_pins_.end() && "add_output: output pin name already exists");
  output_pin_decls_.push_back(DeclaredIoPin{key, port_id, loop_last});
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

inline bool GraphIO::has_input(std::string_view name) const {
  const auto it = declared_io_pins_.find(std::string(name));
  return it != declared_io_pins_.end() && it->second.direction == IoDirection::Input;
}

inline bool GraphIO::has_output(std::string_view name) const {
  const auto it = declared_io_pins_.find(std::string(name));
  return it != declared_io_pins_.end() && it->second.direction == IoDirection::Output;
}

inline bool GraphIO::is_loop_last(std::string_view name) const {
  const auto it = declared_io_pins_.find(std::string(name));
  assert(it != declared_io_pins_.end() && "is_loop_last: declared pin name not found");
  if (it == declared_io_pins_.end()) {
    return false;
  }

  if (it->second.direction == IoDirection::Input) {
    return input_pin_decls_[it->second.index].loop_last;
  }
  return output_pin_decls_[it->second.index].loop_last;
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

}  // namespace hhds
