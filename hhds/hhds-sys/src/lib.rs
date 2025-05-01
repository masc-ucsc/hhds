#![allow(non_upper_case_globals)]
#![allow(non_camel_case_types)]
#![allow(non_snake_case)]

mod tree;

use std::os::raw::c_void;
use tree::{Forest, PreOrderIterator, Tree};
include!("../target/debug/build/bindgen-test-88895d99f6a8c150/out/bindings.rs");

pub fn add(left: u64, right: u64) -> u64 {
    left + right
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_structs() {
        let forest = Forest::new();
        let tree_ref = forest.create_tree(1);
        assert!(tree_ref < 0);
        let tree = forest.get_tree(tree_ref);
        let mut a = tree.pre_ord_iter();
        assert_eq!(a.next(), Some(8));
        assert_eq!(a.next(), Some(33));
    }

    #[test]
    fn test_subtree_references() {
        let forest = Forest::new();
        let main_tree_ref = forest.create_tree(1);
        let sub_tree_ref = forest.create_tree(2);

        let main_tree = forest.get_tree(main_tree_ref);
        let sub_tree = forest.get_tree(sub_tree_ref);

        let child1 = main_tree.add_child(10);
        let child2 = main_tree.add_child(11);
        let sub_child = sub_tree.add_child(20);
        main_tree.add_subtree_ref(child1, sub_tree_ref);
        let deleted = forest.delete_tree(sub_tree_ref);
        assert!(!deleted);
        let still_there = forest.get_tree(sub_tree_ref);
        assert_eq!(still_there.get_data(still_there.get_root()), 2);
        main_tree.delete_leaf(child1);
        let deleted = forest.delete_tree(sub_tree_ref);
        assert!(deleted);
    }
    #[test]
    fn test_basic_forest_operations() {
        let forest = Forest::new();
        let tree1_ref = forest.create_tree(1);
        let tree2_ref = forest.create_tree(2);

        assert!(tree1_ref < 0);
        assert!(tree2_ref < 0);
        assert_ne!(tree1_ref, tree2_ref);
        let tree1 = forest.get_tree(tree1_ref);
        let tree2 = forest.get_tree(tree2_ref);

        let tree1_root = tree1.get_root();
        let tree2_root = tree2.get_root();

        assert_eq!(tree1.get_data(tree1_root), 1);
        assert_eq!(tree2.get_data(tree2_root), 2);
    }
}
