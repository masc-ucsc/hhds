// See LICENSE.txt for details

#pragma once

#include <array>
#include <cassert>
#include <vector>

#include "graph_base.hpp"
#include "iassert.hpp"

namespace hhds {

class __attribute__((packed)) Graph_overflow {  // AKA pin or node entry
public:
  Graph_overflow() { clear(); }

  void clear() {
    bzero(this, sizeof(Graph_overflow));  // set zero everything
  }

  bool add_edge(uint32_t self_id, uint32_t other_id) {
    I(self_id != other_id);
    if (n_sedges < sedge.size()) {
      int64_t s    = other_id - self_id;
      bool    fits = s > std::numeric_limits<int16_t>::min() && s < std::numeric_limits<int16_t>::max();
      if (fits) {
        for (auto i = 0u; i < sedge.size(); ++i) {
          if (sedge[i] != 0) {
            continue;
          }
          sedge[i] = static_cast<int16_t>(s);
          ++n_sedges;
          return true;
        }
        I(false);
        return false;
      }
    }
    for (auto &ent : ledge) {
      if (ent != 0) {
        continue;
      }
      ent = other_id;
      ++n_ledges;
      return true;
    }
    return false;
  }

  bool del_edge(uint32_t self_id, uint32_t other_id) {
    I(self_id != other_id);
    int64_t s    = other_id - self_id;
    bool    fits = s > std::numeric_limits<int16_t>::min() && s < std::numeric_limits<int16_t>::max();
    if (fits) {
      for (auto &ent : sedge) {
        if (ent != static_cast<int16_t>(s)) {
          continue;
        }

        --n_sedges;
        ent = 0;
        return true;
      }
    }
    for (auto &ent : ledge) {
      if (ent != other_id) {
        continue;
      }
      ent = 0;
      --n_ledges;
      return true;
    }

    return false;
  }

  [[nodiscard]] size_t get_num_local_edges() const { return n_sedges + n_ledges; }

  [[nodiscard]] bool has_edges() const { return (n_sedges | n_ledges) != 0; }

  void dump(uint32_t self_id) const;

  class iterator {
  public:
    using value_type = uint32_t;
    using sit_type   = std::array<int16_t, 7>::const_iterator;
    using lit_type   = std::array<uint32_t, 4>::const_iterator;

    iterator(value_type sid, sit_type x1, sit_type send, lit_type x2, lit_type lend)
        : self_id(sid), sit(x1), sit_end(send), lit(x2), lit_end(lend) {}

    [[nodiscard]] value_type operator*() const {
      if (sit != sit_end) {
        return self_id + *sit;
      }
      return *lit;
    }

    iterator &operator++() {
      while (sit != sit_end) {
        ++sit;
        if (*sit != 0) {
          return *this;
        }
      }
      do {
        ++lit;
        if (*lit != 0) {
          return *this;
        }
      } while (lit != lit_end);

      return *this;
    }

    [[nodiscard]] const iterator operator++(int) {
      iterator tmp(*this);
      operator++();
      return tmp;
    }

    [[nodiscard]] bool operator==(const iterator &rhs) const { return sit == rhs.sit && lit == rhs.lit; }
    [[nodiscard]] bool operator!=(const iterator &rhs) const { return !(*this == rhs); }

    [[nodiscard]] sit_type get_sit() const { return sit; }
    [[nodiscard]] lit_type get_lit() const { return lit; }

  private:
    value_type self_id;
    sit_type   sit;
    sit_type   sit_end;
    lit_type   lit;
    lit_type   lit_end;
  };

  [[nodiscard]] iterator begin(uint32_t self_id) {
    return {self_id, sedge.data(), sedge.data() + sedge.size(), ledge.data(), ledge.data() + ledge.size()};
  }

  [[nodiscard]] iterator end() {
    return {0, sedge.data() + sedge.size(), sedge.data() + sedge.size(), ledge.data() + ledge.size(), ledge.data() + ledge.size()};
  }

  [[nodiscard]] iterator begin(uint32_t self_id) const {
    return {self_id, sedge.data(), sedge.data() + sedge.size(), ledge.data(), ledge.data() + ledge.size()};
  }

  [[nodiscard]] iterator end() const {
    return {0, sedge.data() + sedge.size(), sedge.data() + sedge.size(), ledge.data() + ledge.size(), ledge.data() + ledge.size()};
  }
  [[nodiscard]] iterator cbegin(uint32_t self_id) const { return begin(self_id); }
  [[nodiscard]] iterator cend() const { return end(); }

  void erase(iterator it) {
    if (it == end()) {
      return;
    }
    auto sit = it.get_sit();
    if (sit != (sedge.data() + sedge.size())) {
      auto pos = sit - sedge.data();
      I(pos >= 0 && pos < sedge.size());
      sedge[pos] = 0;
    } else {
      auto pos = it.get_lit() - ledge.data();
      I(pos >= 0 && pos < ledge.size());
      sedge[pos] = 0;
    }
  }

private:
  // Overflow (32 bytes) -- Always 32bytes aligned
  // Byte 0:1
  uint8_t n_sedges;  // number of sedges (7 max) 0xFF means free
  uint8_t n_ledges;  // number of ledges (4 max)
  // sedges: 2:15
  std::array<int16_t, 7> sedge;
  // ledges: 16:32
  std::array<uint32_t, 4> ledge;
};

};  // namespace hhds
