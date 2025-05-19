#include "graph.hpp"

#include <cassert>
#include <iostream>

using namespace hhds;

void test_node_to_node() {
  Graph g;
  Nid   n1  = g.create_node();
  Nid   n2  = g.create_node();
  Vid   src = (static_cast<Vid>(n1) << 2) | (1ULL << 1);
  Vid   dst = (static_cast<Vid>(n2) << 2);
  g.add_edge(src, dst);

  auto sed = g.ref_node(n1)->get_edges(n1);
  assert(sed[0] == n2 && "test_node_to_node failed: sedges[0] != dst");

  auto sed2 = g.ref_node(n2)->get_edges(n2);
  assert(sed2[0] == n1 && "test_node_to_node failed: sedges[0] != src");
  std::cout << "test_node_to_node passed\n";
}

void test_pin_to_pin() {
  Graph g;
  Nid   n  = g.create_node();
  Pid   p1 = g.create_pin(n, 0);
  Pid   p2 = g.create_pin(n, 1);
  // p1, p2 already have bit0=1 (pin); set bit1=1 for driver on p1
  Vid src = p1 << 2;
  Vid dst = p2 << 2;
  src     = src | 3;
  dst     = dst | 1;
  g.add_edge(src, dst);

  auto sed = g.ref_pin(p1)->get_edges(p1);
  assert(sed[0] == p2 && "test_pin_to_pin failed: sedges[0] != dst");

  auto sed2 = g.ref_pin(p2)->get_edges(p2);
  assert(sed2[0] == p1 && "test_pin_to_pin failed: sedges[0] != src");

  std::cout << "test_pin_to_pin passed\n";
}

void test_node_to_pin() {
  Graph g;
  Nid   n = g.create_node();
  Pid   p = g.create_pin(n, 0);
  // node source: bit0=0, bit1=1; pin target p already has bit0=1, we leave bit1=0 indicating sink
  Vid src = n << 2;
  Vid dst = p << 2;

  src = src | 2;
  dst = dst | 1;
  g.add_edge(src, dst);

  auto sed = g.ref_node(n)->get_edges(n);
  assert(sed[0] == p && "test_node_to_pin failed: sedges[0] != dst");

  auto sed2 = g.ref_pin(p)->get_edges(p);
  assert(sed2[0] == n && "test_node_to_pin failed: sedges[0] != src");
  std::cout << "test_node_to_pin passed\n";
}

void test_pin_to_node() {
  Graph g;
  Nid   n = g.create_node();
  Pid   p = g.create_pin(n, 0);
  // pin source: bit0=1, set bit1=1 for driver; node target: bit0=0, bit1=0
  Vid src = p << 2;
  src     = src | 3;
  Vid dst = n << 2;
  dst     = dst | 0;
  g.add_edge(src, dst);

  auto sed = g.ref_pin(p)->get_edges(p);
  assert(sed[0] == n && "test_pin_to_node failed: sedges[0] != dst");

  auto sed2 = g.ref_node(n)->get_edges(n);
  assert(sed2[0] == p && "test_pin_to_node failed: sedges[0] != src");

  std::cout << "test_pin_to_node passed\n";
}

void test_overflow_handling() {
  Graph g;
  Nid   n1 = g.create_node();
  Nid   n2 = g.create_node();
  Nid   n3 = g.create_node();
  Nid   n4 = g.create_node();
  Nid   n5 = g.create_node();

  Pid p1 = g.create_pin(n1, 0);
  Pid p2 = g.create_pin(n2, 1);
  Pid p3 = g.create_pin(n3, 0);
  Pid p4 = g.create_pin(n4, 1);

  // first edge
  Vid src  = p1 << 2;
  Vid dst  = p2 << 2;
  src      = src | 3;
  dst      = dst | 1;
  Vid dst1 = dst;
  g.add_edge(src, dst);

  // second edge
  src      = p1 << 2;
  dst      = p3 << 2;
  src      = src | 3;
  dst      = dst | 1;
  Vid dst2 = dst;
  g.add_edge(src, dst);

  // third edge
  src      = p1 << 2;
  dst      = p4 << 2;
  src      = src | 3;
  dst      = dst | 1;
  Vid dst3 = dst;
  g.add_edge(src, dst);

  // fourth edge
  src      = p1 << 2;
  dst      = n3 << 2;
  src      = src | 3;
  dst      = dst | 0;
  Vid dst4 = dst;
  g.add_edge(src, dst);

  // fifth edge
  src      = p1 << 2;
  dst      = n4 << 2;
  src      = src | 3;
  dst      = dst | 0;
  Vid dst5 = dst;
  g.add_edge(src, dst);

  // sixth edge
  src      = p1 << 2;
  dst      = n2 << 2;
  src      = src | 3;
  dst      = dst | 0;
  Vid dst6 = dst;
  g.add_edge(src, dst);

  auto sed = g.ref_pin(p1)->get_edges(p1);
  assert(sed[0] == p2 && "test_overflow_handling failed: sedges[0] != p2");
  assert(sed[1] == p3 && "test_overflow_handling failed: sedges[1] != p3");
  assert(sed[2] == p4 && "test_overflow_handling failed: sedges[2] != p4");
  assert(sed[3] == n3 && "test_overflow_handling failed: sedges[3] != n3");
  assert(sed[4] == dst5 && "test_overflow_handling failed: sedges[4] != dst4");
  assert(sed[5] == dst6 && "test_overflow_handling failed: sedges[5] != dst5");

  auto sed2 = g.ref_pin(p2)->get_edges(p2);
  assert(sed2[0] == p1 && "test_overflow_handling failed: sedges[0] != src");

  auto sed3 = g.ref_pin(p3)->get_edges(p3);
  assert(sed3[0] == p1 && "test_overflow_handling failed: sedges[0] != src");

  auto sed4 = g.ref_pin(p4)->get_edges(p4);
  assert(sed4[0] == p1 && "test_overflow_handling failed: sedges[0] != src");

  auto sed5 = g.ref_node(n3)->get_edges(n3);
  assert(sed5[0] == p1 && "test_overflow_handling failed: sedges[0] != src");

  auto sed6 = g.ref_node(n4)->get_edges(n4);
  assert(sed6[0] == p1 && "test_overflow_handling failed: sedges[0] != src");

  auto sed7 = g.ref_node(n2)->get_edges(n2);
  assert(sed7[0] == p1 && "test_overflow_handling failed: sedges[0] != src");

  // seventh edge
  src      = p1 << 2;
  dst      = n5 << 2;
  src      = src | 3;
  dst      = dst | 0;
  Vid dst7 = dst;
  g.add_edge(src, dst);
  Pin* tempP1 = g.ref_pin(p1);

  // check if the overflow handling is done correctly
  assert(tempP1->check_overflow() == true && "test_overflow_handling failed: use_overflow != true");
  sed                       = g.ref_pin(p1)->get_edges(p1);
  std::vector<Vid> expected = {dst1, dst2, dst3, dst4, dst5, dst6, dst7};
  std::sort(expected.begin(), expected.end());
  assert(sed[0] == expected[0] && "test_overflow_handling failed: sedges[0] != dst1");
  assert(sed[1] == expected[1] && "test_overflow_handling failed: sedges[1] != dst2");
  assert(sed[2] == expected[2] && "test_overflow_handling failed: sedges[2] != dst3");
  assert(sed[3] == expected[3] && "test_overflow_handling failed: sedges[3] != dst4");
  assert(sed[4] == expected[4] && "test_overflow_handling failed: sedges[4] != dst5");
  assert(sed[5] == expected[5] && "test_overflow_handling failed: sedges[5] != dst6");
  assert(sed[6] == expected[6] && "test_overflow_handling failed: sedges[6] != dst7");

  auto sed8 = g.ref_node(n5)->get_edges(n5);
  assert(sed8[0] == p1 && "test_overflow_handling failed: sedges[0] != src");

  std::cout << "test_overflow_handling passed\n";
}

int main() {
  std::cout << "Running graph tests...\n";
  test_node_to_node();
  test_pin_to_pin();
  test_node_to_pin();
  test_pin_to_node();
  test_overflow_handling();
  std::cout << "All graph tests passed.\n";
  return 0;
}
