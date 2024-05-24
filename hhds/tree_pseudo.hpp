//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// tree.hpp
#include <cstdint>
#include <vector>

namespace hhds {

using Tree_index = std::int32_t;

class Tree {
public:
  // Checks
  [[nodiscard]] bool is_empty(Tree_index id) const;

  // Gets
  [[nodiscard]] Tree_index get_parent(Tree_index id) const;
  [[nodiscard]] Tree_index get_last_child(Tree_index id) const;

  // Insertion
  [[nodiscard]] Tree_index insert_last_child(Tree_index parent);

private:
  struct Tree_node {
    Tree_index first_child_id;
    Tree_index parent_id;
  };

  std::vector<Tree_node> nodes;
};

}  // namespace hhsd

#if 0
// Even though Moliere said "Trees that are slow to grow bear the best fruit.",
// but this project aim at the opposite. We want the fastest trees to grow and
// traverse.

//
// API: find_siblings
// API: find_parent
// API: find_children
// API: find_next_siblings
// API: find_prev_siblings
// API: is_first_child()
// API: is_last_child()
//
// API: insert_child_next_to
// API: insert_first_child
// API: insert_last_child
// API: add_child (first child or insert_last_child)
//

#include <sys/stat.h>
#include <sys/types.h>

#include <cassert>
#include <cstdint>
#include <functional>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "iassert.hpp"

namespace hhsd {
using Tree_level = int32_t;
using Tree_pos   = int32_t;

unsing Tree_index = uint32_t;

class Tree {
protected:
public:
  Tree_index get_last_child(const Tree_index &parent_index) const;

  size_t max_size() const;

  bool is_last_child(const Tree_index &self_index) const;

  bool is_first_child(const Tree_index &self_index) const;

  Tree_index get_first_child(const Tree_index &parent_index) const;

  Tree_index get_sibling_next(const Tree_index &sibling) const;

  Tree_index get_sibling_prev(const Tree_index &sibling) const;

#if 0
  class Tree_depth_preorder_iterator {
  public:
    class CTree_depth_preorder_iterator {
    public:
      CTree_depth_preorder_iterator(const Tree_index &_ti, const tree<X> *_t) : ti(_ti), t(_t) { start_ti = _ti; }
      CTree_depth_preorder_iterator operator++();
      bool operator!=(const CTree_depth_preorder_iterator &other);
      const Tree_index &operator*() const { return ti; }

    private:
      Tree_index     ti;
      Tree_index     start_ti;
      const Tree2 *t;
    };
  public:
    Tree_depth_preorder_iterator() = delete;
    explicit Tree_depth_preorder_iterator(const Tree_index &_b, const tree<X> *_t) : ti(_b), t(_t) {}

    CTree_depth_preorder_iterator begin() const;
    CTree_depth_preorder_iterator end() const;
  };
#endif

#if 0
  class Tree_depth_postorder_iterator {
  public:
    class CTree_depth_postorder_iterator {
    public:
      CTree_depth_postorder_iterator(const Tree_index &_ti, const tree<X> *_t) : ti(_ti), t(_t) {}
      CTree_depth_postorder_iterator operator++() {
        CTree_depth_postorder_iterator i(ti, t);

        ti = t->get_depth_postorder_next(ti);

        return i;
      };
      bool operator!=(const CTree_depth_postorder_iterator &other) {
        I(t == other.t);
        return ti != other.ti;
      }
      const Tree_index &operator*() const { return ti; }

    private:
      Tree_index     ti;
      const tree<X> *t;
    };

  private:
  protected:
    Tree_index     ti;
    const tree<X> *t;

  public:
    Tree_depth_postorder_iterator() = delete;
    explicit Tree_depth_postorder_iterator(const Tree_index &_b, const tree<X> *_t) : ti(_b), t(_t) {}

    CTree_depth_postorder_iterator begin() const { return CTree_depth_postorder_iterator(ti, t); }
    CTree_depth_postorder_iterator end() const {
      return CTree_depth_postorder_iterator(Tree_index(-1, -1), t);
    }  // 0 is end index for iterator
  };

  class Tree_sibling_iterator {
  public:
    class CTree_sibling_iterator {
    public:
      CTree_sibling_iterator(const Tree_index &_ti, const tree<X> *_t) : ti(_ti), t(_t) {}
      CTree_sibling_iterator operator++() {
        CTree_sibling_iterator i(ti, t);

        ti = t->get_sibling_next(ti);

        return i;
      };
      bool operator!=(const CTree_sibling_iterator &other) {
        I(t == other.t);
        return ti != other.ti;
      }

      bool operator==(const CTree_sibling_iterator &other) {
        I(t == other.t);
        return ti == other.ti;
      }
      const Tree_index &operator*() const { return ti; }

    private:
      Tree_index     ti;
      const tree<X> *t;
    };

  private:
  protected:
    Tree_index     ti;
    const tree<X> *t;

  public:
    Tree_sibling_iterator() = delete;
    explicit Tree_sibling_iterator(const Tree_index &_b, const tree<X> *_t) : ti(_b), t(_t) {}

    CTree_sibling_iterator begin() const { return CTree_sibling_iterator(ti, t); }
    CTree_sibling_iterator end() const { return CTree_sibling_iterator(invalid_index(), t); }
  };
#endif

  Tree2();
  Tree2(std::string_view _path, std::string_view _map_name);

  [[nodiscard]] inline std::string_view get_name() const { return mmap_name; }
  [[nodiscard]] inline std::string_view get_path() const { return mmap_path; }

  void clear();

  [[nodiscard]] bool empty() const;

  // WARNING: can not return Tree_index & because future additions can move the pointer (vector realloc)
  Tree_index add_child(const Tree_index &parent, const X &data);
  Tree_index append_sibling(const Tree_index &sibling, const X &data);
  Tree_index insert_next_sibling(const Tree_index &sibling, const X &data);

  Tree_index get_depth_preorder_next(const Tree_index &child) const;
  Tree_index get_depth_postorder_next(const Tree_index &child) const;

  Tree_index get_parent(const Tree_index &index) const;

  static constexpr Tree_index invalid_index() { return Tree_index(-1, -1); }

  Tree_index get_child(const Tree_index &start_index) const;

#if 0
  void each_bottom_up_fast(std::function<void(const Tree_index &self, const X &)> fn) const;
  void each_top_down_fast(std::function<void(const Tree_index &self, const X &)> fn) const;

  Tree_depth_preorder_iterator depth_preorder(const Tree_index &start_index) const {
    return Tree_depth_preorder_iterator(start_index, this);
  }

  Tree_depth_preorder_iterator  depth_preorder() const { return Tree_depth_preorder_iterator(Tree_index::root(), this); }
  Tree_depth_postorder_iterator depth_postorder() const {
    auto last_child = Tree_index::root();
    while (!is_leaf(last_child)) {
      last_child = get_first_child(last_child);
    }
    return Tree_depth_postorder_iterator(last_child, this);
  }

  Tree_sibling_iterator siblings(const Tree_index &start_index) const { return Tree_sibling_iterator(start_index, this); }
  Tree_sibling_iterator children(const Tree_index &start_index) const {
    if (is_leaf(start_index))
      return Tree_sibling_iterator(invalid_index(), this);

    return Tree_sibling_iterator(get_first_child(start_index), this);
  }
#endif

  bool is_leaf(const Tree_index &index) const;
  bool is_root(const Tree_index &index) const;
  bool has_single_child(const Tree_index &index) const;

  /* LCOV_EXCL_START */
  void dump() const;
  /* LCOV_EXCL_STOP */
};

#endif
