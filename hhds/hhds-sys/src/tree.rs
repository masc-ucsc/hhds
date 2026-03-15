use std::os::raw::c_int;

include!(concat!(env!("OUT_DIR"), "/bindings.rs"));

pub struct Tree {
    handle: hhds_TreeHandle,
}

impl Tree {
    pub fn new_no_ref() -> Self {
        Self {
            handle: unsafe { tree_int_new_empty() },
        }
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

    pub fn add_root(&self, data_ignored: i32) -> hhds_Tree_pos {
        unsafe { add_root(self.handle, data_ignored) }
    }

    pub fn add_child(&self, parent_idx: hhds_Tree_pos, data_ignored: c_int) -> hhds_Tree_pos {
        unsafe { add_child(self.handle, parent_idx, data_ignored) }
    }

    pub fn append_sibling(&self, sibling_idx: hhds_Tree_pos, data_ignored: c_int) -> hhds_Tree_pos {
        unsafe { append_sibling(self.handle, sibling_idx, data_ignored) }
    }

    pub fn insert_next_sibling(&self, sibling_idx: hhds_Tree_pos, data_ignored: c_int) -> hhds_Tree_pos {
        unsafe { insert_next_sibling(self.handle, sibling_idx, data_ignored) }
    }

    pub fn delete_leaf(&self, node_pos: hhds_Tree_pos) {
        unsafe { delete_leaf(self.handle, node_pos) }
    }

    pub fn delete_subtree(&self, subtree_root: hhds_Tree_pos) {
        unsafe { delete_subtree(self.handle, subtree_root) }
    }

    pub fn set_subnode(&self, node_pos: hhds_Tree_pos, subnode_ref: hhds_Tree_pos) {
        unsafe { set_subnode(self.handle, node_pos, subnode_ref) }
    }

    pub fn pre_ord_iter(&self, _follow_subnodes: bool) -> PreOrderIterator<'_> {
        PreOrderIterator::new(self)
    }
}

impl Drop for Tree {
    fn drop(&mut self) {
        unsafe { tree_int_delete(self.handle) }
    }
}

#[derive(Clone, Copy)]
pub struct PreOrderNode {
    pos: hhds_Tree_pos,
}

impl PreOrderNode {
    pub fn deref(&self) -> hhds_Tree_pos {
        self.pos
    }
}

pub struct PreOrderIterator<'a> {
    tree: &'a Tree,
    next_pos: hhds_Tree_pos,
}

impl<'a> PreOrderIterator<'a> {
    fn new(tree: &'a Tree) -> Self {
        Self {
            tree,
            next_pos: tree.get_root(),
        }
    }

    fn advance(&self, current: hhds_Tree_pos) -> hhds_Tree_pos {
        let child = self.tree.get_first_child(current);
        if child > 0 {
            return child;
        }

        let mut cursor = current;
        loop {
            let sibling = self.tree.get_sibling_next(cursor);
            if sibling > 0 {
                return sibling;
            }

            let parent = self.tree.get_parent(cursor);
            if parent <= 0 {
                return 0;
            }
            cursor = parent;
        }
    }
}

impl Iterator for PreOrderIterator<'_> {
    type Item = PreOrderNode;

    fn next(&mut self) -> Option<Self::Item> {
        if self.next_pos <= 0 {
            return None;
        }

        let current = self.next_pos;
        self.next_pos = self.advance(current);
        Some(PreOrderNode { pos: current })
    }
}
