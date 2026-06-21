#include "hhds/graph.hpp"

#include <cassert>
#include <iostream>
#include <vector>
#include <utility>

using namespace hhds;

static Nid node_of(Nid nid) { return nid & ~static_cast<Nid>(3); }

static Node_class find_hier_node(Graph* top, Gid gid, Nid nid) {
  Node_class found;
  bool        ok   = false;
  const Nid   want = node_of(nid);
  for (auto n : top->forward_hier()) {
    if (n.get_current_gid() == gid && node_of(n.get_debug_nid()) == want) {
      found = n;
      ok    = true;
    }
  }
  assert(ok);
  return found;
}

// FAN-OUT DOWN: a top driver feeds an instance input that fans out to TWO
// internal sinks inside the leaf. out_edges from top's src driver must surface
// BOTH leaf sinks.
static void probe_fanout_down() {
  GraphLibrary lib;
  auto leaf_io = lib.create_io("leafFO");
  leaf_io->add_input("a", 1);
  auto leaf = leaf_io->create_graph();
  auto b1   = leaf->create_node();
  auto b2   = leaf->create_node();
  leaf->get_input_pin("a").connect_sink(b1.create_sink_pin());
  leaf->get_input_pin("a").connect_sink(b2.create_sink_pin());

  auto top_io = lib.create_io("topFO");
  auto top    = top_io->create_graph();
  auto src    = top->create_node();
  auto r      = top->create_node();
  r.set_subnode(leaf_io);
  src.create_driver_pin().connect_sink(r.create_sink_pin(1));  // src -> r.a

  // find src in hier context
  Node_class src_h;
  bool ok = false;
  for (auto n : top->forward_hier()) {
    if (n.get_current_gid() == top->get_gid() && node_of(n.get_debug_nid()) == node_of(src.get_debug_nid())) {
      src_h = n; ok = true;
    }
  }
  assert(ok);

  auto outs = src_h.out_edges();
  std::cout << "probe_fanout_down: out_edges count = " << outs.size() << " (expect 2)\n";
  std::vector<Nid> sinks;
  for (auto& e : outs) sinks.push_back(node_of(e.sink.get_master_node().get_debug_nid()));
  for (auto s : sinks) std::cout << "   sink master nid=" << s
       << " (b1=" << node_of(b1.get_debug_nid()) << " b2=" << node_of(b2.get_debug_nid()) << ")\n";
}

// FAN-OUT through an instance: bufA inside leafA drives leafA.y, instance ra.y
// fans out at the TOP level to two sibling instance inputs. out_edges from bufA
// (hier) must surface BOTH downstream leaves.
static void probe_fanout_up_then_down() {
  GraphLibrary lib;
  auto leafA_io = lib.create_io("leafA_FO");
  leafA_io->add_input("a", 1);
  leafA_io->add_output("y", 2);
  auto leafA = leafA_io->create_graph();
  auto bufA  = leafA->create_node();
  leafA->get_input_pin("a").connect_sink(bufA.create_sink_pin());
  bufA.create_driver_pin().connect_sink(leafA->get_output_pin("y"));

  auto leafB_io = lib.create_io("leafB_FO");
  leafB_io->add_input("a", 1);
  auto leafB = leafB_io->create_graph();
  auto bufB  = leafB->create_node();
  leafB->get_input_pin("a").connect_sink(bufB.create_sink_pin());

  auto leafC_io = lib.create_io("leafC_FO");
  leafC_io->add_input("a", 1);
  auto leafC = leafC_io->create_graph();
  auto bufC  = leafC->create_node();
  leafC->get_input_pin("a").connect_sink(bufC.create_sink_pin());

  auto top_io = lib.create_io("topFO2");
  auto top    = top_io->create_graph();
  auto ra = top->create_node(); ra.set_subnode(leafA_io);
  auto rb = top->create_node(); rb.set_subnode(leafB_io);
  auto rc = top->create_node(); rc.set_subnode(leafC_io);
  ra.create_driver_pin(2).connect_sink(rb.create_sink_pin(1));  // ra.y -> rb.a
  ra.create_driver_pin(2).connect_sink(rc.create_sink_pin(1));  // ra.y -> rc.a

  auto bufA_h = find_hier_node(top.get(), leafA->get_gid(), bufA.get_debug_nid());
  auto outs = bufA_h.out_edges();
  std::cout << "probe_fanout_up_then_down: out_edges count = " << outs.size() << " (expect 2: bufB, bufC)\n";
  for (auto& e : outs) {
    std::cout << "   sink gid=" << e.sink.get_current_gid()
              << " master nid=" << node_of(e.sink.get_master_node().get_debug_nid()) << "\n";
  }
  std::cout << "   bufB gid=" << leafB->get_gid() << " nid=" << node_of(bufB.get_debug_nid())
            << " | bufC gid=" << leafC->get_gid() << " nid=" << node_of(bufC.get_debug_nid()) << "\n";
}

// MULTI-DRIVER sink (illegal-ish but possible): leaf input "a" driven by two
// top drivers. inp_edges from buf (hier) should surface BOTH.
static void probe_multidriver() {
  GraphLibrary lib;
  auto leaf_io = lib.create_io("leafMD");
  leaf_io->add_input("a", 1);
  auto leaf = leaf_io->create_graph();
  auto buf  = leaf->create_node();
  leaf->get_input_pin("a").connect_sink(buf.create_sink_pin());

  auto top_io = lib.create_io("topMD");
  auto top    = top_io->create_graph();
  auto s1 = top->create_node();
  auto s2 = top->create_node();
  auto r  = top->create_node(); r.set_subnode(leaf_io);
  s1.create_driver_pin().connect_sink(r.create_sink_pin(1));
  s2.create_driver_pin().connect_sink(r.create_sink_pin(1));

  auto buf_h = find_hier_node(top.get(), leaf->get_gid(), buf.get_debug_nid());
  auto ins = buf_h.inp_edges();
  std::cout << "probe_multidriver: inp_edges count = " << ins.size() << " (expect 2: s1,s2)\n";
}

// DANGLING: leaf input "a" not connected at top. inp_edges should be... what?
static void probe_dangling_up() {
  GraphLibrary lib;
  auto leaf_io = lib.create_io("leafDA");
  leaf_io->add_input("a", 1);
  auto leaf = leaf_io->create_graph();
  auto buf  = leaf->create_node();
  leaf->get_input_pin("a").connect_sink(buf.create_sink_pin());

  auto top_io = lib.create_io("topDA");
  auto top    = top_io->create_graph();
  auto r  = top->create_node(); r.set_subnode(leaf_io);
  (void)r;

  auto buf_h = find_hier_node(top.get(), leaf->get_gid(), buf.get_debug_nid());
  auto ins = buf_h.inp_edges();
  std::cout << "probe_dangling_up: inp_edges count = " << ins.size() << " (instance input unconnected)\n";
  // local view:
  auto ins_local = buf.inp_edges();
  std::cout << "   local inp_edges count = " << ins_local.size() << "\n";
}

// Querying an INSTANCE node itself (is_hier instance with subnode) in hier ctx.
static void probe_query_instance_node() {
  GraphLibrary lib;
  auto leaf_io = lib.create_io("leafQI");
  leaf_io->add_input("a", 1);
  leaf_io->add_output("y", 2);
  auto leaf = leaf_io->create_graph();
  auto buf  = leaf->create_node();
  leaf->get_input_pin("a").connect_sink(buf.create_sink_pin());
  buf.create_driver_pin().connect_sink(leaf->get_output_pin("y"));

  auto top_io = lib.create_io("topQI");
  auto top    = top_io->create_graph();
  auto src = top->create_node();
  auto r   = top->create_node(); r.set_subnode(leaf_io);
  auto dst = top->create_node();
  src.create_driver_pin().connect_sink(r.create_sink_pin(1));
  r.create_driver_pin(2).connect_sink(dst.create_sink_pin());

  // Find the instance node r in hier context (root graph = top)
  Node_class r_h;
  bool ok=false;
  for (auto n : top->forward_hier()) {
    if (n.get_current_gid() == top->get_gid() && node_of(n.get_debug_nid()) == node_of(r.get_debug_nid())) {
      r_h = n; ok = true;
    }
  }
  assert(ok);
  std::cout << "probe_query_instance_node: r is_hier=" << r_h.is_hier() << " has_subnode\n";
  auto ins = r_h.inp_edges();
  std::cout << "   r.inp_edges count = " << ins.size() << "\n";
  for (auto& e : ins)
    std::cout << "     driver gid=" << e.driver.get_current_gid()
              << " master=" << node_of(e.driver.get_master_node().get_debug_nid())
              << " (src=" << node_of(src.get_debug_nid()) << ")\n";
  auto outs = r_h.out_edges();
  std::cout << "   r.out_edges count = " << outs.size() << "\n";
  for (auto& e : outs)
    std::cout << "     sink gid=" << e.sink.get_current_gid()
              << " master=" << node_of(e.sink.get_master_node().get_debug_nid())
              << " (dst=" << node_of(dst.get_debug_nid())
              << " buf=" << node_of(buf.get_debug_nid()) << ")\n";
}

// CONST driver crossing up: top has a const feeding instance input.
static void probe_const_driver() {
  GraphLibrary lib;
  auto leaf_io = lib.create_io("leafCD");
  leaf_io->add_input("a", 1);
  auto leaf = leaf_io->create_graph();
  auto buf  = leaf->create_node();
  leaf->get_input_pin("a").connect_sink(buf.create_sink_pin());

  auto top_io = lib.create_io("topCD");
  auto top    = top_io->create_graph();
  auto r  = top->create_node(); r.set_subnode(leaf_io);
  auto k  = top->create_constant();   // CONST_NODE driver pin
  k.connect_sink(r.create_sink_pin(1));

  auto buf_h = find_hier_node(top.get(), leaf->get_gid(), buf.get_debug_nid());
  auto ins = buf_h.inp_edges();
  std::cout << "probe_const_driver: inp_edges count = " << ins.size() << " (expect 1: CONST)\n";
  for (auto& e : ins)
    std::cout << "   driver gid=" << e.driver.get_current_gid()
              << " master=" << node_of(e.driver.get_master_node().get_debug_nid())
              << " is_driver=" << e.driver.is_driver() << " (CONST_NODE=12)\n";
}

// A cycle THROUGH a boundary: leaf passes a -> y straight through; top wires
// r.y back to r.a, forming a pure combinational hier loop. Does the resolver
// terminate (depth cap) or hang?
static void probe_boundary_cycle() {
  GraphLibrary lib;
  auto leaf_io = lib.create_io("leafCY");
  leaf_io->add_input("a", 1);
  leaf_io->add_output("y", 2);
  auto leaf = leaf_io->create_graph();
  auto buf  = leaf->create_node();
  leaf->get_input_pin("a").connect_sink(buf.create_sink_pin());
  buf.create_driver_pin().connect_sink(leaf->get_output_pin("y"));

  auto top_io = lib.create_io("topCY");
  auto top    = top_io->create_graph();
  auto r  = top->create_node(); r.set_subnode(leaf_io);
  r.create_driver_pin(2).connect_sink(r.create_sink_pin(1));  // r.y -> r.a  (cycle)

  auto buf_h = find_hier_node(top.get(), leaf->get_gid(), buf.get_debug_nid());
  std::cout << "probe_boundary_cycle: calling inp_edges (depth cap should fire)...\n";
  auto ins = buf_h.inp_edges();
  std::cout << "   inp_edges count = " << ins.size() << "\n";
}

// TRUE pass-through cycle: leaf wires input a directly to output y (no node),
// top wires r.y -> r.a. Now there is NO real leaf anywhere in the loop.
static void probe_passthrough_cycle() {
  GraphLibrary lib;
  auto leaf_io = lib.create_io("leafPT");
  leaf_io->add_input("a", 1);
  leaf_io->add_output("y", 2);
  auto leaf = leaf_io->create_graph();
  // direct passthrough: input pin a drives output pin y
  leaf->get_input_pin("a").connect_sink(leaf->get_output_pin("y"));

  auto top_io = lib.create_io("topPT");
  auto top    = top_io->create_graph();
  auto r  = top->create_node(); r.set_subnode(leaf_io);
  auto snk = top->create_node();
  r.create_driver_pin(2).connect_sink(r.create_sink_pin(1));  // r.y -> r.a (cycle)
  r.create_driver_pin(2).connect_sink(snk.create_sink_pin()); // also r.y -> snk (observable)

  // Query snk.inp_edges in hier ctx: driver is r.y -> down into leaf OUTPUT ->
  // driven by leaf INPUT a -> up to r.a -> driven by r.y -> ... cycle.
  Node_class snk_h; bool ok=false;
  for (auto n : top->forward_hier())
    if (n.get_current_gid()==top->get_gid() && node_of(n.get_debug_nid())==node_of(snk.get_debug_nid())) { snk_h=n; ok=true; }
  assert(ok);
  std::cout << "probe_passthrough_cycle: calling snk.inp_edges (true cycle, depth cap)...\n";
  auto ins = snk_h.inp_edges();
  std::cout << "   inp_edges count = " << ins.size() << " (cycle -> likely 0, edge silently dropped)\n";
}

// PORT 0 boundary: instance connected through port 0 (node-as-pin) on both
// the instance side and the leaf IO side. Does find_pin_or_zero / resolution
// handle the node-as-pin (port 0) path?
static void probe_port0_boundary() {
  GraphLibrary lib;
  auto leaf_io = lib.create_io("leafP0");
  leaf_io->add_input("a", 0);   // PORT 0 input
  leaf_io->add_output("y", 0);  // PORT 0 output
  auto leaf = leaf_io->create_graph();
  auto buf  = leaf->create_node();
  leaf->get_input_pin("a").connect_sink(buf.create_sink_pin());
  buf.create_driver_pin().connect_sink(leaf->get_output_pin("y"));

  auto top_io = lib.create_io("topP0");
  auto top    = top_io->create_graph();
  auto src = top->create_node();
  auto r   = top->create_node(); r.set_subnode(leaf_io);
  auto dst = top->create_node();
  src.create_driver_pin().connect_sink(r.create_sink_pin(0));   // src -> r.a(port0)
  r.create_driver_pin(0).connect_sink(dst.create_sink_pin());   // r.y(port0) -> dst

  auto buf_h = find_hier_node(top.get(), leaf->get_gid(), buf.get_debug_nid());
  auto ins = buf_h.inp_edges();
  std::cout << "probe_port0_boundary: inp_edges count = " << ins.size() << " (expect 1: src)\n";
  for (auto& e : ins)
    std::cout << "   driver master=" << node_of(e.driver.get_master_node().get_debug_nid())
              << " (src=" << node_of(src.get_debug_nid()) << ")\n";
  auto outs = buf_h.out_edges();
  std::cout << "   out_edges count = " << outs.size() << " (expect 1: dst)\n";
  for (auto& e : outs)
    std::cout << "   sink master=" << node_of(e.sink.get_master_node().get_debug_nid())
              << " (dst=" << node_of(dst.get_debug_nid()) << ")\n";
}

// AMBIGUOUS hier_pos: the SAME leaf gid is instantiated inside TWO different
// wrapper modules P1 and P2. In each wrapper, the leaf instance is the first
// subnode, so it gets the SAME per-graph tree_pos. Both wrappers are placed in
// top. The leaf passes a->y. Top feeds P1.a from srcA and reads P1.y into dstA;
// P2.a from srcB, P2.y into dstB. From the leaf reached via P1, inp_edges must
// resolve to srcA (NOT srcB). If reconstruct_hier_path matches by (gid,tree_pos)
// only, it may pick the P2 chain.
static void probe_ambiguous_hier_pos() {
  GraphLibrary lib;
  auto leaf_io = lib.create_io("leafAMB");
  leaf_io->add_input("a", 1);
  leaf_io->add_output("y", 2);
  auto leaf = leaf_io->create_graph();
  auto buf  = leaf->create_node();
  leaf->get_input_pin("a").connect_sink(buf.create_sink_pin());
  buf.create_driver_pin().connect_sink(leaf->get_output_pin("y"));

  // wrapper P1: a -> li.a ; li.y -> y
  auto p1_io = lib.create_io("p1AMB");
  p1_io->add_input("a", 1);
  p1_io->add_output("y", 2);
  auto p1 = p1_io->create_graph();
  auto li1 = p1->create_node(); li1.set_subnode(leaf_io);  // first subnode in p1
  p1->get_input_pin("a").connect_sink(li1.create_sink_pin(1));
  li1.create_driver_pin(2).connect_sink(p1->get_output_pin("y"));

  // wrapper P2: identical shape
  auto p2_io = lib.create_io("p2AMB");
  p2_io->add_input("a", 1);
  p2_io->add_output("y", 2);
  auto p2 = p2_io->create_graph();
  auto li2 = p2->create_node(); li2.set_subnode(leaf_io);  // first subnode in p2
  p2->get_input_pin("a").connect_sink(li2.create_sink_pin(1));
  li2.create_driver_pin(2).connect_sink(p2->get_output_pin("y"));

  auto top_io = lib.create_io("topAMB");
  auto top    = top_io->create_graph();
  auto srcA = top->create_node();
  auto srcB = top->create_node();
  auto rp1  = top->create_node(); rp1.set_subnode(p1_io);
  auto rp2  = top->create_node(); rp2.set_subnode(p2_io);
  auto dstA = top->create_node();
  auto dstB = top->create_node();
  srcA.create_driver_pin().connect_sink(rp1.create_sink_pin(1));  // srcA -> P1.a
  rp1.create_driver_pin(2).connect_sink(dstA.create_sink_pin());  // P1.y -> dstA
  srcB.create_driver_pin().connect_sink(rp2.create_sink_pin(1));  // srcB -> P2.a
  rp2.create_driver_pin(2).connect_sink(dstB.create_sink_pin());  // P2.y -> dstB

  std::cout << "probe_ambiguous_hier_pos: li1.tree_pos vs li2.tree_pos -- both first subnode in their wrapper\n";
  // Walk every visit of the leaf buf node and check its resolution.
  int idx = 0;
  for (auto n : top->forward_hier()) {
    if (n.get_current_gid() == leaf->get_gid() && node_of(n.get_debug_nid()) == node_of(buf.get_debug_nid())) {
      auto ins = n.inp_edges();
      std::cout << "  leaf-buf visit #" << idx << " hier_pos=" << n.get_hier_pos()
                << " inp_edges=" << ins.size();
      for (auto& e : ins)
        std::cout << " -> driver master=" << node_of(e.driver.get_master_node().get_debug_nid());
      std::cout << "  (srcA=" << node_of(srcA.get_debug_nid())
                << " srcB=" << node_of(srcB.get_debug_nid()) << ")\n";
      auto outs = n.out_edges();
      std::cout << "      out_edges=" << outs.size();
      for (auto& e : outs)
        std::cout << " -> sink master=" << node_of(e.sink.get_master_node().get_debug_nid());
      std::cout << "  (dstA=" << node_of(dstA.get_debug_nid())
                << " dstB=" << node_of(dstB.get_debug_nid()) << ")\n";
      ++idx;
    }
  }
}

// Query the TOP graph's INPUT_NODE / OUTPUT_NODE directly in hier ctx. These
// are IO nodes (not has_subnode). out_edges of the top input node should report
// its real sinks. Make sure resolve doesn't misclassify.
static void probe_query_top_io_node() {
  GraphLibrary lib;
  auto leaf_io = lib.create_io("leafTI");
  leaf_io->add_input("a", 1);
  auto leaf = leaf_io->create_graph();
  auto buf  = leaf->create_node();
  leaf->get_input_pin("a").connect_sink(buf.create_sink_pin());

  auto top_io = lib.create_io("topTI");
  top_io->add_input("pi", 1);
  auto top = top_io->create_graph();
  auto r   = top->create_node(); r.set_subnode(leaf_io);
  top->get_input_pin("pi").connect_sink(r.create_sink_pin(1));  // top.pi -> r.a

  // get top input node in hier ctx via forward_hier? IO nodes aren't yielded.
  // Construct hier handle directly: root=top, hier_pos=ROOT.
  auto in_node = top->get_input_node();  // class ctx
  std::cout << "probe_query_top_io_node: input node is_hier=" << in_node.is_hier() << "\n";
  auto outs_local = top->get_input_node().out_edges();
  std::cout << "   top input out_edges (class ctx) = " << outs_local.size()
            << " (local; should be 1: r.a). NOTE: class ctx, not hier.\n";
  for (auto& e : outs_local)
    std::cout << "      sink master=" << node_of(e.sink.get_master_node().get_debug_nid())
              << " has_subnode-instance? r=" << node_of(r.get_debug_nid()) << "\n";
}

int main() {
  probe_query_top_io_node();
  probe_ambiguous_hier_pos();
  probe_port0_boundary();
  probe_passthrough_cycle();
  probe_fanout_down();
  probe_fanout_up_then_down();
  probe_multidriver();
  probe_dangling_up();
  probe_query_instance_node();
  probe_const_driver();
  probe_boundary_cycle();
  std::cout << "probe done\n";
  return 0;
}
