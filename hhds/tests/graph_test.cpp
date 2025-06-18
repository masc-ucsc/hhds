#include "graph.hpp"

#include <cassert>
#include <iostream>

using namespace hhds;

void test_node_to_node() {
  Graph g;
  Nid   n1 = g.create_node();
  Nid   n2 = g.create_node();
  g.add_edge(n1, n2);

  {
    auto range = g.ref_node(n1)->get_edges(n1);
    auto it    = range.begin();
    assert(it != range.end() && "no edge out of n1");
    assert(*it == n2 && "edge out of n1 should be n2");
    ++it;
    assert(it == range.end() && "only one edge should exist");
  }

  {
    auto range = g.ref_node(n2)->get_edges(n2);
    auto it    = range.begin();
    assert(it != range.end() && "no edge out of n2");
    assert(*it == n1 | 2 && "edge out of n2 should be n1");
    ++it;
    assert(it == range.end() && "only one edge should exist");
  }
  std::cout << "test_node_to_node passed\n";
}

void test_pin_to_pin() {
  Graph g;
  Nid   n  = g.create_node();
  Pid   p1 = g.create_pin(n, 0);
  Pid   p2 = g.create_pin(n, 1);
  g.add_edge(p1, p2);
  {
    auto range = g.ref_pin(p1)->get_edges(p1);
    auto it    = range.begin();
    assert(it != range.end() && *it == p2 && "test_pin_to_pin failed: missing p2");
    ++it;
    assert(it == range.end() && "test_pin_to_pin failed: extra edges found");
  }

  {
    auto range = g.ref_pin(p2)->get_edges(p2);
    auto it    = range.begin();
    assert(it != range.end() && *it == p1 | 2 && "test_pin_to_pin failed: missing p1");
    ++it;
    assert(it == range.end() && "test_pin_to_pin failed: extra edges found");
  }

  std::cout << "test_pin_to_pin passed\n";
}

void test_node_to_pin() {
  Graph g;
  Nid   n = g.create_node();
  Pid   p = g.create_pin(n, 0);
  g.add_edge(n, p);
  {
    auto range = g.ref_node(n)->get_edges(n);
    auto it    = range.begin();
    assert(it != range.end() && *it == p && "test_node_to_pin failed: missing p");
    ++it;
    assert(it == range.end() && "test_node_to_pin failed: extra edges found");
  }
  {
    auto range = g.ref_pin(p)->get_edges(p);
    auto it    = range.begin();
    assert(it != range.end() && *it == n | 2 && "test_node_to_pin failed: missing n");
    ++it;
    assert(it == range.end() && "test_node_to_pin failed: extra edges found");
  }
  std::cout << "test_node_to_pin passed\n";
}

void test_pin_to_node() {
  Graph g;
  Nid   n = g.create_node();
  Pid   p = g.create_pin(n, 0);
  g.add_edge(p, n);
  {
    auto range = g.ref_pin(p)->get_edges(p);
    auto it    = range.begin();
    assert(it != range.end() && *it == n && "test_pin_to_node failed: missing n");
    ++it;
    assert(it == range.end() && "test_pin_to_node failed: extra edges found");
  }
  {
    auto range = g.ref_node(n)->get_edges(n);
    auto it    = range.begin();
    assert(it != range.end() && *it == p | 2 && "test_pin_to_node failed: missing p");
    ++it;
    assert(it == range.end() && "test_pin_to_node failed: extra edges found");
  }

  std::cout << "test_pin_to_node passed\n";
}

void test_sedges_ledges() {
  Graph g;
  Nid   n1 = g.create_node();
  Nid   n2 = g.create_node();
  Nid   n3 = g.create_node();
  Nid   n4 = g.create_node();

  Pid p1 = g.create_pin(n1, 0);
  Pid p2 = g.create_pin(n2, 0);
  Pid p3 = g.create_pin(n3, 0);
  Pid p4 = g.create_pin(n4, 0);

  g.add_edge(p1, p2);
  g.add_edge(p1, p3);
  g.add_edge(p1, p4);
  g.add_edge(p1, n2);
  g.add_edge(p1, n3);
  g.add_edge(p1, n4);

  auto sed = g.ref_pin(p1)->get_edges(p1);
  assert(sed.begin() != sed.end() && "test_sedges_ledges failed: no edges found");
  ankerl::unordered_dense::set<Vid> expected_set = {p2, p3, p4, n2, n3, n4};
  ankerl::unordered_dense::set<Vid> actual_set;
  for (auto it = sed.begin(); it != sed.end(); ++it) {
    actual_set.insert(*it);
  }
  assert(actual_set.size() == expected_set.size() && "test_sedges_ledges failed: size mismatch");
  for (const auto& expected : expected_set) {
    assert(actual_set.count(expected) && "test_sedges_ledges failed: missing expected edge");
  }

  auto sed2 = g.ref_pin(p2)->get_edges(p2);
  assert(sed2.begin() != sed2.end() && "test_sedges_ledges failed: no edges found for p2");
  auto it2 = sed2.begin();
  assert(*it2 == p1 | 2 && "test_sedges_ledges failed: sedges[0] != src for p2");
  ++it2;
  assert(it2 == sed2.end() && "test_sedges_ledges failed: extra edges found for p2");

  auto sed3 = g.ref_pin(p3)->get_edges(p3);
  assert(sed3.begin() != sed3.end() && "test_sedges_ledges failed: no edges found for p3");
  auto it3 = sed3.begin();
  assert(*it3 == p1 | 2 && "test_sedges_ledges failed: sedges[0] != src for p3");
  ++it3;
  assert(it3 == sed3.end() && "test_sedges_ledges failed: extra edges found for p3");

  auto sed4 = g.ref_pin(p4)->get_edges(p4);
  assert(sed4.begin() != sed4.end() && "test_sedges_ledges failed: no edges found for p4");
  auto it4 = sed4.begin();
  assert(*it4 == p1 | 2 && "test_sedges_ledges failed: sedges[0] != src for p4");
  ++it4;
  assert(it4 == sed4.end() && "test_sedges_ledges failed: extra edges found for p4");

  auto sed5 = g.ref_node(n2)->get_edges(n2);
  assert(sed5.begin() != sed5.end() && "test_sedges_ledges failed: no edges found for n2");
  auto it5 = sed5.begin();
  assert(*it5 == p1 | 2 && "test_sedges_ledges failed: sedges[0] != src for n2");
  ++it5;
  assert(it5 == sed5.end() && "test_sedges_ledges failed: extra edges found for n2");

  auto sed6 = g.ref_node(n3)->get_edges(n3);
  assert(sed6.begin() != sed6.end() && "test_sedges_ledges failed: no edges found for n3");
  auto it6 = sed6.begin();
  assert(*it6 == p1 | 2 && "test_sedges_ledges failed: sedges[0] != src for n3");
  ++it6;
  assert(it6 == sed6.end() && "test_sedges_ledges failed: extra edges found for n3");

  auto sed7 = g.ref_node(n4)->get_edges(n4);
  assert(sed7.begin() != sed7.end() && "test_sedges_ledges failed: no edges found for n4");
  auto it7 = sed7.begin();
  assert(*it7 == p1 | 2 && "test_sedges_ledges failed: sedges[0] != src for n4");
  ++it7;
  assert(it7 == sed7.end() && "test_sedges_ledges failed: extra edges found for n4");
  std::cout << "test_sedges_ledges passed\n";
}

void test_overflow_handling() {
  Graph g;
  Nid   n1 = g.create_node();
  Nid   n2 = g.create_node();
  Nid   n3 = g.create_node();
  Nid   n4 = g.create_node();
  Nid   n5 = g.create_node();

  Pid p1 = g.create_pin(n1, 0);
  Pid p2 = g.create_pin(n2, 0);
  Pid p3 = g.create_pin(n3, 0);
  Pid p4 = g.create_pin(n4, 0);

  g.add_edge(p1, p2);
  g.add_edge(p1, p3);
  g.add_edge(p1, p4);
  g.add_edge(p1, n3);
  g.add_edge(p1, n4);
  g.add_edge(p1, n2);
  g.add_edge(p1, n5);  // This will trigger overflow handling

  auto sed = g.ref_pin(p1)->get_edges(p1);
  assert(sed.begin() != sed.end() && "test_overflow_handling failed: no edges found");
  ankerl::unordered_dense::set<Vid> expected_set = {p2, p3, p4, n3, n4, n2, n5};

  ankerl::unordered_dense::set<Vid> actual_set;
  for (auto it = sed.begin(); it != sed.end(); ++it) {
    actual_set.insert(*it);
  }
  assert(actual_set.size() == expected_set.size() && "test_overflow_handling failed: size mismatch");
  for (const auto& expected : expected_set) {
    assert(actual_set.count(expected) && "test_overflow_handling failed: missing expected edge");
  }

  // TODO: Testing other way around
  auto sed2 = g.ref_pin(p2)->get_edges(p2);
  auto it2  = sed2.begin();
  assert(it2 != sed2.end() && "test_overflow_handling failed: no edges found for p2");
  assert(*it2 == p1 | 2 && "test_overflow_handling failed: sedges[0] != src");

  auto sed3 = g.ref_pin(p3)->get_edges(p3);
  auto it3  = sed3.begin();
  assert(it3 != sed3.end() && "test_overflow_handling failed: no edges found for p3");
  assert(*it3 == p1 | 2 && "test_overflow_handling failed: sedges[0] != src");

  auto sed4 = g.ref_pin(p4)->get_edges(p4);
  auto it4  = sed4.begin();
  assert(it4 != sed4.end() && "test_overflow_handling failed: no edges found for p4");
  assert(*it4 == p1 | 2 && "test_overflow_handling failed: sedges[0] != src");

  auto sed5 = g.ref_node(n3)->get_edges(n3);
  auto it5  = sed5.begin();
  assert(it5 != sed5.end() && "test_overflow_handling failed: no edges found for n3");
  assert(*it5 == p1 | 2 && "test_overflow_handling failed: sedges[0] != src");

  auto sed6 = g.ref_node(n4)->get_edges(n4);
  auto it6  = sed6.begin();
  assert(it6 != sed6.end() && "test_overflow_handling failed: no edges found for n4");
  assert(*it6 == p1 | 2 && "test_overflow_handling failed: sedges[0] != src");

  auto sed7 = g.ref_node(n2)->get_edges(n2);
  auto it7  = sed7.begin();
  assert(it7 != sed7.end() && "test_overflow_handling failed: no edges found for n2");
  assert(*it7 == p1 | 2 && "test_overflow_handling failed: sedges[0] != src");

  auto sed8 = g.ref_node(n5)->get_edges(n5);
  auto it8  = sed8.begin();
  assert(it8 != sed8.end() && "test_overflow_handling failed: no edges found for n5");
  assert(*it8 == p1 | 2 && "test_overflow_handling failed: sedges[0] != src");

  // check if the overflow handling is done correctly
  auto p1_ptr = g.ref_pin(p1);
  assert(p1_ptr->check_overflow() == true && "test_overflow_handling failed: use_overflow != true");

  std::cout << "test_overflow_handling passed\n";
}

int main() {
  std::cout << "Running graph tests...\n";
  test_node_to_node();
  test_pin_to_pin();
  test_node_to_pin();
  test_pin_to_node();
  test_sedges_ledges();
  test_overflow_handling();
  std::cout << "All graph tests passed.\n";
  return 0;
}
