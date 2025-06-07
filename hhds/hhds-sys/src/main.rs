use hhds_sys::tree::Tree;
use std::env::args;

fn main() {
    let mut arguments = args();
    arguments.next();
    let num_nodes: u32 = arguments
        .next()
        .unwrap_or(String::from("10"))
        .parse()
        .unwrap_or_else(|_| {
            eprintln!("Invalid number format, using default of 10");
            10
        });

    if num_nodes == 0 {
        eprintln!("Number of nodes must be greater than 0");
        return;
    }
    let tree = Tree::new_no_ref();
    let root = tree.add_root(1);
    let mut current = root;
    for i in 0..(num_nodes - 1) {
        current = tree.add_child(current, i as i32);
    }
}
