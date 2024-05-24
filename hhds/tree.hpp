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
          : firstChildId(NOT_INIT), lastChildId(NOT_INIT), parentId(NOT_INIT) {}

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
      [[nodiscard]] constexpr bool is_empty() const noexcept { return firstChildId == NOT_INIT; }
      [[nodiscard]] constexpr bool is_root() const noexcept { return parentId == INVALID; }
      [[nodiscard]] constexpr bool is_invalid() const noexcept { 
        return firstChildId == INVALID 
            || lastChildId == INVALID
            || parentId == INVALID; 
      }

      // Special Indices for readability
      enum class SpecialIndex : Tree_index {
          NOT_INIT = -3,
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
      static constexpr Tree_index NOT_INIT = static_cast<Tree_index>(SpecialIndex::NOT_INIT);
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

    // Modifiers
    void add_child(const Tree_index &par_idx, const T &data);
    void delete_leaf(const Tree_index &chld_idx);
    void delete_subtree(const Tree_index &chld_idx);
    void append_sibling(const Tree_index &sib_idx, const T &data);
    void insert_next_sibling(const Tree_index &sib_idx, const T &data);

  //: public

  private:
    std::vector<Tree_Node>        nodes;          // The "flattened" tree
    std::vector<T>                data;           // The data stored in the tree
    std::vector<std::int32_t>     level;          /* DOUBT : SHOULD WE PUT THE LEVEL value IN THE TREE_NODE? */
    std::vector<std::int32_t>     count_at_level; /* DOUBT : SHOULD WE PUT THE LEVEL value IN THE TREE_NODE? */
    [[nodiscard]] bool _check_idx_exists(const Tree_index &idx) const noexcept {
      return idx >= 0 && idx < nodes.size();
    }

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
    assert(_check_idx_exists(idx));
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
  */
  template<typename T>
  bool Tree<T>::is_first_child(const Tree_index &idx) const {
    if (!_check_idx_exists(idx)) {
      throw std::out_of_range("Parent index out of range");
    } else if (nodes[idx].is_root()) {
      return false;
    }

    return nodes[nodes[idx].getParentId()].getFirstChildId() == idx;
  }

  /**
   * @brief Checks if the node at the given index is the last child of its parent
   * 
   * @param idx The index of the node to check
   * @return true if the node is the last child of its parent
  */
  template<typename T>
  bool Tree<T>::is_last_child(const Tree_index &idx) const {
    if (!_check_idx_exists(idx)) {
      throw std::out_of_range("Parent index out of range");
    } else if (nodes[idx].is_root()) {
      return false;
    }

    return nodes[nodes[idx].getParentId()].getLastChildId() == idx;
  }

} // HHDS namespace