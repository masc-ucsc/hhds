#pragma once

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <iterator>
#include <memory>
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

class GraphLibrary;

class Graph {
public:
  Graph();
  void clear_graph();

  [[nodiscard]] Nid create_node();
  [[nodiscard]] Pid create_pin(Nid nid, Port_id port_id);

  // Built-in nodes created by Graph::clear_graph()
  static constexpr Nid INPUT_NODE  = (static_cast<Nid>(1) << 2);
  static constexpr Nid OUTPUT_NODE = (static_cast<Nid>(2) << 2);

  // add_input(port): creates a pin on default input node (node 1)
  [[nodiscard]] Pid add_input(Port_id port_id) { return create_pin(INPUT_NODE, port_id); }
  // add_output(port): creates a pin on default output node (node 2)
  [[nodiscard]] Pid add_output(Port_id port_id) { return create_pin(OUTPUT_NODE, port_id); }

  [[nodiscard]] auto ref_node(Nid id) const -> Node*;
  [[nodiscard]] auto ref_pin(Pid id) const -> Pin*;

  void add_edge(Pid driver_id, Pid sink_id);
  void delete_node(Nid nid);
  void display_graph() const;
  void display_next_pin_of_node() const;

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
  void add_edge_int(Pid self_id, Pid other_id);
  void set_next_pin(Nid nid, Pid next_pin);
  void bind_library(const GraphLibrary* owner, Gid self_gid) noexcept;
  void fast_iter_impl(bool hierarchy, Gid top_graph, uint32_t tree_node_num, uint32_t& next_tree_node_num,
                      ankerl::unordered_dense::set<Gid>& active_graphs, std::vector<FastIterator>& out) const;

  std::vector<Node>   node_table;
  std::vector<Pin>    pin_table;
  const GraphLibrary* owner_lib_ = nullptr;
  Gid                 self_gid_  = Gid_invalid;

  friend class GraphLibrary;
};

class GraphLibrary {
public:
  static constexpr Gid invalid_id = Gid_invalid;

  GraphLibrary() {
    // reserve slot 0 for invalid_id
    graphs_.push_back(nullptr);
  }

  // Create a new graph and return its ID.
  [[nodiscard]] Gid create_graph() {
    const Gid id = static_cast<Gid>(graphs_.size());
    graphs_.push_back(std::make_unique<Graph>());
    graphs_.back()->bind_library(this, id);
    ++live_count_;
    return id;
  }

  [[nodiscard]] bool has_graph(Gid id) const noexcept {
    const size_t idx = static_cast<size_t>(id);
    return idx < graphs_.size() && graphs_[idx] != nullptr;
  }

  [[nodiscard]] Graph& get_graph(Gid id) {
    assert(has_graph(id));
    return *graphs_[static_cast<size_t>(id)];
  }

  [[nodiscard]] const Graph& get_graph(Gid id) const {
    assert(has_graph(id));
    return *graphs_[static_cast<size_t>(id)];
  }

  // Tombstone-delete (IDs are not reused).
  void delete_graph(Gid id) noexcept {
    if (static_cast<size_t>(id) >= graphs_.size()) {
      return;
    }
    auto& slot = graphs_[static_cast<size_t>(id)];
    if (slot) {
      slot.reset();
      --live_count_;
    }
  }

  // Slots (including tombstones).
  [[nodiscard]] size_t capacity() const noexcept { return graphs_.size(); }

  // Count of live graphs.
  [[nodiscard]] Gid live_count() const noexcept { return live_count_; }

private:
  std::vector<std::unique_ptr<Graph>> graphs_;
  // count of live graphs
  Gid live_count_ = 0;
};

}  // namespace hhds
