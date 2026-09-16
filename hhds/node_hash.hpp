// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// The canonical structural hash of an hhds graph NODE, on top of the
// order-independent combiners in hash_mix.hpp.

#pragma once

#include <cstdint>

#include "hhds/graph.hpp"
#include "hhds/hash_mix.hpp"

namespace hhds {

// The canonical structural hash of ONE node:
//
//   mix(type) combined with commutative_combine over the inputs, where each
//   input contributes mix(child_hash(driver), sink_pid).
//
// `child_hash` maps a driver Pin_class to the hash already computed for it (the
// caller's memo table, a cone digest, a rank, an IO name hash -- whatever the
// pass keys on). It is called ONCE per input edge, in inp_edges() order.
//
// Sum(a,b) and Sum(b,a) hash the same; Sum(a) - Sub-shaped operand swaps across
// the `as` / `bs` pins do not, because the pid is mixed into each term.
template <typename ChildHash>
[[nodiscard]] uint64_t canonical_node_hash(const Node_class& node, ChildHash&& child_hash) {
  Commutative_combiner c;
  for (const auto& e : node.inp_edges()) {
    const uint64_t child = static_cast<uint64_t>(child_hash(e.driver));
    c.add(hash_combine64(child, static_cast<uint64_t>(e.sink.get_port_id())));
  }
  return hash_combine64(hash_mix64(static_cast<uint64_t>(node.get_type())), c.value());
}

template <typename ChildHash>
[[nodiscard]] Sig128 canonical_node_hash128(const Node_class& node, ChildHash&& child_hash) {
  Commutative_combiner128 c;
  for (const auto& e : node.inp_edges()) {
    const Sig128 child = child_hash(e.driver);
    c.add(hash_combine128(child, hash_mix128(static_cast<uint64_t>(e.sink.get_port_id()))));
  }
  return hash_combine128(hash_mix128(static_cast<uint64_t>(node.get_type())), c.value());
}

}  // namespace hhds
