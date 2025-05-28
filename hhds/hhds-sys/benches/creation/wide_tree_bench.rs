use criterion::{black_box, criterion_group, criterion_main, Criterion};
use hhds_sys::tree::Tree;
use rand::rngs::StdRng;
use rand::{Rng, SeedableRng};
use std::env;
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
    for _i in 0..num_nodes {
        tree.add_child(root, generate_random_int(rng, 1, 100));
    }
}
fn bench_tree_traversal(c: &mut Criterion, num_nodes: u32) {
    let mut group = c.benchmark_group("TreeTraversal");
    group.sample_size(10);
    group.bench_function(format!("preorder_{}_nodes", num_nodes), |b| {
        let mut rng = create_rng();
        b.iter(|| {
            let tree = Tree::new_no_ref();
            build_tree(&mut rng, black_box(&tree), num_nodes);
        });
    });

    group.finish()
}

fn test_wide_tree(c: &mut Criterion) {
    let nodes = env::var("NODES")
        .ok()
        .and_then(|v| v.parse().ok())
        .unwrap_or(10);
    bench_tree_traversal(c, nodes);
}

criterion_group!(benches, test_wide_tree);
criterion_main!(benches);
