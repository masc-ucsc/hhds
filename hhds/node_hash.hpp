// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// The canonical structural hash of an hhds graph NODE, on top of the
// order-independent combiners in hash_mix.hpp.

#pragma once

#include <algorithm>
#include <cstdint>
#include <utility>

#include "absl/container/flat_hash_map.h"
#include "absl/container/inlined_vector.h"
#include "hhds/graph.hpp"
#include "hhds/hash_mix.hpp"

namespace hhds {

// The canonical structural hash of ONE node:
//
//   mix(type) combined with commutative_combine over the inputs, where each
//   input contributes mix(child_hash(driver), sink_role(sink_pid)).
//
// `child_hash` maps a driver Pin_class to the hash already computed for it (the
// caller's memo table, a cone digest, a rank, an IO name hash -- whatever the
// pass keys on). It is called ONCE per driver, in inp_sorted_pins() order.
//
// `sink_role` maps a sink pid to the OPERAND ROLE it plays on this node's cell
// type. It exists because a SINK PIN TAKES EXACTLY ONE DRIVER: a cell that
// folds several operands under one commutative identity op spends one sink pid
// PER OPERAND, so the raw pid distinguishes operands that are semantically
// interchangeable, and `Sum(a, b)` would not hash like `Sum(b, a)`. The role is
// what must be mixed in -- "added" vs "subtracted", `a` vs `b` -- not the slot.
// hhds does not own the cell-type table, so the caller supplies the mapping;
// LiveHD passes Ntype::sink_bank. The default_sink_role overload below keeps
// the raw pid, which is correct for any IR whose cells are purely positional.
//
// With the role mixed in, `Sum(a,b)` and `Sum(b,a)` hash the same while a swap
// ACROSS the add and subtract roles does not.
//
// The combiner is a MULTISET fold (hash_mix.hpp): a sum, an xor and a count,
// never a bare xor -- `h(a) ^ h(a) == 0` would make `a + a` hash like an empty
// operand list, and `a + a` is 2a, not nothing. Duplicated operands accumulate.
//
// THIS IS A FILTER, NOT A DECISION. It is a 64-bit digest of an unbounded
// structure, so two different nodes CAN collide. A caller that merges nodes on
// a match (CSE, cone dedup) must confirm with a real multiset comparison of the
// operands first; a false merge is a silent miscompile.
inline constexpr uint64_t kTagLeaf = 0x1eaf00001eaf0001ULL;
inline constexpr uint64_t kTagNode = 0x40de000040de0002ULL;
inline constexpr uint64_t kTagCycle = 0xc1c1e000c1c1e003ULL;

struct default_sink_role {
  [[nodiscard]] Port_id operator()(Type, Port_id pid) const noexcept { return pid; }
};

// PER-GROUP fold (owner spec, 2026-09-16). A cell is not simply "commutative" or
// "ordered": LiveHD's Sum/LT/GT carry TWO operand banks ("as" on even pids, "bs"
// on odd), each commutative INTERNALLY and ordered with respect to the other.
// Mult/And/Or/Xor/Ror/EQ carry one bank; every other op is unbanked, where the
// pid IS the role and each group holds exactly one operand.
//
// So: fold each group with a Field_combiner (order-independent inside the
// group), then combine the group digests POSITIONALLY by their role id. A swap
// INSIDE a bank is invisible; a swap ACROSS banks is not; and an unbanked op
// degenerates to a purely positional hash without a special case.
//
// This is also why the fold must key on the ROLE and never on the raw pid: a Sum
// with three adds and one subtract occupies pids {0,2,4} and {1} -- pids are
// deliberately NOT dense (cell.hpp), and one operand per sink pin makes the raw
// pid distinguish operands that are semantically interchangeable.
//
// op(n) is folded in at the end, so `add` and `and` over the same operand set
// cannot collide (invariant 7).
template <typename ChildHash, typename SinkRole = default_sink_role>
[[nodiscard]] uint64_t canonical_node_hash(const Node_class& node, ChildHash&& child_hash, SinkRole&& sink_role = {}) {
  const auto type = node.get_type();

  // Small ordered map: role id -> that group's fold. Node arity is tiny (a wide
  // Hotmux is the outlier and is unbanked, so each of its pids is its own group,
  // which stays positional). Kept sorted so the positional combine below is
  // deterministic without a separate sort pass.
  absl::InlinedVector<std::pair<uint64_t, Field_combiner>, 4> groups;
  // ONE DRIVER PER SINK PIN, so the node's sink-pin list IS its operand list.
  // The plural reader keeps a compact loop's two-driver carry-in folding into
  // the same group, exactly as the in-edge walk this replaced did.
  for (auto sink : node.inp_sorted_pins()) {
    const uint64_t role = static_cast<uint64_t>(sink_role(type, sink.get_port_id()));
    for (const auto& driver : sink.get_driver_pins()) {
      const uint64_t child = static_cast<uint64_t>(child_hash(driver));
      auto it = std::lower_bound(groups.begin(), groups.end(), role, [](const auto& g, uint64_t r) { return g.first < r; });
      if (it == groups.end() || it->first != role) {
        it = groups.insert(it, {role, Field_combiner{}});
      }
      it->second.add(child);
    }
  }

  uint64_t acc = hash_mu(static_cast<uint64_t>(type));
  for (const auto& [role, g] : groups) {
    acc = hash_mu2(acc, hash_mu2(role, g.value()));
  }
  return acc;
}

// group_fold — THE shared spelling of "bucket terms by operand role, fold each
// bucket order-independently, combine the buckets positionally".
//
// This is the composition six LiveHD call sites each hand-rolled (pass_partition
// x4, semdiff, color_reduce, pass_submatch, sim_color_plan, cgen_sim). It is the
// same rule canonical_node_hash applies to a node's own operands, lifted so a
// caller that has already built its own `role -> terms` map can reuse it.
//
// THE GROUP KEYS ARE SORTED HERE, and that is load-bearing. The predecessor put
// every term in ONE combiner with the role mixed into each term, so iteration
// order of an absl::flat_hash_map was harmless. Combining group digests
// POSITIONALLY is strictly stronger -- no term can migrate across roles even
// under a collision in the role mixing -- but it makes order matter, and
// flat_hash_map iteration order is NOT stable across runs. Folding an unsorted
// sequence would hand every cache site a digest that changes run to run.
//
// `base` is folded in first so a caller's tag/kind prefix cannot be confused
// with a group digest.
template <typename ByGroup>
[[nodiscard]] inline uint64_t group_fold(uint64_t base, const ByGroup& by_group) {
  absl::InlinedVector<std::pair<uint64_t, uint64_t>, 4> digests;
  digests.reserve(by_group.size());
  for (const auto& [role, terms] : by_group) {
    Field_combiner c;
    for (const auto& t : terms) {
      c.add(static_cast<uint64_t>(t));
    }
    digests.emplace_back(static_cast<uint64_t>(static_cast<uint32_t>(role)), c.value());
  }
  std::sort(digests.begin(), digests.end());
  uint64_t acc = hash_mu2(base, digests.size());
  for (const auto& [role, d] : digests) {
    acc = hash_mu2(acc, hash_mu2(role, d));
  }
  return acc;
}

// ---------------------------------------------------------------------------
// CONE and SUBGRAPH identity (owner ruling, 2026-09-16).
//
// ONE facility, not a per-pass re-implementation. Before this, six call sites
// hand-rolled the same composition over Commutative_combiner -- pass_partition
// (x4), semdiff, color_reduce, pass_submatch, sim_color_plan, cgen_sim -- each
// spelled slightly differently. cone_hash is the shared spelling.
//
//   cone_hash(driver_pin)  the structural identity of the logic cone driving
//                          that pin, back to the cone's boundary.
//   subgraph_hash(graph)   a subgraph's identity IS the fold of its output
//                          cones (owner: "subgraph hashid is just a hash of
//                          output cones in the subgraph").
//
// 64-BIT ON PURPOSE. A collision costs a verifying traversal, and from an
// incremental standpoint that is extremely rare -- so the 128-bit variants and
// their Sig128 plumbing are not worth their weight. The hash is a FILTER: a hit
// means "compare them", a miss means "different" and holds unconditionally.
//
// STABLE ACROSS RUNS -- this is the property incremental depends on, and the
// easiest one to break. NODE IDS AND PIN IDS ARE NEVER FOLDED IN: they are
// allocation order and are reshuffled every time a graph is loaded. Only
// PORT_ID, cell type, width and CONNECTIVITY enter the digest. (Node/pin ids
// appear ONLY as the memo key below, which is per-invocation scratch and never
// reaches a hash value.)
//
// A cone STOPS at a boundary pin: a graph input, a constant, a state element,
// or anything the caller's `is_boundary` predicate claims. A boundary
// contributes a LEAF term built from its stable facts, never its identity.

// Leaf term for a cone boundary: (kind, port_id, width). No id, by construction.
template <typename WidthOf>
[[nodiscard]] inline uint64_t cone_leaf_hash(const Pin_class& pin, WidthOf&& width_of) {
  uint64_t h = hash_mu2(kTagLeaf, static_cast<uint64_t>(pin.get_master_node().get_type()));
  h          = hash_mu2(h, static_cast<uint64_t>(pin.get_port_id()));
  h          = hash_mu2(h, static_cast<uint64_t>(width_of(pin)));
  return h;
}

// cone_hash — memoised post-order walk back from `out`, stopping at boundaries.
//
// `is_boundary(pin)` -> true when the cone stops there.
// `width_of(pin)`    -> the pin's bit width (0 when unknown; folded either way).
// `sink_role`        -> the operand-bank mapping (LiveHD: Ntype::sink_bank), so
//                       a commutative cell hashes the same whichever slot each
//                       operand landed in.
template <typename IsBoundary, typename WidthOf, typename SinkRole = default_sink_role>
[[nodiscard]] inline uint64_t cone_hash(const Pin_class& out, IsBoundary&& is_boundary, WidthOf&& width_of,
                                        SinkRole&& sink_role = {}) {
  // Memo keyed on the driver pin's index. SCRATCH ONLY -- the key is an id and
  // must never be folded into a digest; it just stops us re-walking a shared
  // fanout cone once per consumer.
  absl::flat_hash_map<uint64_t, uint64_t> memo;

  auto visit = [&](auto&& self, const Pin_class& pin) -> uint64_t {
    if (pin.is_invalid()) {
      return hash_mu2(kTagLeaf, 0);
    }
    const uint64_t key = static_cast<uint64_t>(pin.get_class_index().value);
    if (auto it = memo.find(key); it != memo.end()) {
      return it->second;
    }
    uint64_t h;
    if (is_boundary(pin)) {
      h = cone_leaf_hash(pin, width_of);
    } else {
      // Seed the memo BEFORE recursing so a combinational cycle terminates with
      // a well-defined value instead of overflowing the stack. A cycle is a
      // defect elsewhere, but a hash must not be the thing that crashes on it.
      // The marker must not contain the memo key: that would make an unrelated
      // allocation change the digest of an otherwise identical cyclic cone.
      memo.emplace(key, hash_mu(kTagCycle));
      const auto node = pin.get_master_node();
      h               = canonical_node_hash(node, [&](const Pin_class& drv) { return self(self, drv); }, sink_role);
      // The OUTPUT PORT distinguishes a multi-output cell's cones.
      h               = hash_mu2(h, static_cast<uint64_t>(pin.get_port_id()));
    }
    memo[key] = h;
    return h;
  };
  return visit(visit, out);
}

// subgraph_hash — the fold of a subgraph's OUTPUT cones.
//
// Ordered by output port_id (stable; the ids are not), so two structurally
// identical subgraphs agree regardless of the order their outputs were created.
template <typename OutputPins, typename IsBoundary, typename WidthOf, typename SinkRole = default_sink_role>
[[nodiscard]] inline uint64_t subgraph_hash(OutputPins&& outs, IsBoundary&& is_boundary, WidthOf&& width_of,
                                            SinkRole&& sink_role = {}) {
  absl::InlinedVector<std::pair<uint64_t, uint64_t>, 8> by_port;  // (port_id, cone hash)
  for (const auto& o : outs) {
    by_port.emplace_back(static_cast<uint64_t>(o.get_port_id()), cone_hash(o, is_boundary, width_of, sink_role));
  }
  std::sort(by_port.begin(), by_port.end());
  uint64_t acc = hash_mu2(kTagNode, by_port.size());
  for (const auto& [port, h] : by_port) {
    acc = hash_mu2(acc, hash_mu2(port, h));
  }
  return acc;
}

}  // namespace hhds
