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

fn build_hhds_tree(rng: &mut StdRng, tree: &HhdsTree, num_nodes: u32) {
    let root = tree.add_root(generate_random_int(rng, 1, 100));
    for _ in 0..num_nodes {
        tree.add_child(root, generate_random_int(rng, 1, 100));
    }
}

fn build_ego_tree(rng: &mut StdRng, num_nodes: u32) -> EgoTree<i32> {
    let mut tree = EgoTree::new(generate_random_int(rng, 1, 100));
    let root_id = tree.root().id();

    for _ in 0..num_nodes {
        tree.get_mut(root_id).unwrap().append(generate_random_int(rng, 1, 100));
    }

    tree
}

fn bench_wide_tree_comparison(c: &mut Criterion) {
    let node_counts = vec![10, 100, 1000, 10000, 100000, 1000000];

    for num_nodes in node_counts {
        let mut group = c.benchmark_group(format!("wide_tree_{}_nodes", num_nodes));
        group.sample_size(if num_nodes >= 100000 { 10 } else { 100 });

        // Benchmark HHDS tree creation
        group.bench_function("hhds", |b| {
            b.iter(|| {
                let mut rng = create_rng();
                let tree = HhdsTree::new_no_ref();
                build_hhds_tree(&mut rng, std::hint::black_box(&tree), std::hint::black_box(num_nodes));
            });
        });

        // Benchmark ego_tree creation
        group.bench_function("ego_tree", |b| {
            b.iter(|| {
                let mut rng = create_rng();
                let _tree = build_ego_tree(&mut rng, std::hint::black_box(num_nodes));
            });
        });

        group.finish();
    }
}

criterion_group!(benches, bench_wide_tree_comparison);
criterion_main!(benches);