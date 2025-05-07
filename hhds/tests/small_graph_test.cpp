#include "graph.hpp"
#include "iassert.hpp"
#include <iostream>

constexpr int NUM_NODES         = 10;
constexpr int NUM_TYPES         = 3;
constexpr int MAX_PINS_PER_NODE = 10;

void test_node_operations() {

    // test creating nodes
    hhds::Node n1 = hhds::Node();
    hhds::Node n2 = hhds::Node(4);

    // Verify IDs and members
    I(n1.get_nid() == 0, "Default node should be 0.");
    I(n1.get_type() == 0, "Default type should be 0.");
    I(n1.get_next_pin_id() == 0, "Default type should be 0.");
    I(n2.get_nid() == 4, "Node should be assigned 4.");

    // Verify setters work
    n1.set_type(1);
    I(n1.get_type() == 1, "Node type should be 1.");
    n1.set_next_pin_id(20);
    I(n1.get_next_pin_id() == 20, "Next node id should be 20");

    // Clear node works
    n1.clear_node();
    I(n1.get_nid() == 0, "Node should be cleared (0).");
}

void test_pin_operations() {
    bool has_edges;
    hhds::Pin p1 = hhds::Pin();
    hhds::Pin p2 = hhds::Pin();
    I(p1.get_master_nid() == 0, "Default master nid for p1 should be 0.");
    has_edges = p1.has_edges();
    I(!has_edges, "Initial pin should not have edges.");
    bool added_edge = p1.add_edge(1, 2);
    I(added_edge, "Adding an edge should always return true");
    has_edges = p1.has_edges();
    I(has_edges, "Pin should have edges after added edge");
    std::array<int32_t, 4>  edges = p1.get_sedges(1);
    std::cout << edges[0] << edges[1] << edges[2] << edges[3] << std::endl;
    std::array<int32_t, 4> expected = {2, 0, 0, 0};
    I(edges == expected, "something");

    // add up to 4 edges then 1 more
    for (int i = 3; i < 6; i++) {
        p1.add_edge(1, i);
    }
    expected = {2, 3, 4, 5};
    I(p1.get_sedges(1) == expected, "sometihng");
    p1.add_edge(1, 6);
    edges = p1.get_sedges(1);
    std::cout << edges[0] << edges[1] << edges[2] << edges[3] << edges[4] << std::endl;
    I(p1.get_sedges(1) == expected, "sometihng");
    /*
    */

}
/*
void test_single_graph_pin() {
    std::vector<hhds::Nid> node_id;
    std::vector<hhds::Pid> pin_id;

    hhds::Graph g1;
    hhds::Nid nid = g1.create_node();
    I(g1.node_table[0] == 0, "First node should be 0");
    I(g1.node_table[1] == 3, "Second node should be 3");
}

*/

void trivial_ops() {
    std::vector<hhds::Nid> node_id;
    std::vector<hhds::Pid> pin_id;

    hhds::Graph g1;
    for (int i = 0; i < NUM_NODES; i++) {
        hhds::Nid nid = g1.create_node();
        I(nid != 0, "Node id should not be 0");
        node_id.push_back(nid);
        g1.ref_node(nid)->set_type(nid % NUM_TYPES);

        int rpins = std::rand() & MAX_PINS_PER_NODE + 1;
        printf("rpins: %d\n", rpins);
        for (int i = 0; i < rpins; i++) {
            hhds::Pid pid = g1.create_pin(nid, 1);
            printf("%ld\n", pid);
            pin_id.push_back(pid);
        }


        // is_invalid() assert ?
        // is_pin() assert?
        // is_node assert?
    }
    g1.add_edge(2, 3);
    g1.add_edge(10, 20);
    g1.add_edge(5, 1);
    g1.add_edge(5, 7);
    // g1.add_edge(4, 100000); //Should not fit for both pins, diff not in range
    g1.add_edge(6, 5);
    g1.add_edge(2, 5);
    g1.add_edge(5, 13);  // Should overflow for 5, because 5 already has 4 sedge
    g1.add_edge(5, 3);   // Should overflow for 5, because 5 already has 4 sedge
    g1.add_edge(5, 4);   // Should overflow for 5, because 5 already has 4 sedge
    g1.add_edge(10, 24);
  
    g1.display_graph();
    //g1.display_next_pin_of_node();
  
}

int main() {
    std::cout <<"\nStarting Node operations unit test...\n\n";
  
    test_node_operations();
    std::cout << "Node operations test passed\n";

    std::cout <<"\nStarting Node operations unit test...\n\n";
  
    test_pin_operations();
    std::cout << "Node operations test passed\n";

    std::cout <<"\nStarting basic graph...\n\n";
  
    trivial_ops();
    std::cout << "Basic graph operations test passed\n";
    return 0;
}