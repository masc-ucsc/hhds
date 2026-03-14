#pragma once

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <iterator>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "graph_sizing.hpp"
#include "unordered_dense.hpp"

namespace hhds {

constexpr int NUM_NODES         = 10;
constexpr int NUM_TYPES         = 3;
constexpr int MAX_PINS_PER_NODE = 10;

class __attribute__((packed)) Pin {
  friend class Graph;

public:
  Pin();
  Pin(Nid master_nid_value, Port_id port_id_value);

  [[nodiscard]] Nid     get_master_nid() const;
  [[nodiscard]] Port_id get_port_id() const;
  auto                  add_edge(Pid self_id, Pid other_id) -> bool;
  auto                  delete_edge(Pid self_id, Pid other_id) -> bool;
  [[nodiscard]] bool    has_edges() const;
  [[nodiscard]] Pid     get_next_pin_id() const;
  void                  set_next_pin_id(Pid id);
  [[nodiscard]] bool    check_overflow() const { return use_overflow; }

  static constexpr size_t MAX_EDGES = 8;

  class EdgeRange {
  public:
    using iterator = typename ankerl::unordered_dense::set<Vid>::const_iterator;
    EdgeRange(const Pin* pin, Pid pid) noexcept;
    EdgeRange(EdgeRange&& o) noexcept;
    ~EdgeRange() noexcept;

    // disable copy
    EdgeRange(const EdgeRange&)            = delete;
    EdgeRange& operator=(const EdgeRange&) = delete;

    iterator begin() const noexcept { return set_->begin(); }
    iterator end() const noexcept { return set_->end(); }

  private:
    // Pid                                pid_;
    const Pin*                         pin_;
    ankerl::unordered_dense::set<Vid>* set_;
    bool                               own_;

    static ankerl::unordered_dense::set<Vid>* acquire_set() noexcept;
    static void                               release_set(ankerl::unordered_dense::set<Vid>*) noexcept;
    static void                               populate_set(const Pin*, ankerl::unordered_dense::set<Vid>&, Pid) noexcept;
  };
  [[nodiscard]] auto get_edges(Pid pid) const noexcept -> EdgeRange;  // should be in node

private:
  auto overflow_handling(Pid self_id, Vid other_id) -> bool;

  Nid     master_nid : Nid_bits;   // 42 bits
  Port_id port_id : Port_bits;     // 22 bits    => 64 bits (8 bytes)
  Pid     next_pin_id : Nid_bits;  // 42 bits
  Nid     ledge0 : Nid_bits;       // 42 bits to too far node/pin (does not fit in sedge) => 64 bits (8 bytes)
  Nid     ledge1 : Nid_bits;       // 42 bits to too far node/pin (does not fit in sedge) => 64 bits (8 bytes)
  uint8_t use_overflow : 1;        // 1 bit      => 64 bits (8 bytes)

  // adds upto a total of 191 bits => 24 bytes
  union {
    int64_t                            sedges;  // 48 bits
    ankerl::unordered_dense::set<Vid>* set;     // 8 bytes
  } sedges_;                                    // Total: 8 bytes
  // Total: 32 bytes
};

static_assert(sizeof(Pin) == 32, "Pin size mismatch");

class __attribute__((packed)) Node {
  friend class Graph;

public:
  Node();
  explicit Node(Nid nid_value);

  [[nodiscard]] Nid  get_nid() const;
  [[nodiscard]] Type get_type() const;
  void               set_type(Type t);
  [[nodiscard]] Pid  get_next_pin_id() const;
  void               set_next_pin_id(Pid id);
  [[nodiscard]] bool has_edges() const;
  auto               add_edge(Pid self_id, Pid other_id) -> bool;
  auto               delete_edge(Pid self_id, Pid other_id) -> bool;
  [[nodiscard]] bool check_overflow() const { return use_overflow; }

  // Subgraph link: stored in ledge0 *only when use_overflow == 1* as (gid + 1).
  // Caller must validate gid before calling set_subnode().
  void               set_subnode(Gid gid);
  [[nodiscard]] Gid  get_subnode() const noexcept;
  [[nodiscard]] bool has_subnode() const noexcept;

  static constexpr size_t MAX_EDGES = 8;

  class EdgeRange {
  public:
    using iterator = typename ankerl::unordered_dense::set<Vid>::const_iterator;
    EdgeRange(const Node* node, Nid nid) noexcept;
    EdgeRange(EdgeRange&& o) noexcept;
    ~EdgeRange() noexcept;
    // disable copy
    EdgeRange(const EdgeRange&)            = delete;
    EdgeRange& operator=(const EdgeRange&) = delete;
    iterator   begin() const noexcept { return set_->begin(); }
    iterator   end() const noexcept { return set_->end(); }

  private:
    // Nid                                       nid_;
    const Node*                               node_;
    ankerl::unordered_dense::set<Vid>*        set_;
    bool                                      own_;
    static ankerl::unordered_dense::set<Vid>* acquire_set() noexcept;
    static void                               release_set(ankerl::unordered_dense::set<Vid>*) noexcept;
    static void                               populate_set(const Node*, ankerl::unordered_dense::set<Vid>&, Nid) noexcept;
  };

  [[nodiscard]] auto get_edges(Nid nid) const noexcept -> EdgeRange;

private:
  void clear_node();
  auto overflow_handling(Nid self_id, Vid other_id) -> bool;

  Nid     nid : Nid_bits;
  Type    type : 16;
  Pid     next_pin_id : Nid_bits;
  Nid     ledge0 : Nid_bits;
  Nid     ledge1 : Nid_bits;
  uint8_t use_overflow : 1;
  uint8_t padding : 7;
  union {
    int64_t                            sedges;  // 48 bits
    ankerl::unordered_dense::set<Vid>* set;     // 8 bytes
  } sedges_;                                    // Total: 8 bytes
};
static_assert(sizeof(Node) == 32, "Node size mismatch");

class Graph;
class Node_class;
class Tree;

class Pin_class {
public:
  Pin_class() = default;
  Pin_class(Graph* graph_value, Nid raw_nid_value, Port_id port_id_value, Pid pin_pid_value)
      : graph(graph_value), raw_nid(raw_nid_value & ~static_cast<Nid>(2)), port_id(port_id_value), pin_pid(pin_pid_value) {}
  Pin_class(Nid raw_nid_value, Port_id port_id_value, Pid pin_pid_value)
      : raw_nid(raw_nid_value & ~static_cast<Nid>(2)), port_id(port_id_value), pin_pid(pin_pid_value) {}

  [[nodiscard]] Node_class get_master_node() const;
  [[nodiscard]] Nid        get_raw_nid() const noexcept {
    assert_accessible_handle();
    return raw_nid;
  }
  [[nodiscard]] Pid get_pin_pid() const noexcept {
    assert_accessible_handle();
    return pin_pid;
  }
  [[nodiscard]] Port_id get_port_id() const noexcept {
    assert_accessible_handle();
    return port_id;
  }

  // Interop with existing APIs that still accept raw Pid.
  [[nodiscard]] operator Pid() const noexcept {
    assert_accessible_handle();
    return pin_pid;
  }

  [[nodiscard]] bool operator==(const Pin_class& other) const noexcept { return graph == other.graph && pin_pid == other.pin_pid; }
  [[nodiscard]] bool operator!=(const Pin_class& other) const noexcept { return !(*this == other); }

  template <typename H>
  friend H AbslHashValue(H h, const Pin_class& pin) {
    return H::combine(std::move(h), pin.graph, pin.pin_pid);
  }

private:
  void    assert_accessible_handle() const noexcept;
  Graph*  graph   = nullptr;
  Nid     raw_nid = 0;
  Port_id port_id = 0;
  Pid     pin_pid = 0;

  friend class Graph;
};

class Node_class {
public:
  Node_class() = default;
  Node_class(Graph* graph_value, Nid raw_nid_value) : graph(graph_value), raw_nid(raw_nid_value) {}

  [[nodiscard]] Port_id get_port_id() const noexcept {
    assert_accessible_handle();
    return 0;
  }
  [[nodiscard]] Nid get_raw_nid() const noexcept {
    assert_accessible_handle();
    return raw_nid;
  }
  [[nodiscard]] Pin_class create_pin(Port_id port_id) const;

  // Interop with existing APIs that still accept raw Nid.
  [[nodiscard]] operator Nid() const noexcept {
    assert_accessible_handle();
    return raw_nid;
  }

  [[nodiscard]] bool operator==(const Node_class& other) const noexcept { return graph == other.graph && raw_nid == other.raw_nid; }
  [[nodiscard]] bool operator!=(const Node_class& other) const noexcept { return !(*this == other); }

  template <typename H>
  friend H AbslHashValue(H h, const Node_class& node) {
    return H::combine(std::move(h), node.graph, node.raw_nid);
  }

private:
  void   assert_accessible_handle() const noexcept;
  Graph* graph   = nullptr;
  Nid    raw_nid = 0;

  friend class Graph;
};

class Node_flat {
public:
  Node_flat() = default;
  Node_flat(Gid root_gid_value, Gid current_gid_value, Nid raw_nid_value)
      : root_gid(root_gid_value), current_gid(current_gid_value), raw_nid(raw_nid_value) {}

  [[nodiscard]] Gid     get_root_gid() const noexcept { return root_gid; }
  [[nodiscard]] Gid     get_current_gid() const noexcept { return current_gid; }
  [[nodiscard]] Port_id get_port_id() const noexcept { return 0; }
  [[nodiscard]] Nid     get_raw_nid() const noexcept { return raw_nid; }

  [[nodiscard]] bool operator==(const Node_flat& other) const noexcept {
    return root_gid == other.root_gid && current_gid == other.current_gid && raw_nid == other.raw_nid;
  }
  [[nodiscard]] bool operator!=(const Node_flat& other) const noexcept { return !(*this == other); }

  template <typename H>
  friend H AbslHashValue(H h, const Node_flat& node) {
    return H::combine(std::move(h), node.root_gid, node.current_gid, node.raw_nid);
  }

private:
  Gid root_gid    = Gid_invalid;
  Gid current_gid = Gid_invalid;
  Nid raw_nid     = 0;
};

class Pin_flat {
public:
  Pin_flat() = default;
  Pin_flat(Gid root_gid_value, Gid current_gid_value, Nid raw_nid_value, Port_id port_id_value, Pid pin_pid_value)
      : root_gid(root_gid_value)
      , current_gid(current_gid_value)
      , raw_nid(raw_nid_value & ~static_cast<Nid>(2))
      , port_id(port_id_value)
      , pin_pid(pin_pid_value) {}

  [[nodiscard]] Gid     get_root_gid() const noexcept { return root_gid; }
  [[nodiscard]] Gid     get_current_gid() const noexcept { return current_gid; }
  [[nodiscard]] Nid     get_raw_nid() const noexcept { return raw_nid; }
  [[nodiscard]] Port_id get_port_id() const noexcept { return port_id; }
  [[nodiscard]] Pid     get_pin_pid() const noexcept { return pin_pid; }

  [[nodiscard]] bool operator==(const Pin_flat& other) const noexcept {
    return root_gid == other.root_gid && current_gid == other.current_gid && pin_pid == other.pin_pid;
  }
  [[nodiscard]] bool operator!=(const Pin_flat& other) const noexcept { return !(*this == other); }

  template <typename H>
  friend H AbslHashValue(H h, const Pin_flat& pin) {
    return H::combine(std::move(h), pin.root_gid, pin.current_gid, pin.pin_pid);
  }

private:
  Gid     root_gid    = Gid_invalid;
  Gid     current_gid = Gid_invalid;
  Nid     raw_nid     = 0;
  Port_id port_id     = 0;
  Pid     pin_pid     = 0;
};

class Node_hier {
public:
  Node_hier() = default;
  Node_hier(std::shared_ptr<Tree> hier_ref_value, std::shared_ptr<std::vector<Gid>> hier_gids_value, int64_t hier_pos_value,
            Nid raw_nid_value);

  [[nodiscard]] Gid     get_root_gid() const noexcept;
  [[nodiscard]] Gid     get_current_gid() const noexcept;
  [[nodiscard]] Port_id get_port_id() const noexcept { return 0; }
  [[nodiscard]] Nid     get_raw_nid() const noexcept { return raw_nid; }

  [[nodiscard]] bool operator==(const Node_hier& other) const noexcept {
    return hier_ref.get() == other.hier_ref.get() && hier_pos == other.hier_pos && raw_nid == other.raw_nid;
  }
  [[nodiscard]] bool operator!=(const Node_hier& other) const noexcept { return !(*this == other); }

  template <typename H>
  friend H AbslHashValue(H h, const Node_hier& node) {
    return H::combine(std::move(h), node.hier_ref.get(), node.hier_pos, node.raw_nid);
  }

private:
  std::shared_ptr<Tree>             hier_ref;
  std::shared_ptr<std::vector<Gid>> hier_gids;
  int64_t                           hier_pos = 0;
  Nid                               raw_nid  = 0;
};

class Pin_hier {
public:
  Pin_hier() = default;
  Pin_hier(std::shared_ptr<Tree> hier_ref_value, std::shared_ptr<std::vector<Gid>> hier_gids_value, int64_t hier_pos_value,
           Nid raw_nid_value, Port_id port_id_value,
           Pid pin_pid_value)
      : hier_ref(std::move(hier_ref_value))
      , hier_gids(std::move(hier_gids_value))
      , hier_pos(hier_pos_value)
      , raw_nid(raw_nid_value & ~static_cast<Nid>(2))
      , port_id(port_id_value)
      , pin_pid(pin_pid_value) {}

  [[nodiscard]] Gid     get_root_gid() const noexcept;
  [[nodiscard]] Gid     get_current_gid() const noexcept;
  [[nodiscard]] Nid     get_raw_nid() const noexcept { return raw_nid; }
  [[nodiscard]] Port_id get_port_id() const noexcept { return port_id; }
  [[nodiscard]] Pid     get_pin_pid() const noexcept { return pin_pid; }

  [[nodiscard]] bool operator==(const Pin_hier& other) const noexcept {
    return hier_ref.get() == other.hier_ref.get() && hier_pos == other.hier_pos && pin_pid == other.pin_pid;
  }
  [[nodiscard]] bool operator!=(const Pin_hier& other) const noexcept { return !(*this == other); }

  template <typename H>
  friend H AbslHashValue(H h, const Pin_hier& pin) {
    return H::combine(std::move(h), pin.hier_ref.get(), pin.hier_pos, pin.pin_pid);
  }

private:
  std::shared_ptr<Tree>             hier_ref;
  std::shared_ptr<std::vector<Gid>> hier_gids;
  int64_t                           hier_pos = 0;
  Nid                               raw_nid  = 0;
  Port_id                           port_id  = 0;
  Pid                               pin_pid  = 0;
};

class Edge_class {
public:
  Node_class driver;
  Node_class sink;
  Pin_class  driver_pin;
  Pin_class  sink_pin;

  // edge kind mapping:
  // 1 => n -> n
  // 2 => p -> p
  // 3 => n -> p
  // 4 => p -> n
  uint8_t type : 3;
};

class Edge_flat {
public:
  Pin_flat driver;
  Pin_flat sink;

  [[nodiscard]] bool operator==(const Edge_flat& other) const noexcept { return driver == other.driver && sink == other.sink; }
  [[nodiscard]] bool operator!=(const Edge_flat& other) const noexcept { return !(*this == other); }

  template <typename H>
  friend H AbslHashValue(H h, const Edge_flat& edge) {
    return H::combine(std::move(h), edge.driver, edge.sink);
  }
};

class Edge_hier {
public:
  Pin_hier driver;
  Pin_hier sink;

  [[nodiscard]] bool operator==(const Edge_hier& other) const noexcept { return driver == other.driver && sink == other.sink; }
  [[nodiscard]] bool operator!=(const Edge_hier& other) const noexcept { return !(*this == other); }

  template <typename H>
  friend H AbslHashValue(H h, const Edge_hier& edge) {
    return H::combine(std::move(h), edge.driver, edge.sink);
  }
};

class GraphLibrary;

class Graph {
public:
  Graph();
  // Graphs are owned via handles; copying or moving would break identity and stale-handle checks.
  Graph(const Graph&)            = delete;
  Graph& operator=(const Graph&) = delete;
  Graph(Graph&&)                 = delete;
  Graph& operator=(Graph&&)      = delete;
  void   clear_graph();

  [[nodiscard]] Node_class create_node();
  [[nodiscard]] Pid        create_pin(Nid nid, Port_id port_id);
  [[nodiscard]] Gid        get_gid() const noexcept { return self_gid_; }

  // Built-in nodes created by Graph::clear_graph()
  static constexpr Nid INPUT_NODE  = (static_cast<Nid>(1) << 2);
  static constexpr Nid OUTPUT_NODE = (static_cast<Nid>(2) << 2);

  // add_input(port): creates a pin on default input node (node 1)
  [[nodiscard]] Pid add_input(Port_id port_id) { return create_pin(INPUT_NODE, port_id); }
  // add_output(port): creates a pin on default output node (node 2)
  [[nodiscard]] Pid add_output(Port_id port_id) { return create_pin(OUTPUT_NODE, port_id); }

  [[nodiscard]] auto ref_node(Nid id) const -> Node*;
  [[nodiscard]] auto ref_pin(Pid id) const -> Pin*;

  void add_edge(Node_class driver_node, Node_class sink_node) {
    assert_compatible(driver_node);
    assert_compatible(sink_node);
    add_edge(driver_node.get_raw_nid(), sink_node.get_raw_nid());
  }
  void add_edge(Node_class driver_node, Pin_class sink_pin) {
    assert_compatible(driver_node);
    assert_compatible(sink_pin);
    add_edge(driver_node.get_raw_nid(), sink_pin.get_pin_pid());
  }
  void add_edge(Pin_class driver_pin, Node_class sink_node) {
    assert_compatible(driver_pin);
    assert_compatible(sink_node);
    add_edge(driver_pin.get_pin_pid(), sink_node.get_raw_nid());
  }
  void add_edge(Pin_class driver_pin, Pin_class sink_pin) {
    assert_compatible(driver_pin);
    assert_compatible(sink_pin);
    add_edge(driver_pin.get_pin_pid(), sink_pin.get_pin_pid());
  }
  void set_subnode(Node_class node, Gid gid);
  void set_subnode(Nid nid, Gid gid);

  void                                  add_edge(Pid driver_id, Pid sink_id);
  void                                  del_edge(Node_class node1, Node_class node2);
  void                                  del_edge(Node_class node, Pin_class pin);
  void                                  del_edge(Pin_class pin, Node_class node);
  void                                  del_edge(Pin_class pin1, Pin_class pin2);
  [[nodiscard]] std::vector<Edge_class> out_edges(Node_class node);
  [[nodiscard]] std::vector<Edge_class> inp_edges(Node_class node);
  [[nodiscard]] std::vector<Edge_class> out_edges(Pin_class pin);
  [[nodiscard]] std::vector<Edge_class> inp_edges(Pin_class pin);
  [[nodiscard]] std::vector<Pin_class>  get_pins(Node_class node);
  [[nodiscard]] std::vector<Pin_class>  get_driver_pins(Node_class node);
  [[nodiscard]] std::vector<Pin_class>  get_sink_pins(Node_class node);
  [[nodiscard]] auto                    fast_class() const -> std::span<const Node_class>;
  [[nodiscard]] auto                    forward_class() const -> std::span<const Node_class>;
  [[nodiscard]] auto                    fast_flat() const -> std::span<const Node_flat>;
  [[nodiscard]] auto                    forward_flat() const -> std::span<const Node_flat>;
  [[nodiscard]] auto                    fast_hier() const -> std::span<const Node_hier>;
  [[nodiscard]] auto                    forward_hier() const -> std::span<const Node_hier>;
  void                                  delete_node(Nid nid);
  void                                  display_graph() const;
  void                                  display_next_pin_of_node() const;

  class FastIterator {
  public:
    FastIterator(Nid node_id_value, Gid top_graph_value, Gid curr_graph_value, uint32_t tree_node_num_value) noexcept
        : node_id(node_id_value), top_graph(top_graph_value), curr_graph(curr_graph_value), tree_node_num(tree_node_num_value) {}

    [[nodiscard]] Nid      get_node_id() const noexcept { return node_id; }
    [[nodiscard]] Gid      get_top_graph() const noexcept { return top_graph; }
    [[nodiscard]] Gid      get_curr_graph() const noexcept { return curr_graph; }
    [[nodiscard]] uint32_t get_tree_node_num() const noexcept { return tree_node_num; }

  private:
    Nid      node_id;        // encoded nid vid: (nid << 2) | 0
    Gid      top_graph;      // root graph of traversal
    Gid      curr_graph;     // graph that owns node_id
    uint32_t tree_node_num;  // DFS tree-node id (root graph == 1)
  };

  [[nodiscard]] std::vector<FastIterator> fast_iter(bool hierarchy, Gid top_graph = 0, uint32_t tree_node_num = 0) const;

private:
  void                    assert_accessible() const noexcept;
  void                    assert_compatible(const Node_class& node) const noexcept;
  void                    assert_compatible(const Pin_class& pin) const noexcept;
  void                    invalidate_from_library() noexcept;
  void                    del_edge_int(Vid driver_id, Vid sink_id);
  void                    add_edge_int(Pid self_id, Pid other_id);
  void                    set_next_pin(Nid nid, Pid next_pin);
  [[nodiscard]] Pin_class make_pin_class(Pid pin_pid) const;
  void                    bind_library(const GraphLibrary* owner, Gid self_gid) noexcept;
  void                    invalidate_traversal_caches() noexcept;
  void                    rebuild_fast_class_cache() const;
  void                    rebuild_fast_flat_cache() const;
  void                    rebuild_fast_hier_cache() const;
  void                    rebuild_forward_class_cache() const;
  void                    rebuild_forward_flat_cache() const;
  void                    rebuild_forward_hier_cache() const;
  void                    fast_iter_impl(bool hierarchy, Gid top_graph, uint32_t tree_node_num, uint32_t& next_tree_node_num,
                                         ankerl::unordered_dense::set<Gid>& active_graphs, std::vector<FastIterator>& out) const;
  void fast_hier_impl(std::shared_ptr<Tree> hier_ref, std::shared_ptr<std::vector<Gid>> hier_gids, int64_t hier_pos,
                      ankerl::unordered_dense::set<Gid>& active_graphs, std::vector<Node_hier>& out) const;
  void forward_flat_impl(Gid top_graph, ankerl::unordered_dense::set<Gid>& active_graphs, std::vector<Node_flat>& out) const;
  void forward_hier_impl(std::shared_ptr<Tree> hier_ref, std::shared_ptr<std::vector<Gid>> hier_gids, int64_t hier_pos,
                         ankerl::unordered_dense::set<Gid>& active_graphs, std::vector<Node_hier>& out) const;

  std::vector<Node>                  node_table;
  std::vector<Pin>                   pin_table;
  mutable std::vector<Node_class>    fast_class_cache_;
  mutable std::vector<Node_flat>     fast_flat_cache_;
  mutable std::vector<Node_hier>     fast_hier_cache_;
  mutable std::vector<Node_class>    forward_class_cache_;
  mutable std::vector<Node_flat>     forward_flat_cache_;
  mutable std::vector<Node_hier>     forward_hier_cache_;
  mutable std::shared_ptr<Tree>             fast_hier_tree_cache_;
  mutable std::shared_ptr<std::vector<Gid>> fast_hier_gid_cache_;
  mutable std::shared_ptr<Tree>             forward_hier_tree_cache_;
  mutable std::shared_ptr<std::vector<Gid>> forward_hier_gid_cache_;
  mutable bool                       fast_class_cache_valid_    = false;
  mutable bool                       fast_hier_cache_valid_     = false;
  mutable bool                       forward_class_cache_valid_ = false;
  mutable bool                       forward_flat_cache_valid_  = false;
  mutable bool                       forward_hier_cache_valid_  = false;
  mutable uint64_t                   fast_hier_cache_epoch_     = 0;
  mutable uint64_t                   forward_flat_cache_epoch_  = 0;
  mutable uint64_t                   forward_hier_cache_epoch_  = 0;
  const GraphLibrary*                owner_lib_                 = nullptr;
  Gid                                self_gid_                  = Gid_invalid;
  bool                               deleted_                   = false;

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
    graphs_.push_back(nullptr);
  }

  ~GraphLibrary() {
    for (auto& graph : graphs_) {
      if (graph) {
        graph->invalidate_from_library();
      }
    }
  }

  // Create a new graph and return shared ownership.
  [[nodiscard]] std::shared_ptr<Graph> create_graph() {
    const Gid              id    = static_cast<Gid>(graphs_.size());
    std::shared_ptr<Graph> graph = std::make_shared<Graph>();
    graph->bind_library(this, id);
    graphs_.push_back(graph);
    ++live_count_;
    note_graph_mutation();
    return graph;
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

  [[nodiscard]] uint64_t mutation_epoch() const noexcept { return mutation_epoch_; }

  // Tombstone-delete (IDs are not reused).
  void delete_graph(Gid id) noexcept {
    if (static_cast<size_t>(id) >= graphs_.size()) {
      return;
    }
    auto& slot = graphs_[static_cast<size_t>(id)];
    if (slot && !slot->deleted_) {
      slot->invalidate_from_library();
      --live_count_;
      note_graph_mutation();
    }
  }

  // Slots (including tombstones).
  [[nodiscard]] size_t capacity() const noexcept { return graphs_.size(); }

  // Count of live graphs.
  [[nodiscard]] Gid live_count() const noexcept { return live_count_; }

private:
  void note_graph_mutation() const noexcept { ++mutation_epoch_; }

  std::vector<std::shared_ptr<Graph>> graphs_;
  // count of live graphs
  Gid              live_count_     = 0;
  mutable uint64_t mutation_epoch_ = 1;

  friend class Graph;
};

// Compact-tier conversions: information can be discarded (_hier/_flat -> _class),
// but hierarchy context cannot be reconstructed from _class/_flat alone.
[[nodiscard]] Node_class to_class(const Node_hier& v);
[[nodiscard]] Node_flat  to_flat(const Node_hier& v);
[[nodiscard]] Node_class to_class(const Node_flat& v);
[[nodiscard]] Node_flat  to_flat(const Node_class& v, Gid current_gid, Gid root_gid = Gid_invalid);
Node_hier                to_hier(Node_class) = delete;
Node_hier                to_hier(Node_flat)  = delete;

[[nodiscard]] Pin_class to_class(const Pin_hier& v);
[[nodiscard]] Pin_flat  to_flat(const Pin_hier& v);
[[nodiscard]] Pin_class to_class(const Pin_flat& v);
[[nodiscard]] Pin_flat  to_flat(const Pin_class& v, Gid current_gid, Gid root_gid = Gid_invalid);
Pin_hier                to_hier(Pin_class) = delete;
Pin_hier                to_hier(Pin_flat)  = delete;

[[nodiscard]] Edge_flat  to_flat(const Edge_class& e, Gid current_gid, Gid root_gid = Gid_invalid);
[[nodiscard]] Edge_flat  to_flat(const Edge_hier& e);
[[nodiscard]] Edge_hier  to_hier(const Edge_class& e, std::shared_ptr<Tree> hier_ref, std::shared_ptr<std::vector<Gid>> hier_gids,
                                 int64_t hier_pos);
[[nodiscard]] Edge_class to_class(const Edge_flat& e);
[[nodiscard]] Edge_class to_class(const Edge_hier& e);

}  // namespace hhds
