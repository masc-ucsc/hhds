#include "graph.hpp"
#include "iassert.hpp"
#include <iostream>

constexpr int NUM_NODES         = 10;
constexpr int NUM_TYPES         = 3;
constexpr int MAX_PINS_PER_NODE = 10;

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

    std::cout <<"\nStarting basic graph...\n\n";
  
    trivial_ops();
    std::cout << "Basic graph operations test passed\n";
    return 0;
}