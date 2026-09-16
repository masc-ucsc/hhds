// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// Order-independent hashing primitives.
//
// Passes that key a node (or a cone of nodes) on its SHAPE rather than on its
// identity kept re-implementing the same rule: "combine the operand hashes
// commutatively within a sink-port class, and mix the sink pid in so operands
// never migrate across ports". Eleven independent copies of it existed in
// LiveHD alone (CSE keys, cone digests, sub-match keys, ref/impl pairing keys),
// every one of them spelled as "bucket by port, sort each bucket, fold".
//
// The sort is not needed. An order-independent COMBINER over the terms does
// the same job in one pass, and the port mix is what keeps a non-commutative
// cell honest: an op whose sink pin takes a single driver has one term per pid,
// so mixing the pid makes every term unique and the commutative combiner
// degenerates to a positional one. Only the pins that genuinely fold several
// drivers under the cell's identity op (a Sum's `as`, an Or's `a`) end up with
// several terms sharing a key, which is exactly where commutativity is wanted.

#pragma once

#include <cstdint>
#include <span>

namespace hhds {

// MurmurHash3's 64-bit finalizer. A deterministic mixer for internal digests,
// not a cryptographic hash.
[[nodiscard]] constexpr uint64_t hash_mix64(uint64_t value) noexcept {
  value ^= value >> 33U;
  value *= 0xff51afd7ed558ccdULL;
  value ^= value >> 33U;
  value *= 0xc4ceb9fe1a85ec53ULL;
  value ^= value >> 33U;
  return value;
}

// Order-DEPENDENT combiner, for the positional parts of a digest.
[[nodiscard]] constexpr uint64_t hash_combine64(uint64_t hash, uint64_t value) noexcept {
  return hash_mix64(hash ^ (value + 0x9e3779b97f4a7c15ULL + (hash << 6U) + (hash >> 2U)));
}

// Order-INDEPENDENT combiner over a multiset of 64-bit terms.
//
// Each term is mixed first (so the accumulators never see raw, structured
// input), then folded three ways:
//   * a SUM, which separates multisets that a pure xor cannot (xor cancels a
//     repeated pair, {a, a} vs {}),
//   * an XOR, which separates multisets a pure sum cannot as cheaply, and
//   * the COUNT, which separates {a} from {a, 0}-style padding.
// The three are combined positionally at the end, so the result depends on the
// multiset and on nothing else. No sort, one pass, and it streams.
class Commutative_combiner {
public:
  constexpr void add(uint64_t term) noexcept {
    const uint64_t m  = hash_mix64(term);
    sum_             += m;
    xor_             ^= m;
    ++count_;
  }

  [[nodiscard]] constexpr uint64_t value() const noexcept {
    return hash_combine64(hash_combine64(hash_mix64(sum_), xor_), count_);
  }

  [[nodiscard]] constexpr uint64_t count() const noexcept { return count_; }

private:
  uint64_t sum_   = 0;
  uint64_t xor_   = 0;
  uint64_t count_ = 0;
};

[[nodiscard]] constexpr uint64_t commutative_combine(std::span<const uint64_t> terms) noexcept {
  Commutative_combiner c;
  for (uint64_t t : terms) {
    c.add(t);
  }
  return c.value();
}

// 128-bit variant, for the callers that key cone identity on the digest and
// cannot afford a 64-bit birthday collision over a large cone population
// (LiveHD's color_reduce and sim_color_plan).
struct Sig128 {
  uint64_t a = 0;
  uint64_t b = 0;

  friend constexpr auto operator<=>(const Sig128&, const Sig128&) = default;
};

inline constexpr uint64_t kSig128_lane_b = 0x9ae16a3b2f90404fULL;

[[nodiscard]] constexpr Sig128 hash_mix128(uint64_t value) noexcept {
  return {hash_mix64(value), hash_mix64(value ^ kSig128_lane_b)};
}

[[nodiscard]] constexpr Sig128 hash_combine128(Sig128 hash, Sig128 value) noexcept {
  return {hash_combine64(hash.a, value.a), hash_combine64(hash.b, hash_mix64(value.b ^ kSig128_lane_b))};
}

class Commutative_combiner128 {
public:
  constexpr void add(Sig128 term) noexcept {
    a_.add(term.a);
    b_.add(term.b ^ kSig128_lane_b);
  }
  constexpr void add(uint64_t term) noexcept { add(hash_mix128(term)); }

  [[nodiscard]] constexpr Sig128 value() const noexcept { return {a_.value(), b_.value()}; }

private:
  Commutative_combiner a_;
  Commutative_combiner b_;
};

[[nodiscard]] constexpr Sig128 commutative_combine128(std::span<const Sig128> terms) noexcept {
  Commutative_combiner128 c;
  for (const auto& t : terms) {
    c.add(t);
  }
  return c.value();
}

}  // namespace hhds
