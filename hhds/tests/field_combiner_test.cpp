// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// Contract test for the FIELD combiner (hhds/hash_mix.hpp) -- the commutative
// hash defined by the owner spec of 2026-09-16.
//
// Several of these pass under a weaker xor/sum combiner; only a product in
// F* = GF(2^61-1)* with gamma excluding 0 and 1 survives all of them. Each test
// names the invariant it pins.

#include "hhds/hash_mix.hpp"

#include <cstdio>
#include <cstdlib>
#include <set>
#include <vector>

namespace {

#define TEST_CHECK(condition)                                                            \
  do {                                                                                   \
    if (!(condition)) {                                                                  \
      std::fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition); \
      std::abort();                                                                      \
    }                                                                                    \
  } while (false)

uint64_t fold(const std::vector<uint64_t>& v) {
  hhds::Field_combiner c;
  for (auto x : v) {
    c.add(x);
  }
  return c.value();
}

// Invariant 2: gamma excludes 0 and 1, so no operand can ANNIHILATE the product
// or VANISH from it. This is the property a bare xor lacks in both directions.
void test_gamma_never_zero_or_one() {
  for (uint64_t v : {0ULL, 1ULL, 2ULL, hhds::kMersenne61 - 1, hhds::kMersenne61, hhds::kMersenne61 + 1, ~0ULL}) {
    TEST_CHECK(hhds::hash_gamma(v) >= 2);
    TEST_CHECK(hhds::hash_gamma(v) < hhds::kMersenne61);
  }
  for (uint64_t v = 0; v < 300000; ++v) {
    TEST_CHECK(hhds::hash_gamma(v) >= 2);
    TEST_CHECK(hhds::hash_gamma(v) < hhds::kMersenne61);
  }
}

// The field multiply stays closed, and 1 is a true identity (the accumulator's
// seed), so an empty group is distinguishable rather than forged.
void test_mulmod_closed_and_identity() {
  for (uint64_t a = 2; a < 4000; a += 7) {
    TEST_CHECK(hhds::hash_mulmod61(a, 1) == a);
    for (uint64_t b = 2; b < 4000; b += 97) {
      const auto r = hhds::hash_mulmod61(a, b);
      TEST_CHECK(r < hhds::kMersenne61);
      TEST_CHECK(r == hhds::hash_mulmod61(b, a));
    }
  }
}

// The whole point: inside one commutative group, order is invisible.
void test_order_independent() {
  TEST_CHECK(fold({1, 2, 3}) == fold({3, 1, 2}));
  TEST_CHECK(fold({1, 2, 3}) == fold({2, 3, 1}));
  TEST_CHECK(fold({7, 7, 9}) == fold({9, 7, 7}));
}

// Invariant 2 at the fold level: `a + a` is 2a, NOT nothing. A bare xor fails
// exactly here (h ^ h == 0 makes {a,a} hash like {}).
void test_duplicates_do_not_cancel() {
  TEST_CHECK(fold({5, 5}) != fold({}));
  TEST_CHECK(fold({5, 5}) != fold({5}));
  TEST_CHECK(fold({5, 5, 5}) != fold({5}));
  TEST_CHECK(fold({3, 3, 4}) != fold({3, 4, 4}));
}

// Invariant 3: the product alone does not encode cardinality, so the count is
// folded in explicitly.
void test_cardinality_is_folded() {
  TEST_CHECK(fold({9}) != fold({9, 9}));
  hhds::Field_combiner c;
  TEST_CHECK(c.count() == 0);
  c.add(1);
  c.add(1);
  TEST_CHECK(c.count() == 2);
}

// Distinct multisets give distinct digests over a sweep. A smoke test for the
// mixer, not a proof -- the hash is a FILTER and callers must still confirm.
void test_no_collisions_over_a_sweep() {
  std::set<uint64_t> seen;
  int                n = 0;
  for (uint64_t a = 0; a < 300; ++a) {
    for (uint64_t b = a; b < 300; ++b) {
      seen.insert(fold({a, b}));
      ++n;
    }
  }
  TEST_CHECK(seen.size() == static_cast<size_t>(n));

  // The exact shape that a SINGLE mix let through: gamma went linear on small
  // inputs (gamma(30) == 2*gamma(15)), so multisets with equal integer products
  // collided. Pinned by value so a future "simplification" of the mixing cannot
  // quietly reintroduce it.
  TEST_CHECK(fold({10, 30}) != fold({15, 20}));
  TEST_CHECK(fold({10, 70}) != fold({20, 35}));
  TEST_CHECK(fold({15, 70}) != fold({30, 35}));
}

// Invariant 1: every integer passes through mu before entering the field, so
// low-entropy structured input does not yield structured output.
void test_low_entropy_inputs_stay_separated() {
  std::set<uint64_t> seen;
  for (uint64_t i = 0; i < 4096; ++i) {
    seen.insert(fold({i}));
  }
  TEST_CHECK(seen.size() == 4096);
  std::set<uint64_t> seen2;
  for (uint64_t i = 0; i < 4096; ++i) {
    seen2.insert(hhds::hash_mu2(i, 0));
  }
  TEST_CHECK(seen2.size() == 4096);
}

// mu2 is deliberately NOT symmetric: it is the ORDERED mixer used to fold group
// digests positionally. If it were symmetric, the per-group form would lose the
// bank ordering (Sum's "as" vs "bs" -- a - b would hash like b - a).
void test_mu2_is_ordered() {
  TEST_CHECK(hhds::hash_mu2(1, 2) != hhds::hash_mu2(2, 1));
  TEST_CHECK(hhds::hash_mu2(0, 5) != hhds::hash_mu2(5, 0));
}

}  // namespace

int main() {
  test_gamma_never_zero_or_one();
  test_mulmod_closed_and_identity();
  test_order_independent();
  test_duplicates_do_not_cancel();
  test_cardinality_is_folded();
  test_no_collisions_over_a_sweep();
  test_low_entropy_inputs_stay_separated();
  test_mu2_is_ordered();
  std::printf("field_combiner_test passed\n");
  return 0;
}

