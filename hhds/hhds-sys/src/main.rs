use hhds_sys::tree::Tree;
use std::env::args;

fn main() {
    let mut arguments = args();
    arguments.next();
    let num_nodes: u32 = arguments
        .next()
        .unwrap_or(String::from("10"))
        .parse()
        .unwrap();
    let tree = Tree::new_no_ref();
    let root = tree.add_root(1);
    let mut current = root;
    for i in 0..(num_nodes - 1) {
        current = tree.add_child(current, i as i32);
    }
}
