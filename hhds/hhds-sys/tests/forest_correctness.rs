use hhds_sys::tree::Forest;

include!(concat!(env!("OUT_DIR"), "/bindings.rs"));

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
#[test]
fn test_subtree_references() {
    let forest = Forest::new();

    let main_tree_ref = forest.create_tree(1);
    let sub_tree_ref = forest.create_tree(2);

    let main_tree = forest.get_tree(main_tree_ref);
    let sub_tree = forest.get_tree(sub_tree_ref);

    let child1 = main_tree.add_child(main_tree.get_root(), 10);
    let _child2 = main_tree.add_child(main_tree.get_root(), 11);

    let _sub_child = sub_tree.add_child(sub_tree.get_root(), 20);

    // add subtree_reference
    main_tree.add_subtree_ref(child1, sub_tree_ref);

    // verify reference counting - should return false and not delete
    let deleted = forest.delete_tree(sub_tree_ref);
    assert!(!deleted);

    // tree should still be accessible
    let still_there = forest.get_tree(sub_tree_ref);
    assert_eq!(still_there.get_data(still_there.get_root()), 2);
    main_tree.delete_leaf(child1);

    let deleted = forest.delete_tree(sub_tree_ref);
    assert!(deleted);
}
#[test]
pub fn test_tree_traversal_with_subtrees() {
    let forest = Forest::new();

    let main_tree_ref = forest.create_tree(1);
    let sub_tree_ref = forest.create_tree(10);

    let main_tree = forest.get_tree(main_tree_ref);
    let sub_tree = forest.get_tree(sub_tree_ref);

    // Build the subtree
    sub_tree.add_child(sub_tree.get_root(), 11);
    sub_tree.add_child(sub_tree.get_root(), 12);

    // Build the main tree
    let child1 = main_tree.add_child(main_tree.get_root(), 2);
    main_tree.add_child(main_tree.get_root(), 3);

    // Add the subtree reference
    main_tree.add_subtree_ref(child1, sub_tree_ref);

    // Test subtree traversal without following subtrees
    let mut visited_values: Vec<i32> = Vec::new();
    for i in sub_tree.pre_ord_iter(true) {
        visited_values.push(i);
    }
    assert_eq!(visited_values, vec![10, 11, 12]);

    // Test main tree traversal without following subtrees
    visited_values.clear();
    for i in main_tree.pre_ord_iter(false) {
        visited_values.push(i);
    }
    assert_eq!(visited_values, vec![1, 2, 3]);

    // Test main tree traversal with following subtrees
    visited_values.clear();
    for i in main_tree.pre_ord_iter(true) {
        println!("{}", i);
        visited_values.push(i);
    }
    assert_eq!(visited_values, vec![1, 2, 10, 11, 12, 3]);
}
/*
*/
