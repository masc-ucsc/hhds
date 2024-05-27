//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// tree.hpp
#include <sys/types.h>
#include <sys/stat.h>

#include <stdexcept>
#include <cassert>

#include <cstdint>
#include <functional>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace hhds {
  using Tree_index = std::int32_t;

  class Tree_Node {
  public:
      /* Default constructor */
      constexpr Tree_Node() noexcept
          : firstChildId(INVALID), lastChildId(INVALID), parentId(INVALID) {}

      /* Parameterized constructor */
      constexpr Tree_Node(Tree_index firstChildId, Tree_index lastChildId, Tree_index parentId) noexcept
          : firstChildId(firstChildId), lastChildId(lastChildId), parentId(parentId) {}

      /* Copy constructor */
      constexpr Tree_Node(const Tree_Node &other) noexcept
          : firstChildId(other.firstChildId), lastChildId(other.lastChildId), parentId(other.parentId) {}

      /* Copy assignment */
      constexpr Tree_Node &operator=(const Tree_Node &other) noexcept {
        firstChildId = other.firstChildId;
        lastChildId = other.lastChildId;
        parentId = other.parentId;
        return *this;
      }

      // Operators
      constexpr bool operator==(const Tree_Node &other) const { 
        return firstChildId == other.firstChildId 
            && lastChildId == other.lastChildId
            && parentId == other.parentId;
      }
      constexpr bool operator!=(const Tree_Node &other) const { 
        return firstChildId != other.firstChildId 
            || lastChildId != other.lastChildId
            || parentId != other.parentId;
      }

      // Accessor methods for encapsulation
      constexpr Tree_index getFirstChildId() const noexcept { return firstChildId; }
      constexpr Tree_index getLastChildId() const noexcept { return lastChildId; }
      constexpr Tree_index getParentId() const noexcept { return parentId; }

      constexpr void setFirstChildId(Tree_index id) noexcept { firstChildId = id; }
      constexpr void setLastChildId(Tree_index id) noexcept { lastChildId = id; }
      constexpr void setParentId(Tree_index id) noexcept { parentId = id; }
      constexpr void invalidate() noexcept { 
        firstChildId = INVALID;
        lastChildId = INVALID;
        parentId = INVALID;
      }

      // Checkers
      [[nodiscard]] constexpr bool is_empty() const noexcept { return firstChildId == INVALID; }
      [[nodiscard]] constexpr bool has_no_children() const noexcept { return firstChildId == INVALID; }
      [[nodiscard]] constexpr bool is_uninitialized() const noexcept { return firstChildId == INVALID;}
      [[nodiscard]] constexpr bool is_root() const noexcept { return parentId == INVALID; }
      [[nodiscard]] constexpr bool is_invalid() const noexcept { 
        return (firstChildId == INVALID 
            || lastChildId == INVALID)
            && (parentId != INVALID); 
      }

      // Special Indices for readability
      enum class SpecialIndex : Tree_index {
          IN_HASHMAP = -2,
          INVALID = -1
      };

  // : public

  protected:
      // Member variables
      Tree_index firstChildId;
      Tree_index lastChildId;
      Tree_index parentId;

      // Enum constants for ease of use
      static constexpr Tree_index IN_HASHMAP = static_cast<Tree_index>(SpecialIndex::IN_HASHMAP);
      static constexpr Tree_index INVALID = static_cast<Tree_index>(SpecialIndex::INVALID);

  // : protected

  }; // Tree_Node class 

  template<typename T>
  class Tree {
  public:
    // Checks
    [[nodiscard]] bool is_empty(const Tree_index &idx) const;
    [[nodiscard]] bool is_first_child(const Tree_index &idx) const;
    [[nodiscard]] bool is_last_child(const Tree_index &idx) const;

    // Gets
    [[nodiscard]] Tree_index get_last_child(const Tree_index &par_idx) const;
    [[nodiscard]] Tree_index get_first_child(const Tree_index &par_idx) const;
    /*  DOUBT  */ [[nodiscard]] Tree_index get_tree_width(const std::int32_t level) const;
    [[nodiscard]] Tree_index get_sibling_prev(const Tree_index &sib_idx) const;
    [[nodiscard]] Tree_index get_sibling_next(const Tree_index &sib_idx) const;
    [[nodiscard]] Tree_index get_parent(const Tree_index &child_idx) const;

    // Modifiers
    void add_child(const Tree_index &par_idx, const T &data);
    void add_root(const T &data);
    void delete_leaf(const Tree_index &chld_idx);
    void delete_subtree(const Tree_index &chld_idx);
    void append_sibling(const Tree_index &sib_idx, const T &data);
    void insert_next_sibling(const Tree_index &sib_idx, const T &data);

  //: public

  private:
    std::vector<Tree_Node>        nodes;              // The "flattened" tree
    std::vector<T>                data_points;        // The data stored in the tree
    std::vector<std::int32_t>     level;              /* DOUBT : SHOULD WE PUT THE LEVEL value IN THE TREE_NODE? */
    std::vector<std::int32_t>     count_at_level;     /* DOUBT : SHOULD WE PUT THE LEVEL value IN THE TREE_NODE? */
    [[nodiscard]] bool _check_idx_exists(const Tree_index &idx) const noexcept {
      return idx >= 0 && idx < static_cast<int>(nodes.size());
    }
    [[nodiscard]] bool _cannot_be_accessed(const Tree_index &idx) const noexcept {
      return idx == static_cast<Tree_index>(Tree_Node::SpecialIndex::INVALID) 
          || idx == static_cast<Tree_index>(Tree_Node::SpecialIndex::INVALID); 
    }

    // DEBUG
    void print_nodes();

  //: private

  }; // Tree class

  /**
   * @brief Checks if the node at the given index is empty
   * 
   * @param idx The index of the node to check
   * @return true if the node is empty
  */
  template<typename T>
  bool Tree<T>::is_empty(const Tree_index &idx) const {
    if (!_check_idx_exists(idx)) {
      throw std::out_of_range("Parent index out of range");
    }
    return nodes[idx].is_empty();
  }

  /**
   * @brief Gets the last child of the node at the given index
   * 
   * @param par_idx The index of the parent node (given index)
   * @return The index of the last child of the parent node
   * 
   * @todo Add exception handling for Child in hashmap
   * @todo Add that handling to all
   * @todo How are we handling invalid accesses?
  */
  template<typename T>
  Tree_index Tree<T>::get_last_child(const Tree_index &par_idx) const {
    if (!_check_idx_exists(par_idx)) {
      throw std::out_of_range("Parent index out of range");
    }
    return nodes[par_idx].getLastChildId();
  }

  /**
   * @brief Gets the first child of the node at the given index
   * 
   * @param par_idx The index of the parent node (given index)
   * @return The index of the first child of the parent node
  */
  template<typename T>
  Tree_index Tree<T>::get_first_child(const Tree_index &par_idx) const {
    if (!_check_idx_exists(par_idx)) {
      throw std::out_of_range("Parent index out of range");
    }

    return nodes[par_idx].getFirstChildId();
  }

  /**
   * @brief Checks if the node at the given index is the first child of its parent
   * 
   * @param idx The index of the node to check
   * @return true if the node is the first child of its parent
   * 
  */
  template<typename T>
  bool Tree<T>::is_first_child(const Tree_index &idx) const {
    if (!_check_idx_exists(idx)) {
      throw std::out_of_range("Parent index out of range");
    } else if (nodes[idx].is_root()) {
      return false;
    } else if (_cannot_be_accessed(nodes[idx].getParentId())) {
      return false;
    }

    return nodes[nodes[idx].getParentId()].getFirstChildId() == idx;
  }

  /**
   * @brief Checks if the node at the given index is the last child of its parent
   * 
   * @param idx The index of the node to check
   * @return true if the node is the last child of its parent
   * 
   * @todo Add exception handling for Child in hashmap
  */
  template<typename T>
  bool Tree<T>::is_last_child(const Tree_index &idx) const {
    if (!_check_idx_exists(idx)) {
      throw std::out_of_range("Parent index out of range");
    } else if (nodes[idx].is_root()) {
      return false;
    } else if (_cannot_be_accessed(nodes[idx].getParentId())) {
      return false;
    }

    return nodes[nodes[idx].getParentId()].getLastChildId() == idx;
  }

  /**
   * @brief Gets the previous sibling of the node at the given index
   * 
   * @param sib_idx The index of the sibling node (given index)
   * @return The index of the previous sibling of the sibling node
   * 
   * @todo Add exception handling for Child in hashmap
  */
  template<typename T>
  Tree_index Tree<T>::get_sibling_prev(const Tree_index &sib_idx) const {
    if (!_check_idx_exists(sib_idx)) {
      throw std::out_of_range("Sibling index out of range");
    } else if (nodes[sib_idx].is_root() || is_first_child(sib_idx)) {
      return static_cast<Tree_index>(Tree_Node::SpecialIndex::INVALID);
    }

    return sib_idx - 1; // The flattening policy assigns consecutive id to sibs.
  }

  /**
   * @brief Gets the next sibling of the node at the given index
   * 
   * @param sib_idx The index of the sibling node (given index)
   * @return The index of the next sibling of the sibling node
   * 
   * @todo Add exception handling for Child in hashmap
  */
  template<typename T>
  Tree_index Tree<T>::get_sibling_next(const Tree_index &sib_idx) const {
    if (!_check_idx_exists(sib_idx)) {
      throw std::out_of_range("Sibling index out of range");
    } else if (nodes[sib_idx].is_root() || is_last_child(sib_idx)) {
      return static_cast<Tree_index>(Tree_Node::SpecialIndex::INVALID);
    }

    return sib_idx + 1; // The flattening policy assigns consecutive id to sibs.
  }

  /**
   * @brief Gets the parent of the node at the given index
   * 
   * @param sib_idx The index of the child node (given index)
   * @return The index of the parent of the child node
   * 
   * @todo Add exception handling for Child in hashmap
  */
  template<typename T>
  Tree_index Tree<T>::get_parent(const Tree_index &child_idx) const {
    if (!_check_idx_exists(child_idx)) {
      throw std::out_of_range("Child index out of range");
    }

    return nodes[child_idx].getParentId();
  }

  /**
   * @brief Adds the first node, that is, the root of the tree
   * 
   * @param data The data to be stored in the root node
  */
  template<typename T>
  void Tree<T>::add_root(const T &data) {
    if (!nodes.empty()) {
      throw std::logic_error("Root already exists");
    }
    nodes.emplace_back();
    data_points.emplace_back(data);
  }


  /**
   * @brief Adds a child to the node at the given index
   * 
   * @param par_idx The index of the parent node (given index)
   * @param data The data to be stored in the child node
   * 
   * @todo Add exception handling for Child in hashmap
   * @todo Add case II
  */
  template<typename T>
  void Tree<T>::add_child(const Tree_index &par_idx, const T &data) {
    if (nodes.empty()) {
      throw std::logic_error("Tree is empty. Add a root node first.");
    } else if (!_check_idx_exists(par_idx)) {
      throw std::out_of_range("Parent index out of range");
    }

    std::cout << "Adding child to parent : " << par_idx << "\n";

    /* PLEASE REMOVE THIS COMMENT ONCE THE BRUTE ADD CHILD HAS BEEN FINALIZED */
    // Case I: The add_child has been called close to the end of this array
    //         I will have to add the child right AFTER the par_idx.lastchild
    //         If the parent has no children yet, then the id will be right 
    //         AFTER the par_idx.
    //      0) last_no_upd = (par_idx.has_no_children()) ? par_idx : par_idx.lastchild
    //      1) Shift everything at id > to_add to the right by 1
    //      2) Add +1 to every (fc, lc, par) id > to_add. NOTE: 
    //         if any par <= to_add, then don't do id++.
    //      3) Add the new node at to_add
    //      4) Update the par_idx.lastchild to to_add
    const Tree_index last_no_upd = (nodes[par_idx].has_no_children()) ? 
                              par_idx : nodes[par_idx].getLastChildId();

    std::cout << "Last no update : " << last_no_upd << "\n";

    // Make space for another node at the end
    Tree<T>::print_nodes();
  
    nodes.emplace_back();
    data_points.emplace_back(data);
    Tree<T>::print_nodes();

    // Shift everything at id > last_no_upd to the right by 1
    // and add +1 wherever valid
    for (Tree_index i = nodes.size() - 1; i > last_no_upd; --i) {
      std::cout << "Loop starts running : " << i << "\n";
      nodes[i] = nodes[i - 1];
      data_points[i] = data_points[i - 1];

      // Add +1 to every (fc, lc, par) id > last_no_upd
      if (nodes[i].getFirstChildId() > last_no_upd)
        nodes[i].setFirstChildId(nodes[i].getFirstChildId() + 1);

      if (nodes[i].getLastChildId() > last_no_upd)
        nodes[i].setLastChildId(nodes[i].getLastChildId() + 1);

      if (nodes[i].getParentId() > last_no_upd)
        nodes[i].setParentId(nodes[i].getParentId() + 1);
    }

    // Add the new node at last_no_upd + 1
    nodes[last_no_upd + 1] = Tree_Node(static_cast<Tree_index>(Tree_Node::SpecialIndex::INVALID), 
                                      static_cast<Tree_index>(Tree_Node::SpecialIndex::INVALID), 
                                      par_idx);
    data_points[last_no_upd + 1] = data;

    // Update bookkeeping at the parent index
    if (nodes[par_idx].has_no_children()) {
      nodes[par_idx].setFirstChildId(last_no_upd + 1);
    }
    nodes[par_idx].setLastChildId(last_no_upd + 1);
  
    Tree<T>::print_nodes();
  }

  /**
   * @brief Print the list nodes in the tree, for debugging purposes
   * 
  */
  template<typename T>
  void Tree<T>::print_nodes() {
    std::cout << "Nodes in the tree:\n";
    for (std::size_t i = 0; i < nodes.size(); ++i) {
      std::cout << "Node (" << i << ") : ";
      std::cout << " [" << nodes[i].getFirstChildId() << ", ";
      std::cout << nodes[i].getLastChildId() << ", ";
      std::cout << nodes[i].getParentId() << "] -> ";
      std::cout << data_points[i] << "\n";
    }
  }
} // HHDS namespace