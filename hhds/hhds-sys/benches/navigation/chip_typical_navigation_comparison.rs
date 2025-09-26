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

// Build chip-typical tree with HHDS
fn build_hhds_tree(rng: &mut StdRng, depth_val: u32) -> HhdsTree {
    let tree = HhdsTree::new_no_ref();
    let root = tree.add_root(0);

    let mut current_level = vec![root];
    let mut id = 1;

    for _depth in 0..depth_val {
        let mut next_level = Vec::new();

        for node in current_level {
            let num_children = rng.random_range(3..=8);
            for _i in 0..num_children {
                let data = id;
                id += 1;
                let added = tree.add_child(node, data);
                next_level.push(added);
            }
        }

        current_level = next_level;
    }

    tree
}

// Build chip-typical tree with ego_tree
fn build_ego_tree(rng: &mut StdRng, depth_val: u32) -> EgoTree<i32> {
    let mut tree = EgoTree::new(0);
    let root_id = tree.root().id();

    let mut current_level = vec![root_id];
    let mut id = 1;

    for _depth in 0..depth_val {
        let mut next_level = Vec::new();

        for node_id in current_level {
            let num_children = rng.random_range(3..=8);
            for _i in 0..num_children {
                let data = id;
                id += 1;
                let new_node_id = tree.get_mut(node_id).unwrap().append(data).id();
                next_level.push(new_node_id);
            }
        }

        current_level = next_level;
    }

    tree
}

// Test navigation operations on HHDS tree
fn test_hhds_navigation(tree: &HhdsTree) -> u64 {
    let mut operation_count = 0;

    // Traverse the tree and perform navigation operations
    for node in tree.pre_ord_iter(false) {
        let node_pos = node.deref() as i64;
        if node_pos <= 0 { break; }

        // Test parent navigation
        let parent = tree.get_parent(node_pos);
        if parent > 0 { operation_count += 1; }

        // Test child navigation
        let first_child = tree.get_first_child(node_pos);
        if first_child > 0 { operation_count += 1; }

        let last_child = tree.get_last_child(node_pos);
        if last_child > 0 { operation_count += 1; }

        // Test sibling navigation
        let next_sibling = tree.get_sibling_next(node_pos);
        if next_sibling > 0 { operation_count += 1; }

        let prev_sibling = tree.get_sibling_prev(node_pos);
        if prev_sibling > 0 { operation_count += 1; }
    }

    operation_count
}

// Test navigation operations on ego_tree
fn test_ego_navigation(tree: &EgoTree<i32>) -> u64 {
    let mut operation_count = 0;

    // Traverse the tree and perform navigation operations
    for edge in tree.root().traverse() {
        let node_ref = match edge {
            ego_tree::iter::Edge::Open(node) => node,
            ego_tree::iter::Edge::Close(_) => continue,
        };

        // Test parent navigation
        if node_ref.parent().is_some() { operation_count += 1; }

        // Test child navigation
        if node_ref.first_child().is_some() { operation_count += 1; }
        if node_ref.last_child().is_some() { operation_count += 1; }

        // Test sibling navigation
        if node_ref.next_sibling().is_some() { operation_count += 1; }
        if node_ref.prev_sibling().is_some() { operation_count += 1; }
    }

    operation_count
}

fn bench_chip_typical_navigation_comparison(c: &mut Criterion) {
    let depths = vec![2, 3];

    for depth in depths {
        let mut group = c.benchmark_group(format!("chip_typical_navigation_depth_{}", depth));
        group.sample_size(30);

        // Build trees once for navigation benchmarks
        let mut rng = create_rng();
        let hhds_tree = build_hhds_tree(&mut rng, depth);
        let ego_tree = build_ego_tree(&mut rng, depth);

        // Benchmark HHDS tree navigation
        group.bench_function("hhds", |b| {
            b.iter(|| {
                let count = test_hhds_navigation(std::hint::black_box(&hhds_tree));
                std::hint::black_box(count);
            });
        });

        // Benchmark ego_tree navigation
        group.bench_function("ego_tree", |b| {
            b.iter(|| {
                let count = test_ego_navigation(std::hint::black_box(&ego_tree));
                std::hint::black_box(count);
            });
        });

        group.finish();
    }
}

criterion_group!(benches, bench_chip_typical_navigation_comparison);
criterion_main!(benches);
