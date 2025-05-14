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

    // Build the trees
    let child1 = main_tree.add_child(main_tree.get_root(), 2);
    main_tree.add_child(main_tree.get_root(), 3);

    sub_tree.add_child(sub_tree.get_root(), 11);
    sub_tree.add_child(sub_tree.get_root(), 12);

    // Add the subtree reference
    main_tree.add_subtree_ref(child1, sub_tree_ref);

    // Test subtree traversal without following subtrees
    let mut visited_values: Vec<i32> = Vec::new();
    for i in sub_tree.pre_ord_iter(true) {
        visited_values.push(i.get_data());
    }
    assert_eq!(visited_values, vec![10, 11, 12]);

    // Test main tree traversal without following subtrees
    visited_values.clear();
    for i in main_tree.pre_ord_iter(false) {
        visited_values.push(i.get_data());
    }
    assert_eq!(visited_values, vec![1, 2, 3]);

    // Test main tree traversal with following subtrees
    visited_values.clear();
    for i in main_tree.pre_ord_iter(true) {
        visited_values.push(i.get_data());
    }
    assert_eq!(visited_values.len(), 6);
    assert_eq!(visited_values, vec![1, 2, 10, 11, 12, 3]);
}

#[test]
pub fn test_complex_forest_operations() {
    let forest = Forest::new();

    let main_tree_ref = forest.create_tree(1);
    let sub_tree1_ref = forest.create_tree(1000);
    let sub_tree2_ref = forest.create_tree(2000);
    let sub_tree3_ref = forest.create_tree(3000);

    let main_tree = forest.get_tree(main_tree_ref);
    let sub_tree1 = forest.get_tree(sub_tree1_ref);
    let sub_tree2 = forest.get_tree(sub_tree2_ref);
    let sub_tree3 = forest.get_tree(sub_tree3_ref);

    // build tree structures
    let mut main_nodes: Vec<hhds_sys::hhds_Tree_pos> = Vec::new();
    let mut sub1_nodes: Vec<hhds_sys::hhds_Tree_pos> = Vec::new();
    let mut sub2_nodes: Vec<hhds_sys::hhds_Tree_pos> = Vec::new();
    let mut sub3_nodes: Vec<hhds_sys::hhds_Tree_pos> = Vec::new();

    // create a deep main tree with multiple branches
    main_nodes.push(main_tree.get_root());
    for i in 0..100 {
        let parent = main_nodes[i / 3];
        let new_node = main_tree.add_child(parent, 2 + i as i32);
        main_nodes.push(new_node);
    }

    // create wide sub_tree1 with many siblings
    sub1_nodes.push(sub_tree1.get_root());
    let sub1_parent = sub_tree1.get_root();
    for i in 0..50 {
        let new_node = sub_tree1.add_child(sub1_parent, 1100 + i);
        sub1_nodes.push(new_node);
    }

    // create balanced sub_tree2
    sub2_nodes.push(sub_tree2.get_root());
    for i in 0..32 {
        let parent = sub2_nodes[i];
        let left = sub_tree2.add_child(parent, 2100 + i as i32 * 2);
        let right = sub_tree2.add_child(parent, 2100 + i as i32 * 2 + 1);
        sub2_nodes.push(left);
        sub2_nodes.push(right);
    }

    // create deep chain in sub_tree3
    sub3_nodes.push(sub_tree3.get_root());
    let mut current = sub_tree3.get_root();
    for i in 0..100 {
        let new_node = sub_tree3.add_child(current, 3100 + i);
        sub3_nodes.push(new_node);
        current = new_node;
    }

    // create complex reference pattern
    main_tree.add_subtree_ref(main_nodes[0], sub_tree1_ref);
    sub_tree1.add_subtree_ref(sub1_nodes[0], sub_tree2_ref);
    sub_tree2.add_subtree_ref(sub2_nodes[0], sub_tree3_ref);

    let deleted = forest.delete_tree(sub_tree3_ref);
    assert!(!deleted);

    let mut unique_values = std::collections::HashSet::new();
    let mut count = 0;
    for i in main_tree.pre_ord_iter(true) {
        unique_values.insert(i.get_data());
        count += 1;
        assert!(count <= 10_000);
    }
    assert!(
        count >= 318,
        "Should visit at least 318 nodes! Visited {} nodes.",
        count
    );
    assert!(
        unique_values.len() > 200,
        "Should see at least 200 unique values."
    );

    // TODO: Work on deletions
    for i in (0..main_nodes.len() - 1).rev().step_by(10) {
        if i < main_nodes.len() {
            main_tree.delete_leaf(main_nodes[i]);
        }
    }
}

/*
#[test]
pub fn test_edge_cases() {
    let forest = Forest::new();

    // test creation of more trees
    let mut tree_refs: Vec<hhds_sys::hhds_Tree_pos>= Vec::new();
    for i in 0..5{
        let refr = forest.create_tree(i);
        tree_refs.push(refr);

        let tree = forest.get_tree(refr);
        assert_eq!(tree.get_data(tree.get_root()), i, "Tree creation verification failed");
    }
    // create chain of references with two children per tree
    //for i in 0..tree_refs.len() - 1 {
        let current_tree = forest.get_tree(tree_refs[0]);
        let root = current_tree.get_root();
        assert_eq!(current_tree.get_data(root) , 0);

        let child = current_tree.add_child(root, 10);
        assert_eq!(current_tree.get_data(child), 10);

        current_tree.add_subtree_ref(child, tree_refs[1]);
        let child = current_tree.add_child(root, 11);
        return;
    //}

    // create single circular reference
    let last_tree = forest.get_tree(*tree_refs.last().unwrap());
    let last_child = last_tree.add_child(last_tree.get_root(), 100);
    assert_eq!(last_tree.get_data(last_child), 100);
}
*/
