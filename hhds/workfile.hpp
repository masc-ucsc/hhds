template <typename X>
class tree {
private:
  /* The tree pointers and data stored separately */
  std::vector<Tree_pointers>    pointers_stack;
  std::vector<std::optional<X>> data_stack;
  Forest<X>* forest_ptr;

  /* Special functions for sanity */
  [[nodiscard]] inline bool _check_idx_exists(const Tree_pos& idx) const noexcept {
    // idx >= 0 not needed for unsigned int
    return idx < static_cast<Tree_pos>(pointers_stack.size() << CHUNK_SHIFT);
  }

  [[nodiscard]] inline bool _contains_data(const Tree_pos& idx) const noexcept {
    return (pointers_stack[idx >> CHUNK_SHIFT].get_num_short_del_occ() >= (idx & CHUNK_MASK));
    // return (idx < data_stack.size() && data_stack[idx].has_value());
  }

  /* Function to add an entry to the pointers and data stack (typically for add/append)*/
  Tree_pos _create_space(const X& data) {
    // Make space for CHUNK_SIZE number of entries at the end
    data_stack.emplace_back(data);
    data_stack.resize(data_stack.size() + CHUNK_MASK);

    // Add the single pointer node for all CHUNK_SIZE entries
    pointers_stack.emplace_back();

    return pointers_stack.size() - 1;
  }

  /* Function to insert a new chunk in between (typically for handling add/append corner cases)*/
  Tree_pos _insert_chunk_after(const Tree_pos& curr) {
    // Allot new chunk at the end
    const auto new_chunk_id = _create_space(X());

    // Update bookkeeping -> This is basically inserting inside of a doubly linked list
    pointers_stack[new_chunk_id].set_prev_sibling(curr);
    pointers_stack[new_chunk_id].set_next_sibling(pointers_stack[curr].get_next_sibling());
    pointers_stack[curr].set_next_sibling(new_chunk_id);

    if (pointers_stack[new_chunk_id].get_next_sibling() != INVALID) {
      pointers_stack[pointers_stack[new_chunk_id].get_next_sibling()].set_prev_sibling(new_chunk_id);
    }

    pointers_stack[new_chunk_id].set_parent(pointers_stack[curr].get_parent());

    return new_chunk_id;
  }

  /* Helper function to check if we can fit something in the int16_t delta*/
  inline bool _fits_in_short_del(const Tree_pos& parent_chunk_id, const Tree_pos& child_chunk_id) {
    const int64_t delta = child_chunk_id - parent_chunk_id;
    return (delta >= MIN_SHORT_DELTA) && (delta <= MAX_SHORT_DELTA);
  }

  /* Helper function to update the parent pointer of all sibling chunks*/
  void _update_parent_pointer(const Tree_pos& first_child, const Tree_pos& new_parent_id) {
    I(_check_idx_exists(first_child), "First child index out of range");
    I(_check_idx_exists(new_parent_id), "New parent index out of range");

    auto curr_chunk_id = (first_child >> CHUNK_SHIFT);

    while (curr_chunk_id != INVALID) {
      pointers_stack[curr_chunk_id].set_parent(new_parent_id);
      curr_chunk_id = pointers_stack[curr_chunk_id].get_next_sibling();
    }
  }

  Tree_pos _try_fit_child_ptr(const Tree_pos& parent_id, const Tree_pos& child_id) {
    I(_check_idx_exists(parent_id), "parent_id index out of range");
    I(_check_idx_exists(child_id), "child_id index out of range");

    /* BASE CASE OF THE RECURSION */
    // If parent has long ptr access, this is easy
    if ((parent_id & CHUNK_MASK) == 0) {
      pointers_stack[parent_id >> CHUNK_SHIFT].set_last_child_l(child_id >> CHUNK_SHIFT);
      if (pointers_stack[parent_id >> CHUNK_SHIFT].get_first_child_l() == INVALID) {
        pointers_stack[parent_id >> CHUNK_SHIFT].set_first_child_l(child_id >> CHUNK_SHIFT);
      }

      // update is_leaf flag
      pointers_stack[parent_id >> CHUNK_SHIFT].set_is_leaf(false);

      return parent_id;
    }

    // Now, try to fit the child in the int16_t delta
    const auto parent_chunk_id     = (parent_id >> CHUNK_SHIFT);
    const auto parent_chunk_offset = (parent_id & CHUNK_MASK);
    if (_fits_in_short_del(parent_chunk_id, child_id >> CHUNK_SHIFT)) {
      // Adjust the child pointers
      pointers_stack[parent_chunk_id].set_last_child_s_at(parent_chunk_offset - 1, (child_id >> CHUNK_SHIFT) - parent_chunk_id);

      if (pointers_stack[parent_chunk_id].get_first_child_s_at(parent_chunk_offset - 1) == INVALID) {
        pointers_stack[parent_chunk_id].set_first_child_s_at(parent_chunk_offset - 1, (child_id >> CHUNK_SHIFT) - parent_chunk_id);
      }

      // update is_leaf flag
      pointers_stack[parent_chunk_id].set_is_leaf(false);

      return parent_id;
    }

    /* RECURSION */
    const auto            grandparent_id = pointers_stack[parent_chunk_id].get_parent();
    std::vector<Tree_pos> new_chunks;

    // Break the chunk fully -> Every node in the chunk is moved to a separate chunk
    for (int16_t offset = parent_chunk_offset - 1; offset < NUM_SHORT_DEL; offset++) {
      if (_contains_data((parent_chunk_id << CHUNK_SHIFT) + offset + 1)) {
        const auto curr_id = (parent_chunk_id << CHUNK_SHIFT) + offset + 1;

        // Create a new chunk, put this one over there
        const auto new_chunk_id = _insert_chunk_after(new_chunks.empty() ? parent_chunk_id : new_chunks.back());

        // Store the new chunk id for updates later
        new_chunks.push_back(new_chunk_id);

        // Remove data from old, and put it here
        data_stack[new_chunk_id << CHUNK_SHIFT] = data_stack[curr_id];
        data_stack[curr_id]                     = std::nullopt;

        // Convert the int16_t pointers here to long pointers there
        const auto fc = pointers_stack[parent_chunk_id].get_first_child_s_at(offset);
        const auto lc = pointers_stack[parent_chunk_id].get_last_child_s_at(offset);
        if (fc != INVALID) {
          pointers_stack[new_chunk_id].set_first_child_l(fc + parent_chunk_id);
          pointers_stack[new_chunk_id].set_last_child_l(lc + parent_chunk_id);
          pointers_stack[new_chunk_id].set_is_leaf(false);
        }

        // Update the parent pointer of all children of this guy
        // THIS LOOKS LIKE A BOTTLENECK -> Will iterate over all ~ (children / 8) chunks
        if (fc != INVALID) {
          _update_parent_pointer((fc + parent_chunk_id) << CHUNK_SHIFT, new_chunk_id << CHUNK_SHIFT);
        }

        // Remove the int16_t pointers in the old chunk
        pointers_stack[parent_chunk_id].set_first_child_s_at(offset, INVALID);
        pointers_stack[parent_chunk_id].set_last_child_s_at(offset, INVALID);
      }
    }
    // Decrement the number of occupied slots in the chunk
    pointers_stack[parent_chunk_id].set_num_short_del_occ(parent_chunk_offset - 1);

    // Try fitting the last chunk here in the grandparent. Recurse.
    const auto my_new_parent = _try_fit_child_ptr(grandparent_id, new_chunks.back() << CHUNK_SHIFT);

    // Update the parent pointer of the new chunks
    if (my_new_parent != grandparent_id) {
      for (const auto& new_chunk : new_chunks) {
        pointers_stack[new_chunk].set_parent(my_new_parent);
      }
    }

    // update is_leaf flag
    pointers_stack[parent_chunk_id].set_is_leaf(false);

    return new_chunks.front() << CHUNK_SHIFT;  // The first one was where the parent was sent
  }
  // :private

public:
  /**
   *  Query based API (no updates)
   */
  [[nodiscard]] Tree_pos get_parent(const Tree_pos& curr_index) const;
  [[nodiscard]] Tree_pos get_last_child(const Tree_pos& parent_index) const;
  [[nodiscard]] Tree_pos get_first_child(const Tree_pos& parent_index) const;
  [[nodiscard]] bool     is_last_child(const Tree_pos& self_index) const;
  [[nodiscard]] bool     is_first_child(const Tree_pos& self_index) const;
  [[nodiscard]] Tree_pos get_sibling_next(const Tree_pos& sibling_id) const;
  [[nodiscard]] Tree_pos get_sibling_prev(const Tree_pos& sibling_id) const;
  [[nodiscard]] bool     is_leaf(const Tree_pos& leaf_index) const;
  [[nodiscard]] Tree_pos get_root() const {return ROOT;}

  /**
   *  Update based API (Adds and Deletes from the tree)
   */
  // FREQUENT UPDATES
  Tree_pos append_sibling(const Tree_pos& sibling_id, const X& data);
  Tree_pos add_child(const Tree_pos& parent_index, const X& data);
  Tree_pos add_root(const X& data);

  void delete_leaf(const Tree_pos& leaf_index);
  void delete_subtree(const Tree_pos& subtree_root);
  void add_subtree_ref(const Tree_pos& node_pos, Tree_pos subtree_ref);

  // INFREQUENT UPDATES
  Tree_pos insert_next_sibling(const Tree_pos& sibling_id, const X& data);

  /**
   * Data access API
   */
  X& get_data(const Tree_pos& idx) {
    GI(_check_idx_exists(idx), data_stack[idx].has_value(), "Index out of range or no data at the index");

    return *data_stack[idx];
  }

  const X& get_data(const Tree_pos& idx) const {
    GI(_check_idx_exists(idx), data_stack[idx].has_value(), "Index out of range or no data at the index");

    return *data_stack[idx];
  }

  void set_data(const Tree_pos& idx, const X& data) {
    I(_check_idx_exists(idx), "Index out of range");

    data_stack[idx] = data;
  }

  // Use "X operator[](const Tree_pos& idx) const { return get_data(idx); }" to pass data as a const reference
  X operator[](const Tree_pos& idx) { return *data_stack[idx]; }

  /**
   *  Debug API (Temp)
   */
  void print_tree(int deep = 0) {
    for (size_t i = 0; i < pointers_stack.size(); i++) {
      std::cout << "Index: " << (i << CHUNK_SHIFT) << " Parent: " << pointers_stack[i].get_parent()
                << " Data: " << data_stack[i << CHUNK_SHIFT].value_or(-1) << std::endl;
      std::cout << "First Child: " << pointers_stack[i].get_first_child_l() << " ";
      std::cout << "Last Child: " << pointers_stack[i].get_last_child_l() << " ";
      std::cout << "Next Sibling: " << pointers_stack[i].get_next_sibling() << " ";
      std::cout << "Prev Sibling: " << pointers_stack[i].get_prev_sibling() << " ";
      std::cout << "Num Occ: " << pointers_stack[i].get_num_short_del_occ() << std::endl;
      std::cout << "Is Leaf: " << pointers_stack[i].get_is_leaf() << std::endl;
      std::cout << std::endl;
    }

    std::cout << std::endl;

    if (deep) {
      for (size_t i = 0; i < data_stack.size(); i++) {
        if (data_stack[i].has_value()) {
          std::cout << "Index: " << i << " Data: " << data_stack[i].value() << std::endl;
          std::cout << "PAR : " << get_parent(i) << std::endl;
          std::cout << "FC  : " << get_first_child(i) << std::endl;
          std::cout << "LC  : " << get_last_child(i) << std::endl;
          std::cout << "NS  : " << get_sibling_next(i) << std::endl;
          std::cout << "PS  : " << get_sibling_prev(i) << std::endl;
          std::cout << std::endl;
        }
      }
    }
  }

  explicit tree(Forest<X>* forest = nullptr) : forest_ptr(forest) {}

  /**
   * ITERATORS
   * - SIBLING-ORDER(start)
   * - POSTORDER (subtree_parent)
   * - PREORDER (subtree_parent)
   */
  template <typename Derived>
  class traversal_iterator_base {
  protected:
    Tree_pos current;
    const tree<X>* tree_ptr;
    bool m_follow_subtrees;

  public:
    using iterator_category = std::forward_iterator_tag;
    using value_type = Tree_pos;
    using difference_type = std::ptrdiff_t;
    using pointer = Tree_pos*;
    using reference = Tree_pos&;

    traversal_iterator_base(Tree_pos start, const tree<X>* tree, bool follow_refs)
      : current(start), tree_ptr(tree), m_follow_subtrees(follow_refs) {}

    bool operator==(const traversal_iterator_base& other) const { return current == other.current; }
    bool operator!=(const traversal_iterator_base& other) const { return current != other.current; }
    Tree_pos operator*() const { return current; }

  protected:
    // Helper method to handle subtree references
    Tree_pos handle_subtree_ref(Tree_pos pos) {
      if (m_follow_subtrees && tree_ptr->forest_ptr) { // only follow if flag is true
        auto& node = tree_ptr->pointers_stack[pos >> CHUNK_SHIFT];
        if (node.has_subtree_ref()) {
          Tree_pos ref = node.get_subtree_ref();
          if (ref < 0) {
            return ROOT;
          }
        }
      }
      return INVALID;
    }
  };

  // SIBLING ORDER TRAVERSAL
  class sibling_order_iterator : public traversal_iterator_base<sibling_order_iterator> {
  private:
    using base = traversal_iterator_base<sibling_order_iterator>;
    using base::current;
    using base::tree_ptr;
    using base::m_follow_subtrees;

  public:
    using iterator_category = std::forward_iterator_tag;
    using value_type        = Tree_pos;
    using difference_type   = std::ptrdiff_t;
    using pointer           = Tree_pos*;
    using reference         = Tree_pos&;

    sibling_order_iterator(Tree_pos start, tree<X>* tree, bool follow_refs)
      : base(start, tree, follow_refs) {}

    sibling_order_iterator& operator++() {
      Tree_pos subtree_root = this->handle_subtree_ref(this->current);
      if (subtree_root != INVALID) {
        this->current = subtree_root;
        return *this;
      }
      if (tree_ptr->get_sibling_next(current) != INVALID) {
        current = tree_ptr->get_sibling_next(current);
      } else {
        current = INVALID;
      }
      return *this;
    }

    sibling_order_iterator operator++(int) {
      sibling_order_iterator temp = *this;
      ++(*this);
      return temp;
    }
  };

  class sibling_order_range {
  private:
    Tree_pos m_start;
    tree<X>* m_tree_ptr;
    bool m_follow_subtrees;

  public:
    sibling_order_range(Tree_pos start, tree<X>* tree, bool follow_subtrees = false)
      : m_start(start), m_tree_ptr(tree), m_follow_subtrees(follow_subtrees) {}

    sibling_order_iterator begin() { return sibling_order_iterator(m_start, m_tree_ptr, m_follow_subtrees); }
    
    sibling_order_iterator end() { return sibling_order_iterator(INVALID, m_tree_ptr, m_follow_subtrees); }
  };

  sibling_order_range sibling_order(Tree_pos start, bool follow_subtrees = false) {return sibling_order_range(start, this, follow_subtrees); }

  class const_sibling_order_iterator : public traversal_iterator_base<const_sibling_order_iterator> {
  private:
    using base = traversal_iterator_base<const_sibling_order_iterator>;
    using base::current;
    using base::tree_ptr;
    using base::m_follow_subtrees;

  public:
    using iterator_category = std::forward_iterator_tag;
    using value_type = Tree_pos;
    using difference_type = std::ptrdiff_t;
    using pointer = const Tree_pos*;
    using reference = const Tree_pos&;

    const_sibling_order_iterator(Tree_pos start, const tree<X>* tree, bool follow_refs)
      : base(start, tree, follow_refs) {}

    const_sibling_order_iterator& operator++() {
      Tree_pos subtree_root = this->handle_subtree_ref(this->current);
      if (subtree_root != INVALID) {
        this->current = subtree_root;
        return *this;
      }
      if (tree_ptr->get_sibling_next(current) != INVALID) {
        current = tree_ptr->get_sibling_next(current);
      } else {
        current = INVALID;
      }
      return *this;
    }
  };

  class const_sibling_order_range {
  private:
    Tree_pos m_start;
    const tree<X>* m_tree_ptr;
    bool m_follow_subtrees;

  public:
    const_sibling_order_range(Tree_pos start, const tree<X>* tree, bool follow_subtrees = false)
      : m_start(start), m_tree_ptr(tree), m_follow_subtrees(follow_subtrees) {}

    const_sibling_order_iterator begin() const { 
      return const_sibling_order_iterator(m_start, m_tree_ptr, m_follow_subtrees); 
    }
    
    const_sibling_order_iterator end() const { 
      return const_sibling_order_iterator(INVALID, m_tree_ptr, m_follow_subtrees); 
    }
  };

  const_sibling_order_range sibling_order(Tree_pos start) const { return const_sibling_order_range(start, this); }

  // PRE-ORDER TRAVERSAL
  class pre_order_iterator : public traversal_iterator_base<pre_order_iterator> {
  private:
    using base = traversal_iterator_base<pre_order_iterator>;
    using base::current;
    using base::tree_ptr;
    using base::m_follow_subtrees;
    
    std::set<Tree_pos> visited_subtrees;
    tree<X>* current_tree; // Track which tree we're currently traversing
    tree<X>* main_tree;    // Keep reference to main tree
    Tree_pos return_to_node; // Node to return to after subtree traversal

  public:
    using iterator_category = std::forward_iterator_tag;
    using value_type = Tree_pos;
    using difference_type = std::ptrdiff_t;
    using pointer = Tree_pos*;
    using reference = Tree_pos&;

    pre_order_iterator(Tree_pos start, tree<X>* tree, bool follow_refs)
      : base(start, tree, follow_refs), current_tree(tree), main_tree(tree), return_to_node(INVALID) {}

    X get_data() const {
      return current_tree->get_data(current);
    }

    pre_order_iterator& operator++() {
      if (m_follow_subtrees && current_tree->forest_ptr) {
        auto& node = current_tree->pointers_stack[current >> CHUNK_SHIFT];
        if (node.has_subtree_ref()) {
          Tree_pos ref = node.get_subtree_ref();
          if (ref < 0 && visited_subtrees.find(ref) == visited_subtrees.end()) {
            visited_subtrees.insert(ref);
            return_to_node = current;
            current_tree = &(current_tree->forest_ptr->get_tree(ref));
            this->current = ROOT;
            return *this;
          }
        }
      }

      // first try to go to first child
      if (!current_tree->is_leaf(current)) {
        current = current_tree->get_first_child(current);
        return *this;
      }

      // if no children, try to go to next sibling
      auto nxt = current_tree->get_sibling_next(current);
      if (nxt != INVALID) {
        current = nxt;
        return *this;
      }

      // if no next sibling and we're in a subtree, return to main tree
      if (current_tree != main_tree) {
        current_tree = main_tree;
        current = current_tree->get_sibling_next(return_to_node);
        return_to_node = INVALID;
        if (current != INVALID) {
          return *this;
        }
      }

      // if no next sibling, go up to parent's next sibling
      auto parent = current_tree->get_parent(current);
      while (parent != ROOT && parent != INVALID) {
        auto parent_sibling = current_tree->get_sibling_next(parent);
        if (parent_sibling != INVALID) {
          current = parent_sibling;
          return *this;
        }
        parent = current_tree->get_parent(parent);
      }

      // if we'e gone through all possibilities, mark as end
      current = INVALID;
      return *this;
    }

    bool operator==(const pre_order_iterator& other) const { 
      return current == other.current && current_tree == other.current_tree; 
    }

    bool operator!=(const pre_order_iterator& other) const { 
      return !(*this == other); 
    }

    Tree_pos operator*() const { 
      return current; 
    }
  };

  class pre_order_range {
  private:
    Tree_pos m_start;
    tree<X>* m_tree_ptr;
    bool m_follow_subtrees;

  public:
    pre_order_range(Tree_pos start, tree<X>* tree, bool follow_subtrees = false)
      : m_start(start), m_tree_ptr(tree), m_follow_subtrees(follow_subtrees) {}

    pre_order_iterator begin() { return pre_order_iterator(m_start, m_tree_ptr, m_follow_subtrees); }

    pre_order_iterator end() { return pre_order_iterator(INVALID, m_tree_ptr, m_follow_subtrees); }
  };

  pre_order_range pre_order(Tree_pos start = ROOT, bool follow_subtrees = false) {return pre_order_range(start, this, follow_subtrees); }

  class const_pre_order_iterator : public traversal_iterator_base<const_pre_order_iterator> {
  private:
    using base = traversal_iterator_base<const_pre_order_iterator>;
    using base::current;
    using base::tree_ptr;
    using base::m_follow_subtrees;
    
    std::set<Tree_pos> visited_subtrees;
    const tree<X>* current_tree;
    const tree<X>* main_tree;
    Tree_pos return_to_node;

  public:
    using iterator_category = std::forward_iterator_tag;
    using value_type = Tree_pos;
    using difference_type = std::ptrdiff_t;
    using pointer = const Tree_pos*;
    using reference = const Tree_pos&;

    const_pre_order_iterator(Tree_pos start, const tree<X>* tree, bool follow_refs)
      : base(start, tree, follow_refs), current_tree(tree), main_tree(tree), return_to_node(INVALID) {}

    const X get_data() const {
      return current_tree->get_data(current);
    }

    const_pre_order_iterator& operator++() {
      if (m_follow_subtrees && current_tree->forest_ptr) {
        auto& node = current_tree->pointers_stack[current >> CHUNK_SHIFT];
        if (node.has_subtree_ref()) {
          Tree_pos ref = node.get_subtree_ref();
          if (ref < 0 && visited_subtrees.find(ref) == visited_subtrees.end()) {
            visited_subtrees.insert(ref);
            return_to_node = current;
            current_tree = &(current_tree->forest_ptr->get_tree(ref));
            this->current = ROOT;
            return *this;
          }
        }
      }

      // first try to go to first child
      if (!current_tree->is_leaf(current)) {
        current = current_tree->get_first_child(current);
        return *this;
      }

      // if no children, try to go to next sibling
      auto nxt = current_tree->get_sibling_next(current);
      if (nxt != INVALID) {
        current = nxt;
        return *this;
      }

      // if no next sibling and we're in a subtree, return to main tree
      if (current_tree != main_tree) {
        current_tree = main_tree;
        current = current_tree->get_sibling_next(return_to_node);
        return_to_node = INVALID;
        if (current != INVALID) {
          return *this;
        }
      }

      // if no next sibling, go up to parent's next sibling
      auto parent = current_tree->get_parent(current);
      while (parent != ROOT && parent != INVALID) {
        auto parent_sibling = current_tree->get_sibling_next(parent);
        if (parent_sibling != INVALID) {
          current = parent_sibling;
          return *this;
        }
        parent = current_tree->get_parent(parent);
      }

      // if we'e gone through all possibilities, mark as end
      current = INVALID;
      return *this;
    }

    bool operator==(const const_pre_order_iterator& other) const { 
      return current == other.current && current_tree == other.current_tree; 
    }

    bool operator!=(const const_pre_order_iterator& other) const { 
      return !(*this == other); 
    }

    Tree_pos operator*() const { 
      return current; 
    }
  };

  class const_pre_order_range {
  private:
    Tree_pos m_start;
    const tree<X>* m_tree_ptr;
    bool m_follow_subtrees;

  public:
    const_pre_order_range(Tree_pos start, const tree<X>* tree, bool follow_subtrees = false)
      : m_start(start), m_tree_ptr(tree), m_follow_subtrees(follow_subtrees) {}

    const_pre_order_iterator begin() const { 
      return const_pre_order_iterator(m_start, m_tree_ptr, m_follow_subtrees); 
    }

    const_pre_order_iterator end() const { 
      return const_pre_order_iterator(INVALID, m_tree_ptr, m_follow_subtrees); 
    }
  };

  const_pre_order_range pre_order(Tree_pos start = ROOT) const { return const_pre_order_range(start, this); }

  // POST-ORDER TRAVERSAL
  class post_order_iterator : public traversal_iterator_base<post_order_iterator> {
  private:
    using base = traversal_iterator_base<post_order_iterator>;
    using base::current;
    using base::tree_ptr;
    using base::m_follow_subtrees;

  public:
    using iterator_category = std::forward_iterator_tag;
    using value_type        = Tree_pos;
    using difference_type   = std::ptrdiff_t;
    using pointer           = Tree_pos*;
    using reference         = Tree_pos&;

    post_order_iterator(Tree_pos start, tree<X>* tree, bool follow_refs)
      : base(start, tree, follow_refs) {}

    post_order_iterator& operator++() {
      Tree_pos subtree_root = this->handle_subtree_ref(this->current);
      if (subtree_root != INVALID) {
        this->current = subtree_root;
        return *this;
      }

      if (tree_ptr->get_sibling_next(current) != INVALID) {
        auto next = tree_ptr->get_sibling_next(current);
        while (tree_ptr->get_sibling_next(next) != INVALID) {
          next = tree_ptr->get_first_child(next);
        }

        current = next;
      } else {
        current = tree_ptr->get_parent(current);
      }
      return *this;
    }

    post_order_iterator operator++(int) {
      post_order_iterator temp = *this;
      ++(*this);
      return temp;
    }
  };

  class post_order_range {
  private:
    Tree_pos m_start;
    tree<X>* m_tree_ptr;
    bool m_follow_subtrees;

  public:
    post_order_range(Tree_pos start, tree<X>* tree, bool follow_subtrees = false)
      : m_start(start), m_tree_ptr(tree), m_follow_subtrees(follow_subtrees) {}

    post_order_iterator begin() { return post_order_iterator(m_start, m_tree_ptr, m_follow_subtrees); }

    post_order_iterator end() { return post_order_iterator(INVALID, m_tree_ptr, m_follow_subtrees); }
  };

  post_order_range post_order(Tree_pos start = ROOT, bool follow_subtrees = false) {return post_order_range(start, this, follow_subtrees); }

  class const_post_order_iterator : public traversal_iterator_base<const_post_order_iterator> {
  private:
    using base = traversal_iterator_base<const_post_order_iterator>;
    using base::current;
    using base::tree_ptr;
    using base::m_follow_subtrees;

  public:
    using iterator_category = std::forward_iterator_tag;
    using value_type        = Tree_pos;
    using difference_type   = std::ptrdiff_t;
    using pointer           = const Tree_pos*;
    using reference         = const Tree_pos&;

    const_post_order_iterator(Tree_pos start, const tree<X>* tree, bool follow_refs)
      : base(start, tree, follow_refs) {}

    const_post_order_iterator& operator++() {
      Tree_pos subtree_root = this->handle_subtree_ref(this->current);
      if (subtree_root != INVALID) {
        this->current = subtree_root;
        return *this;
      }

      if (tree_ptr->get_sibling_next(current) != INVALID) {
        auto next = tree_ptr->get_sibling_next(current);
        while (tree_ptr->get_sibling_next(next) != INVALID) {
          next = tree_ptr->get_first_child(next);
        }
        current = next;
      } else {
        current = tree_ptr->get_parent(current);
      }
      return *this;
    }

    bool operator==(const const_post_order_iterator& other) const { return current == other.current; }

    bool operator!=(const const_post_order_iterator& other) const { return current != other.current; }

    Tree_pos  operator*() const { return current; }
  };

  class const_post_order_range {
  private:
    Tree_pos m_start;
    const tree<X>* m_tree_ptr;
    bool m_follow_subtrees;

  public:
    const_post_order_range(Tree_pos start, const tree<X>* tree, bool follow_subtrees = false)
      : m_start(start), m_tree_ptr(tree), m_follow_subtrees(follow_subtrees) {}

    const_post_order_iterator begin() const { 
      return const_post_order_iterator(m_start, m_tree_ptr, m_follow_subtrees); 
    }

    const_post_order_iterator end() const { 
      return const_post_order_iterator(INVALID, m_tree_ptr, m_follow_subtrees); 
    }
  };

  const_post_order_range post_order(Tree_pos start = ROOT) const { 
    return const_post_order_range(start, this); 
  }

  // move helper methods inside tree class
  [[nodiscard]] bool is_subtree_ref(Tree_pos pos) const {
    return pos < 0;
  }

  [[nodiscard]] size_t get_subtree_index(Tree_pos pos) const {
    return static_cast<size_t>(-pos - 1);
  }

  [[nodiscard]] Tree_pos make_subtree_ref(size_t subtree_index) const {
    return static_cast<Tree_pos>(-(subtree_index + 1));
  }

  [[nodiscard]] Tree_pos get_subtree_ref(Tree_pos pos) const {
    return pointers_stack[pos >> CHUNK_SHIFT].get_subtree_ref();
  }

  // :public

};  // tree class
