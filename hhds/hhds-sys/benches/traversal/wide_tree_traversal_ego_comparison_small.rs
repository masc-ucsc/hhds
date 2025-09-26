use criterion::{criterion_group, criterion_main, Criterion};
use hhds_sys::tree::Tree as HhdsTree;
use ego_tree::Tree as EgoTree;
use rand::rngs::StdRng;
use rand::{Rng, SeedableRng};
use std::time::{SystemTime, UNIX_EPOCH};

fn create_rng() -> StdRng {
    let now = SystemTime::now().duration_since(UNIX_EPOCH).unwrap();
    let micros = now.as_secs() * 1_000_000 + u64::from(now.subsec_micros());
    StdRng::seed_from_u64(micros)
}

fn generate_random_int(rng: &mut StdRng, min: i32, max: i32) -> i32 {
    rng.random_range(min..=max)
}

fn build_hhds_tree(rng: &mut StdRng, num_nodes: u32) -> HhdsTree {
    let tree = HhdsTree::new_no_ref();
    let root = tree.add_root(generate_random_int(rng, 1, 100));

    for _ in 0..num_nodes {
        tree.add_child(root, generate_random_int(rng, 1, 100));
    }

    tree
}

fn build_ego_tree(rng: &mut StdRng, num_nodes: u32) -> EgoTree<i32> {
    let mut tree = EgoTree::new(generate_random_int(rng, 1, 100));
    let root_id = tree.root().id();

    for _ in 0..num_nodes {
        tree.get_mut(root_id).unwrap().append(generate_random_int(rng, 1, 100));
    }

    tree
}

// Preorder traversal for HHDS tree
fn preorder_traversal_hhds(tree: &HhdsTree) -> i32 {
    let mut cnt = 0;
    for _node in tree.pre_ord_iter(false) {
        // Equivalent to accessing tree[node] but we just count
        cnt += 1;
    }
    cnt
}

// Preorder traversal for ego_tree
fn preorder_traversal_ego(tree: &EgoTree<i32>) -> i32 {
    let mut cnt = 0;
    for _node in tree.root().traverse() {
        cnt += 1;
    }
    cnt
}

fn bench_wide_tree_traversal_comparison(c: &mut Criterion) {
    // Reduced set for faster completion
    let node_counts = vec![50, 100, 500, 1000];

    for num_nodes in node_counts {
        let mut group = c.benchmark_group(format!("wide_tree_traversal_{}_nodes", num_nodes));
        group.sample_size(30); // Reduced sample size for faster completion

        // Create trees once for traversal benchmarks
        let mut rng = create_rng();
        let hhds_tree = build_hhds_tree(&mut rng, num_nodes);
        let ego_tree = build_ego_tree(&mut rng, num_nodes);

        // Benchmark HHDS tree traversal
        group.bench_function("hhds", |b| {
            b.iter(|| {
                let count = preorder_traversal_hhds(std::hint::black_box(&hhds_tree));
                std::hint::black_box(count);
            });
        });

        // Benchmark ego_tree traversal
        group.bench_function("ego_tree", |b| {
            b.iter(|| {
                let count = preorder_traversal_ego(std::hint::black_box(&ego_tree));
                std::hint::black_box(count);
            });
        });

        group.finish();
    }
}

criterion_group!(benches, bench_wide_tree_traversal_comparison);
criterion_main!(benches);