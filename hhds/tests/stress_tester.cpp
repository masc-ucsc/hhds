#include <iostream>
#include <vector>
#include <random>
#include <fstream>
#include <queue>
#include <TreeDS/tree>
#include "../tree.hpp"
#include "../lhtree.hpp"
#include "testlib.h" // Globally included on the device

void generate_random_tree(int n, int t, std::vector<std::pair<int, int>>& edges) {
    std::vector<int> p(n);
    for(int i = 0; i < n; ++i)
        if (i > 0)
            p[i] = rnd.wnext(i, t);

    std::vector<int> perm(n);
    for(int i = 0; i < n; ++i)
        perm[i] = i;
    shuffle(perm.begin() + 1, perm.end());

    for (int i = 1; i < n; i++) {
        if (rnd.next(2))
            edges.push_back(std::make_pair(perm[i], perm[p[i]]));
        else
            edges.push_back(std::make_pair(perm[p[i]], perm[i]));
    }

    shuffle(edges.begin(), edges.end());
}

void bfs_traversal_hhds(hhds::tree<int>& tree, hhds::Tree_pos root, std::vector<int>& result) {
    std::queue<hhds::Tree_pos> q; q.push(root);
    while (!q.empty()) {
        hhds::Tree_pos node = q.front(); q.pop();
        result.push_back(tree[node]);

        for (hhds::Tree_pos child = tree.get_first_child(node); 
             child != hhds::INVALID; 
             child = tree.get_sibling_next(child)) {

            q.push(child);
        }
    }
}

void bfs_traversal_lhtree(lh::tree<int>& tree, lh::Tree_index root, std::vector<int>& result) {
    std::queue<lh::Tree_index> q;
    q.push(root);
    while (!q.empty()) {
        lh::Tree_index node = q.front();
        q.pop();
        result.push_back(tree.get_data(node));

        for (lh::Tree_index child = tree.get_first_child(node); !child.is_invalid(); child = tree.get_sibling_next(child)) {
            q.push(child);
        }
    }
}

int main(int argv, char* argc[]) {
    registerGen(argv, argc, 1);

    const int num_tests = 1000;
    const int num_vertices = 1000000;
    const int t = 0;

    for (int test = 0; test < num_tests; ++test) {
        std::vector<std::pair<int, int>> edges;
        generate_random_tree(num_vertices, t, edges);

        hhds::tree<int> hhds_tree;
        lh::tree<int> lh_tree;

        std::default_random_engine generator(test);
        std::uniform_int_distribution<int> dataDist(1, 100);

        std::vector<hhds::Tree_pos> hhds_nodes(num_vertices);
        std::vector<lh::Tree_index> lh_nodes(num_vertices);

        hhds_nodes[0] = hhds_tree.add_root(dataDist(generator));
        lh::Tree_index root_index = lh::Tree_index(0, 0); // Assuming the root is at level 0, position 0
        lh_tree.set_root(dataDist(generator));
        lh_nodes[0] = root_index;

        for (const auto& edge : edges) {
            int parent = edge.first;
            int child = edge.second;
            hhds_nodes[child] = hhds_tree.add_child(hhds_nodes[parent], dataDist(generator));
            lh_nodes[child] = lh_tree.add_child(lh_nodes[parent], dataDist(generator));
        }

        std::vector<int> hhds_result;
        std::vector<int> lh_result;

        bfs_traversal_hhds(hhds_tree, hhds_nodes[0], hhds_result);
        bfs_traversal_lhtree(lh_tree, lh_nodes[0], lh_result);

        if (hhds_result != lh_result) {
            std::ofstream out("failing_test_case.txt");
            out << num_vertices << " " << t << std::endl;
            for (auto& edge : edges) {
                out << edge.first + 1 << " " << edge.second + 1 << std::endl;
            }
            out.close();
            std::cerr << "Test case " << test << " failed. Written to failing_test_case.txt" << std::endl;
            break;
        }
    }

    return 0;
}
