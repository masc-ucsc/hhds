use criterion::{black_box, criterion_group, criterion_main, Criterion};
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
    rng.random_range(min..max)
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
    black_box(cnt);
}

fn bench_tree_traversal(c: &mut Criterion, num_nodes: u32) {
    let mut group = c.benchmark_group("TreeTraversal");

    group.measurement_time(std::time::Duration::from_secs(60));
    group.bench_function(format!("preorder_{}_nodes", num_nodes), |b| {
        let mut rng = create_rng();
        let tree = Tree::new_no_ref();
        build_tree(&mut rng, &tree, num_nodes);
        b.iter(|| {
            pre_order_traversal(black_box(&tree));
        })
    });

    group.finish()
}

fn test_deep_tree(c: &mut Criterion) {
    bench_tree_traversal(c, 10);
    bench_tree_traversal(c, 100);
    bench_tree_traversal(c, 100);
    bench_tree_traversal(c, 1000);
    bench_tree_traversal(c, 10_000);
    //bench_tree_traversal(c, 100_000);
    //bench_tree_traversal(c, 1_000_000);
    //bench_tree_traversal(c, 10_000_000);
}

criterion_group!(benches, test_deep_tree);
criterion_main!(benches);
