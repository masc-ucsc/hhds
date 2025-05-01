use std::os::raw::{c_int, c_void};

include!("../target/debug/build/bindgen-test-88895d99f6a8c150/out/bindings.rs");

pub struct Forest {
    handle: ForestIntHandle,
}

impl Forest {
    pub fn new() -> Self {
        Self {
            handle: unsafe { forest_int_new() },
        }
    }

    pub fn create_tree(&self, val: c_int) -> hhds_Tree_pos {
        return unsafe { forest_int_create_tree(self.handle, val) };
    }

    pub fn get_tree(&self, tree_ref: hhds_Tree_pos) -> Tree {
        return Tree::new(unsafe { forest_int_get_tree(self.handle, tree_ref) });
    }

    pub fn delete_tree(&self, tree_ref: hhds_Tree_pos) -> bool {
        unsafe { forest_int_delete_tree(self.handle, tree_ref) }
    }
}

pub struct Tree {
    pub handle: *mut c_void,
}

impl Tree {
    fn new(tree_ref: *mut c_void) -> Self {
        Self { handle: tree_ref }
    }

    pub fn get_root(&self) -> hhds_Tree_pos {
        unsafe { get_root(self.handle) }
    }

    pub fn get_data(&self, tree_ref: hhds_Tree_pos) -> c_int {
        unsafe { tree_get_data(self.handle, tree_ref) }
    }

    pub fn add_child(&self, data: c_int) -> hhds_Tree_pos {
        unsafe { add_child(self.handle, self.get_root(), data) }
    }

    pub fn add_subtree_ref(&self, node_pos: hhds_Tree_pos, subtree_ref: hhds_Tree_pos) {
        unsafe { add_subtree_ref(self.handle, node_pos, subtree_ref) }
    }

    pub fn delete_leaf(&self, leaf_index: hhds_Tree_pos) {
        unsafe { delete_leaf(self.handle, leaf_index) }
    }

    pub fn pre_ord_iter(&self) -> PreOrderIterator {
        PreOrderIterator::new(&self)
    }
}

pub struct PreOrderIterator {
    handle: *mut c_void,
}
impl PreOrderIterator {
    pub fn new(tree: &Tree) -> Self {
        Self {
            handle: unsafe { get_pre_order_iterator(tree.handle, false) },
        }
    }

    pub fn deref(&self) -> i64 {
        return unsafe { deref_pre_order_iterator(self.handle) };
    }

    pub fn get_data(&self) -> c_int {
        unsafe { get_data_pre_order_iter(self.handle) }
    }
}
impl Iterator for PreOrderIterator {
    type Item = hhds_Tree_pos;

    fn next(&mut self) -> Option<Self::Item> {
        let val = match unsafe { deref_pre_order_iterator(self.handle) } {
            0 => None,
            val => Some(val),
        };
        self.handle = unsafe { increment_pre_order_iterator(self.handle) };
        return val;
    }
}
