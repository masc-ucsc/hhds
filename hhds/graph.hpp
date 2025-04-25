#pragma once

#include <array>
#include <cassert>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <vector>

#include "graph_sizing.hpp"
#include "hash_set3.hpp"

namespace hhds {

constexpr int NUM_NODES         = 10;
constexpr int NUM_TYPES         = 3;
constexpr int MAX_PINS_PER_NODE = 10;

class __attribute__((packed)) Pin {
public:
  Pin();
  Pin(Nid master_nid_value, Port_id port_id_value);

  [[nodiscard]] Nid                    get_master_nid() const;
  [[nodiscard]] Port_id                get_port_id() const;
  auto                                 add_edge(Pid self_id, Pid other_id) -> bool;
  [[nodiscard]] bool                   has_edges() const;
  [[nodiscard]] std::array<int32_t, 4> get_sedges(Pid pid) const;
  [[nodiscard]] Pid                    get_next_pin_id() const;
  void                                 set_next_pin_id(Pid id);

private:
  auto overflow_handling(Pid self_id, Pid other_id) -> bool;

  Nid     master_nid : Nid_bits;
  Port_id port_id : Port_bits;
  int64_t sedge : 48;              // up to 4×12-bit “short edges”
  Pid     next_pin_id : Nid_bits;  // linked list of pins on same node
  uint8_t use_overflow : 1;
  int64_t padding : 21;
};

class __attribute__((packed)) Node {
public:
  Node();
  explicit Node(Nid nid_value);

  void               set_type(Type t);
  [[nodiscard]] Nid  get_nid() const;
  [[nodiscard]] Type get_type() const;
  [[nodiscard]] Pid  get_next_pin_id() const;
  void               set_next_pin_id(Pid id);

private:
  void clear_node();

  Nid  nid : 42;
  Type type : 16;
  Pid  next_pin_id : 42;
};

class __attribute__((packed)) Graph {
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
