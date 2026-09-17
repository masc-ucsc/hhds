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

// ---------------------------------------------------------------------------
// The field combiner (owner spec, 2026-09-16). This is the construction the
// commutative fold is DEFINED by; the sum/xor/count combiner below predates it
// and is kept only for callers that have not moved.
//
//   p      = 2^61 - 1 (a Mersenne prime); the group is F* = GF(p)*
//   mu     = splitmix64 finalizer, a non-algebraic mixer
//   mu2(a,b) = mu(a ^ mu(b))
//   gamma(h) = max(2, mu(h) mod p)  -- projects into F*, EXCLUDING 0 and 1
//
// The commutative fold is the PRODUCT of gamma(term) in F*, with the operand
// COUNT folded in separately. Why a product in a prime field rather than the
// sum/xor/count fold below:
//   * gamma never yields 0, so no operand can ANNIHILATE the accumulator, and
//     never yields 1, so no operand can VANISH from it. A bare xor has both
//     failures (h ^ h == 0), and a sum has the second (+0).
//   * duplicates accumulate multiplicatively: {a,a} gives gamma(a)^2 != gamma(a),
//     so `a + a` (which is 2a) cannot hash like `a`.
//   * the count is folded explicitly because the product alone does not encode
//     cardinality.
//
// INVARIANTS (all enforced by construction below, and pinned by the tests):
//   1. every integer passes through mu before reaching mu2 or gamma -- no raw
//      index, width or port id ever enters the field;
//   2. gamma excludes 0 and 1, so no operand annihilates or vanishes;
//   3. the count is folded in explicitly;
//   4. edge attributes enter BEFORE the product, so an inversion cannot migrate
//      between operands;
//   6. the commutative branch never folds a pin index; the ordered branch always
//      does (see node_hash.hpp, which owns 5 and 7).
//
// THIS IS A FILTER, NOT A DECISION: a 64-bit digest of an unbounded structure
// collides. A caller that MERGES on a match must confirm with a real operand
// comparison; a false merge is a silent miscompile.

inline constexpr uint64_t kMersenne61 = (1ULL << 61U) - 1U;  // p

// splitmix64 finalizer -- mu.
[[nodiscard]] constexpr uint64_t hash_mu(uint64_t value) noexcept {
  value ^= value >> 30U;
  value *= 0xbf58476d1ce4e5b9ULL;
  value ^= value >> 27U;
  value *= 0x94d049bb133111ebULL;
  value ^= value >> 31U;
  return value;
}

// mu2(a,b) = mu(a ^ mu(b)) -- the pairwise mixer every fold goes through.
[[nodiscard]] constexpr uint64_t hash_mu2(uint64_t a, uint64_t b) noexcept { return hash_mu(a ^ hash_mu(b)); }

// gamma: project a digest into F* = GF(2^61-1)*, excluding 0 and 1 so that no
// operand can annihilate the product or drop out of it.
[[nodiscard]] constexpr uint64_t hash_gamma(uint64_t h) noexcept {
  const uint64_t r = hash_mu(h) % kMersenne61;
  return r < 2U ? 2U : r;
}

// Multiply in GF(2^61-1). The Mersenne shape lets the 128-bit product fold with
// shifts and an add instead of a division.
[[nodiscard]] constexpr uint64_t hash_mulmod61(uint64_t a, uint64_t b) noexcept {
  const __uint128_t prod = static_cast<__uint128_t>(a) * b;
  uint64_t          lo   = static_cast<uint64_t>(prod) & kMersenne61;
  const uint64_t    hi   = static_cast<uint64_t>(prod >> 61U);
  lo += hi;
  if (lo >= kMersenne61) {
    lo -= kMersenne61;
  }
  return lo;
}

// Order-independent fold over one COMMUTATIVE GROUP (one operand bank).
//
// A cell with several banks (LiveHD's Sum/LT/GT: "as" on even pids, "bs" on odd)
// is NOT one commutative set and NOT positional -- it is a SEQUENCE of groups.
// Fold each bank with its own Field_combiner, then combine the bank digests
// POSITIONALLY, so a swap inside a bank is invisible and a swap across banks is
// not. A non-banked op is the degenerate case: every pid is its own group of one.
class Field_combiner {
public:
  constexpr void add(uint64_t term) noexcept {
    // mu BEFORE gamma, per invariant 1 ("every integer passes through mu before
    // reaching mu2 or gamma"). gamma applies mu internally too, so a term is
    // mixed TWICE before it enters the field -- and that is load-bearing, not
    // belt-and-braces. With a single mix, splitmix64's leading `z ^= z >> 30` is
    // a NO-OP for any term below 2^30, the avalanche is one multiply short, and
    // enough multiplicative structure survives that gamma goes LINEAR on small
    // inputs: measured gamma(30) == 2*gamma(15) and gamma(20) == 2*gamma(10)
    // exactly, so {10,30} and {15,20} collided because 10*30 == 15*20. Three
    // such collisions in a 4095-multiset sweep; zero with the mix below, and
    // zero over 45150.
    prod_ = hash_mulmod61(prod_, hash_gamma(hash_mu(term)));
    ++count_;
  }

  [[nodiscard]] constexpr uint64_t value() const noexcept { return hash_mu2(prod_, count_); }
  [[nodiscard]] constexpr uint64_t count() const noexcept { return count_; }

private:
  uint64_t prod_  = 1U;  // F* identity; gamma never returns 1, so it cannot be forged
  uint64_t count_ = 0;
};

[[nodiscard]] constexpr uint64_t field_combine(std::span<const uint64_t> terms) noexcept {
  Field_combiner c;
  for (uint64_t t : terms) {
    c.add(t);
  }
  return c.value();
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

}  // namespace hhds
