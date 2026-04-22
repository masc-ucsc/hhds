#pragma once

// tree_edit_distance.hpp
// Zhang-Shasha tree edit distance algorithm for HHDS trees.
//
// Computes the minimum-cost sequence of insert, delete, and relabel operations
// that transforms one labeled tree into another.
//
// Reference: K. Zhang and D. Shasha, "Simple Fast Algorithms for the Editing
//            Distance between Trees and Related Problems", SIAM J. Comput. 1989.

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <functional>
#include <limits>
#include <unordered_map>
#include <vector>

#include "hhds/tree.hpp"

namespace hhds {

struct EditCosts {
  double insert  = 1.0;  // cost to insert a node (add to T1 to match T2)
  double del     = 1.0;  // cost to delete a node (remove from T1)
  double relabel = 1.0;  // cost to relabel a node (change type)
};

class TreeEditDistance {
public:
  struct Result {
    double distance = 0.0;
  };

  [[nodiscard]] static Result compute(const Tree::Node_class& root1, const Tree::Node_class& root2,
                                      const EditCosts&                                                          costs = EditCosts{},
                                      ::std::function<double(const Tree::Node_class&, const Tree::Node_class&)> cost_fn = nullptr) {
    TreeEditDistance ted(costs, ::std::move(cost_fn));
    return Result{ted._run(root1, root2)};
  }
// Null or empty trees are treated as empty (zero nodes).
// distance(empty, empty) = 0
// distance(empty, T)     = |T| * insert_cost
// distance(T, empty)     = |T| * delete_cost
  [[nodiscard]] static Result compute(const ::std::shared_ptr<Tree>& tree1, const ::std::shared_ptr<Tree>& tree2,
                                      const EditCosts&                                                          costs = EditCosts{},
                                      ::std::function<double(const Tree::Node_class&, const Tree::Node_class&)> cost_fn = nullptr) {
    Tree::Node_class root1 = tree1 ? tree1->get_root_node() : Tree::Node_class();
    Tree::Node_class root2 = tree2 ? tree2->get_root_node() : Tree::Node_class();
    return compute(root1, root2, costs, ::std::move(cost_fn));
  }

private:
  EditCosts                                                                 costs_;
  ::std::function<double(const Tree::Node_class&, const Tree::Node_class&)> cost_fn_;

  struct TreeData {
    ::std::vector<Tree::Node_class> nodes;
    ::std::vector<int>              l;
    // keyroots: sorted list of post-order indices that are keyroots.
    // A keyroot is the largest post-order index among all nodes sharing
    // the same leftmost-leaf value.
    ::std::vector<int> keyroots;
  };

  explicit TreeEditDistance(const EditCosts&                                                          costs,
                            ::std::function<double(const Tree::Node_class&, const Tree::Node_class&)> cost_fn)
      : costs_(costs), cost_fn_(::std::move(cost_fn)) {}

  [[nodiscard]] double _node_cost(const Tree::Node_class& a, const Tree::Node_class& b) const {
    if (cost_fn_) {
      return cost_fn_(a, b);
    }
    return (a.get_type() == b.get_type()) ? 0.0 : costs_.relabel;
  }

  static TreeData _build(const Tree::Node_class& root) {
    TreeData data;
    data.nodes.emplace_back();  // sentinel at [0]
    data.l.push_back(0);        // sentinel at [0]

    ::std::unordered_map<Tree_pos, int> pos_to_idx;

    for (auto node : root.post_order_class()) {
      const int idx                    = static_cast<int>(data.nodes.size());
      pos_to_idx[node.get_debug_nid()] = idx;

      int  lval  = idx;  // assume leaf
      auto first = node.first_child();
      if (first.is_valid()) {
        auto it = pos_to_idx.find(first.get_debug_nid());
        assert(it != pos_to_idx.end() && "post-order invariant violated");
        lval = data.l[it->second];
      }

      data.nodes.push_back(node);
      data.l.push_back(lval);
    }

    const int n = static_cast<int>(data.nodes.size()) - 1;

    ::std::unordered_map<int, int> lval_to_max_i;
    for (int i = 1; i <= n; ++i) {
      lval_to_max_i[data.l[i]] = i;
    }
    data.keyroots.reserve(lval_to_max_i.size());
    for (auto& [lval, i] : lval_to_max_i) {
      data.keyroots.push_back(i);
    }
    ::std::sort(data.keyroots.begin(), data.keyroots.end());

    return data;
  }

  double _run(const Tree::Node_class& root1, const Tree::Node_class& root2) {
    if (root1.is_invalid() && root2.is_invalid()) {
      return 0.0;
    }
    if (root1.is_invalid()) {
      int cnt = 0;
      for ([[maybe_unused]] auto n : root2.post_order_class()) {
        ++cnt;
      }
      return cnt * costs_.insert;
    }
    if (root2.is_invalid()) {
      int cnt = 0;
      for ([[maybe_unused]] auto n : root1.post_order_class()) {
        ++cnt;
      }
      return cnt * costs_.del;
    }

    const TreeData d1 = _build(root1);
    const TreeData d2 = _build(root2);

    const int n = static_cast<int>(d1.nodes.size()) - 1;
    const int m = static_cast<int>(d2.nodes.size()) - 1;

    ::std::vector<::std::vector<double>> td(n + 1, ::std::vector<double>(m + 1, 0.0));

    ::std::vector<::std::vector<double>> fd(n + 1, ::std::vector<double>(m + 1, 0.0));

    for (const int i : d1.keyroots) {
      for (const int j : d2.keyroots) {
        _forest_dist(d1, d2, i, j, td, fd);
      }
    }

    return td[n][m];
  }

  void _forest_dist(const TreeData& d1, const TreeData& d2, int i, int j, ::std::vector<::std::vector<double>>& td,
                    ::std::vector<::std::vector<double>>& fd) {
    const int l1 = d1.l[i];
    const int l2 = d2.l[j];

    fd[l1 - 1][l2 - 1] = 0.0;
    for (int i2 = l1; i2 <= i; ++i2) {
      fd[i2][l2 - 1] = fd[i2 - 1][l2 - 1] + costs_.del;
    }
    for (int j2 = l2; j2 <= j; ++j2) {
      fd[l1 - 1][j2] = fd[l1 - 1][j2 - 1] + costs_.insert;
    }

    for (int i2 = l1; i2 <= i; ++i2) {
      for (int j2 = l2; j2 <= j; ++j2) {
        const double cost_del = fd[i2 - 1][j2] + costs_.del;
        const double cost_ins = fd[i2][j2 - 1] + costs_.insert;

        if (d1.l[i2] == l1 && d2.l[j2] == l2) {
          const double cost_match = fd[i2 - 1][j2 - 1] + _node_cost(d1.nodes[i2], d2.nodes[j2]);
          fd[i2][j2]              = ::std::min({cost_del, cost_ins, cost_match});
          td[i2][j2]              = fd[i2][j2];
        } else {
          const double cost_match = fd[d1.l[i2] - 1][d2.l[j2] - 1] + td[i2][j2];
          fd[i2][j2]              = ::std::min({cost_del, cost_ins, cost_match});
        }
      }
    }
  }
};

}  // namespace hhds
