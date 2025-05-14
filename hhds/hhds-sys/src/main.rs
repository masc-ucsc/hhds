use hhds_sys::tree::Tree;
use rand::rngs::StdRng;
use rand::{Rng, SeedableRng};
use std::time::{SystemTime, UNIX_EPOCH};

fn create_rng() -> StdRng {
    let now = SystemTime::now().duration_since(UNIX_EPOCH).unwrap();
    let micros = now.as_secs() * 1_000_000 + u64::from(now.subsec_micros());

    StdRng::seed_from_u64(micros)
}
fn generate_random_int(rng: &mut StdRng, min: i32, max: i32) -> i32 {
    rng.gen_range(min..=max)
}

fn build_tree(rng: &mut StdRng, tree: &Tree, num_nodes: u32) {
    let root = tree.add_root(generate_random_int(rng, 1, 100));
    let mut current = root;
    for _i in 0..num_nodes {
        current = tree.add_child(current, generate_random_int(rng, 1, 100));
    }
}

fn pre_order_traversal(tree: &Tree) {
    let mut cnt = 0;
    for _node in tree.pre_ord_iter(true) {
        cnt += 1;
    }
}

fn main() {
    let mut rng = create_rng();
    let tree = Tree::new_no_ref();
    build_tree(&mut rng, &tree, 1_000_000);
    pre_order_traversal(&tree)
}
