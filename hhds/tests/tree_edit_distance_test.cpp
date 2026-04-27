#include "hhds/tree_edit_distance.hpp"

#include <cassert>
#include <iostream>

#include "hhds/tree.hpp"

void test_identical_trees() {
  ::std::cout << "Test 1: Identical trees → distance = 0\n";
  auto t1 = hhds::Tree::create();
  auto t2 = hhds::Tree::create();

  auto r1 = t1->add_root_node();
  r1.set_type(1);
  auto c1 = r1.add_child();
  c1.set_type(2);
  auto g1 = c1.add_child();
  g1.set_type(3);

  auto r2 = t2->add_root_node();
  r2.set_type(1);
  auto c2 = r2.add_child();
  c2.set_type(2);
  auto g2 = c2.add_child();
  g2.set_type(3);

  auto res = hhds::TreeEditDistance::compute(t1, t2);
  ::std::cout << "  Distance: " << res.distance << " (expected 0.0)\n";
  assert(res.distance == 0.0 && "Identical trees should have distance 0");
}

void test_single_relabel() {
  ::std::cout << "Test 2: Single relabel → distance = 1\n";
  auto t1 = hhds::Tree::create();
  auto t2 = hhds::Tree::create();

  // Tree 1
  auto r1 = t1->add_root_node();
  r1.set_type(1);
  auto c1 = r1.add_child();
  c1.set_type(2);

  // Tree 2: same structure, different middle node type
  auto r2 = t2->add_root_node();
  r2.set_type(1);
  auto c2 = r2.add_child();
  c2.set_type(99);

  auto res = hhds::TreeEditDistance::compute(t1, t2);
  ::std::cout << "  Distance: " << res.distance << " (expected 1.0)\n";
  assert(res.distance == 1.0 && "Single relabel should cost 1.0");
}

void test_single_deletion() {
  ::std::cout << "Test 3: Single deletion → distance = 1\n";
  auto t1 = hhds::Tree::create();
  auto t2 = hhds::Tree::create();

  // Tree 1: A → [B, C]
  auto r1 = t1->add_root_node();
  r1.set_type(1);
  auto b = r1.add_child();
  b.set_type(2);
  auto c = b.append_sibling();
  c.set_type(3);

  // Tree 2: A → [B]  (C deleted)
  auto r2 = t2->add_root_node();
  r2.set_type(1);
  auto b2 = r2.add_child();
  b2.set_type(2);

  auto res = hhds::TreeEditDistance::compute(t1, t2);
  ::std::cout << "  Distance: " << res.distance << " (expected 1.0)\n";
  assert(res.distance == 1.0 && "Single deletion should cost 1.0");
}

void test_single_insertion() {
  ::std::cout << "Test 4: Single insertion → distance = 1\n";
  auto t1 = hhds::Tree::create();
  auto t2 = hhds::Tree::create();

  // Tree 1: A → [B]
  auto r1 = t1->add_root_node();
  r1.set_type(1);
  auto b = r1.add_child();
  b.set_type(2);

  // Tree 2: A → [B, C]  (C inserted)
  auto r2 = t2->add_root_node();
  r2.set_type(1);
  auto b2 = r2.add_child();
  b2.set_type(2);
  auto c2 = b2.append_sibling();
  c2.set_type(3);

  auto res = hhds::TreeEditDistance::compute(t1, t2);
  ::std::cout << "  Distance: " << res.distance << " (expected 1.0)\n";
  assert(res.distance == 1.0 && "Single insertion should cost 1.0");
}

int main() {
  ::std::cout << "\n════════════════════════════════════════\n";
  ::std::cout << "Zhang-Shasha Algorithm Test Suite\n";
  ::std::cout << "════════════════════════════════════════\n\n";

  try {
    test_identical_trees();
    ::std::cout << "  ✓ PASS\n\n";

    test_single_relabel();
    ::std::cout << "  ✓ PASS\n\n";

    test_single_deletion();
    ::std::cout << "  ✓ PASS\n\n";

    test_single_insertion();
    ::std::cout << "  ✓ PASS\n\n";

    ::std::cout << "════════════════════════════════════════\n";
    ::std::cout << "All tests PASSED ✓\n";
    ::std::cout << "════════════════════════════════════════\n";
  } catch (const ::std::exception& e) {
    ::std::cout << "✗ FAILED: " << e.what() << "\n";
    return 1;
  }

  return 0;
}
