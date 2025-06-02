#pragma once

#include <array>
#include <cassert>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <vector>

#include "graph_sizing.hpp"
#include "unordered_dense.hpp"

namespace hhds {

constexpr int NUM_NODES         = 10;
constexpr int NUM_TYPES         = 3;
constexpr int MAX_PINS_PER_NODE = 10;

class __attribute__((packed)) Pin {
public:
  Pin();
  Pin(Nid master_nid_value, Port_id port_id_value);

  [[nodiscard]] Nid       get_master_nid() const;  // should be in node
  [[nodiscard]] Port_id   get_port_id() const;
  auto                    add_edge(Pid self_id, Pid other_id) -> bool;     // should be in node
  [[nodiscard]] bool      has_edges() const;                               // should be in node
  [[nodiscard]] Pid       get_next_pin_id() const;                         // should be in node
  void                    set_next_pin_id(Pid id);                         // should be in node
  [[nodiscard]] bool      check_overflow() const { return use_overflow; }  // should be in node
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
    const Pin*             pin_;
    Pid                    pid_;
    ankerl::unordered_dense::set<Vid>* set_;
    bool                   own_;

    static ankerl::unordered_dense::set<Vid>* acquire_set() noexcept;
    static void                   release_set(ankerl::unordered_dense::set<Vid>*) noexcept;
    static void                   populate_set(const Pin*, ankerl::unordered_dense::set<Vid>&, Pid) noexcept;
  };
  [[nodiscard]] auto get_edges(Pid pid) const noexcept -> EdgeRange;  // should be in node

private:
  auto overflow_handling(Pid self_id, Vid other_id) -> bool;

  Nid     master_nid : Nid_bits;   // 42 bits
  Port_id port_id : Port_bits;     // 22 bits    => 64 bits (8 bytes)   // should not be in node
  Pid     next_pin_id : Nid_bits;  // 42 bits
  Nid     ledge0 : Nid_bits;       // 42 bits to too far node/pin (does not fit in sedge) => 64 bits (8 bytes)
  Nid     ledge1 : Nid_bits;       // 42 bits to too far node/pin (does not fit in sedge) => 64 bits (8 bytes)
  uint8_t use_overflow : 1;        // 1 bit      => 64 bits (8 bytes)

  // adds upto a total of 191 bits => 24 bytes
  union {
    int64_t                sedges;  // 48 bits
    ankerl::unordered_dense::set<Vid>* set;     // 8 bytes
  } sedges_;                        // Total: 8 bytes
  // Total: 32 bytes
};

static_assert(sizeof(Pin) == 32, "Pin size mismatch");

class __attribute__((packed)) Node {
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
    const Node*                   node_;
    Nid                           nid_;
    ankerl::unordered_dense::set<Vid>*        set_;
    bool                          own_;
    static ankerl::unordered_dense::set<Vid>* acquire_set() noexcept;
    static void                   release_set(ankerl::unordered_dense::set<Vid>*) noexcept;
    static void                   populate_set(const Node*, ankerl::unordered_dense::set<Vid>&, Nid) noexcept;
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
    int64_t                sedges;  // 48 bits
    ankerl::unordered_dense::set<Vid>* set;     // 8 bytes
  } sedges_;                        // Total: 8 bytes
};
static_assert(sizeof(Node) == 32, "Node size mismatch");

class Graph {
public:
  Graph();
  void clear_graph();

  [[nodiscard]] Nid  create_node();
  [[nodiscard]] Pid  create_pin(Nid nid, Port_id port_id);
  [[nodiscard]] auto ref_node(Nid id) const -> Node*;
  [[nodiscard]] auto ref_pin(Pid id) const -> Pin*;

  void add_edge(Pid driver_id, Pid sink_id);
  void display_graph() const;
  void display_next_pin_of_node() const;

private:
  void add_edge_int(Pid self_id, Pid other_id);
  void set_next_pin(Nid nid, Pid next_pin);

  std::vector<Node> node_table;
  std::vector<Pin>  pin_table;
};

}  // namespace hhds
