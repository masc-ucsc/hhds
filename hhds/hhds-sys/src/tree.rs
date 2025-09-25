use std::os::raw::{c_int, c_void};

include!(concat!(env!("OUT_DIR"), "/bindings.rs"));

pub struct Forest {
    handle: ForestIntHandle,
}

impl Default for Forest {
    fn default() -> Self {
        Self::new()
    }
}

impl Forest {
    pub fn new() -> Self {
        Self {
            handle: unsafe { forest_int_new() },
        }
    }

    pub fn create_tree(&self, val: c_int) -> hhds_Tree_pos {
        unsafe { forest_int_create_tree(self.handle, val) }
    }

    pub fn get_tree(&self, tree_ref: hhds_Tree_pos) -> Tree {
        Tree::new(unsafe { forest_int_get_tree(self.handle, tree_ref) })
    }

    pub fn delete_tree(&self, tree_ref: hhds_Tree_pos) -> bool {
        unsafe { forest_int_delete_tree(self.handle, tree_ref) }
    }
}

pub struct Tree {
    handle: *mut c_void,
}

impl Tree {
    // Internal function for Forest
    fn new(tree_ref: *mut c_void) -> Self {
        Self { handle: tree_ref }
    }

    pub fn new_no_ref() -> Self {
        let handle = unsafe { tree_int_new_empty() };
        Self { handle }
    }

    pub fn get_root(&self) -> hhds_Tree_pos {
        unsafe { get_root(self.handle) }
    }

    pub fn get_parent(&self, node_pos: hhds_Tree_pos) -> hhds_Tree_pos {
        unsafe { get_parent(self.handle, node_pos) }
    }

    pub fn get_first_child(&self, parent_pos: hhds_Tree_pos) -> hhds_Tree_pos {
        unsafe { get_first_child(self.handle, parent_pos) }
    }

    pub fn get_last_child(&self, parent_pos: hhds_Tree_pos) -> hhds_Tree_pos {
        unsafe { get_last_child(self.handle, parent_pos) }
    }

    pub fn get_sibling_next(&self, sibling_pos: hhds_Tree_pos) -> hhds_Tree_pos {
        unsafe { get_sibling_next(self.handle, sibling_pos) }
    }

    pub fn get_sibling_prev(&self, sibling_pos: hhds_Tree_pos) -> hhds_Tree_pos {
        unsafe { get_sibling_prev(self.handle, sibling_pos) }
    }

    pub fn get_data(&self, tree_ref: hhds_Tree_pos) -> c_int {
        unsafe { tree_get_data(self.handle, tree_ref) }
    }

    pub fn add_root(&self, data: i32) -> hhds_Tree_pos {
        unsafe { add_root(self.handle, data) }
    }

    pub fn add_child(&self, parent_idx: hhds_Tree_pos, data: c_int) -> hhds_Tree_pos {
        unsafe { add_child(self.handle, parent_idx, data) }
    }

    pub fn append_sibling(&self, sibling_idx: hhds_Tree_pos, data: c_int) -> hhds_Tree_pos {
        unsafe { append_sibling(self.handle, sibling_idx, data) }
    }

    pub fn insert_next_sibling(&self, sibling_idx: hhds_Tree_pos, data: c_int) -> hhds_Tree_pos {
        unsafe { insert_next_sibling(self.handle, sibling_idx, data) }
    }

    pub fn add_subtree_ref(&self, node_pos: hhds_Tree_pos, subtree_ref: hhds_Tree_pos) {
        unsafe { add_subtree_ref(self.handle, node_pos, subtree_ref) }
    }

    pub fn delete_leaf(&self, node_pos: hhds_Tree_pos) {
        unsafe { delete_leaf(self.handle, node_pos) }
    }

    pub fn delete_subtree(&self, subtree_root: hhds_Tree_pos) {
        unsafe { delete_subtree(self.handle, subtree_root) }
    }

    pub fn pre_ord_iter(&self, follow_subtrees: bool) -> PreOrderIterator {
        PreOrderIterator::new(self, follow_subtrees)
    }
}

pub struct PreOrderIterator {
    pub handle: *mut c_void,
    initial: bool,
}

impl PreOrderIterator {
    pub fn new(tree: &Tree, follow_subtrees: bool) -> Self {
        Self {
            handle: unsafe {
                get_pre_order_iterator(tree.handle, tree.get_root(), follow_subtrees)
            },
            initial: true,
        }
    }

    pub fn deref(&self) -> i64 {
        unsafe { deref_pre_order_iterator(self.handle) }
    }

    pub fn get_data(&self) -> c_int {
        unsafe { get_data_pre_order_iter(self.handle) }
    }
}

/*
 * Currently this iterates and returns reference IDs.
 * Need to call get_data() before every iteration to return data of specified reference.
 */
impl Iterator for PreOrderIterator {
    type Item = Self;

    fn next(&mut self) -> Option<Self::Item> {
        if self.initial {
            self.initial = false;
            return Some(Self {
                handle: self.handle,
                initial: false,
            });
        }
        self.handle = unsafe { increment_pre_order_iterator(self.handle) };
        match unsafe { deref_pre_order_iterator(self.handle) } {
            val if val <= 0 => None,
            _val => Some(Self {
                handle: self.handle,
                initial: false,
            }),
        }
    }
}

// TODO: PostOrder Not implemented
pub struct PostOrderIterator {
    pub handle: *mut c_void,
}

impl PostOrderIterator {
    pub fn new(tree: &Tree, follow_subtrees: bool) -> Self {
        Self {
            handle: unsafe {
                get_post_order_iterator(tree.handle, tree.get_root(), follow_subtrees)
            },
        }
    }

    pub fn deref(&self) -> i64 {
        unsafe { deref_post_order_iterator(self.handle) }
    }

    pub fn get_data(&self) -> c_int {
        unsafe { get_data_post_order_iter(self.handle) }
    }
}

impl Iterator for PostOrderIterator {
    type Item = c_int;

    fn next(&mut self) -> Option<Self::Item> {
        let val = match unsafe { deref_post_order_iterator(self.handle) } {
            val if val <= 0 => return None,
            _ => Some(self.get_data()),
        };
        self.handle = unsafe { increment_post_order_iterator(self.handle) };
        val
    }
}
