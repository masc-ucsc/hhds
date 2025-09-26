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

// Build HHDS tree with mixed operations - more realistic tree construction
fn build_mixed_hhds_tree(rng: &mut StdRng, depth_val: u32) -> HhdsTree {
    let tree = HhdsTree::new_no_ref();
    let root = tree.add_root(0);

    let mut current_level = vec![root];
    let mut id = 1;

    for _depth in 0..depth_val {
        let mut next_level = Vec::new();

        for node in current_level {
            let num_children = rng.random_range(3..=6); // 4-10 children per node

            // Add first child normally
            if num_children > 0 {
                let data = id;
                id += 1;
                let first_child = tree.add_child(node, data);
                next_level.push(first_child);

                // For remaining children, mix add_child and insert_next_sibling (via append_sibling)
                let mut current_sibling = first_child;
                for _i in 1..num_children {
                    let operation_choice = rng.random_range(1..=100);
                    let data = id;
                    id += 1;

                    if operation_choice <= 70 { // 70% chance: add as next child to parent
                        let new_child = tree.add_child(node, data);
                        next_level.push(new_child);
                        current_sibling = new_child;
                    } else { // 30% chance: append as sibling
                        let new_sibling = tree.append_sibling(current_sibling, data);
                        next_level.push(new_sibling);
                        current_sibling = new_sibling;
                    }
                }
            }
        }

        current_level = next_level;
    }

    tree
}

// Build ego_tree with mixed operations
fn build_mixed_ego_tree(rng: &mut StdRng, depth_val: u32) -> EgoTree<i32> {
    let mut tree = EgoTree::new(0);
    let root_id = tree.root().id();

    let mut current_level = vec![root_id];
    let mut id = 1;

    for _depth in 0..depth_val {
        let mut next_level = Vec::new();

        for node_id in current_level {
            let num_children = rng.random_range(3..=6);

            // Add first child normally
            if num_children > 0 {
                let data = id;
                id += 1;
                let first_child_id = tree.get_mut(node_id).unwrap().append(data).id();
                next_level.push(first_child_id);

                // For remaining children, mix append and insert operations
                let mut current_sibling_id = first_child_id;
                for _i in 1..num_children {
                    let operation_choice = rng.random_range(1..=100);
                    let data = id;
                    id += 1;

                    if operation_choice <= 70 { // 70% chance: add as child to parent
                        let new_child_id = tree.get_mut(node_id).unwrap().append(data).id();
                        next_level.push(new_child_id);
                        current_sibling_id = new_child_id;
                    } else { // 30% chance: insert after current sibling
                        let new_sibling_id = tree.get_mut(current_sibling_id).unwrap().insert_after(data).id();
                        next_level.push(new_sibling_id);
                        current_sibling_id = new_sibling_id;
                    }
                }
            }
        }

        current_level = next_level;
    }

    tree
}

fn bench_mixed_tree_building_comparison(c: &mut Criterion) {
    let depths = vec![3, 4];

    for depth in depths {
        let mut group = c.benchmark_group(format!("mixed_tree_building_depth_{}", depth));
        group.sample_size(30);

        // Benchmark HHDS tree building with mixed operations
        group.bench_function("hhds", |b| {
            b.iter(|| {
                let mut rng = create_rng();
                let tree = build_mixed_hhds_tree(&mut rng, std::hint::black_box(depth));
                std::hint::black_box(tree);
            });
        });

        // Benchmark ego_tree building with mixed operations
        group.bench_function("ego_tree", |b| {
            b.iter(|| {
                let mut rng = create_rng();
                let tree = build_mixed_ego_tree(&mut rng, std::hint::black_box(depth));
                std::hint::black_box(tree);
            });
        });

        group.finish();
    }
}

criterion_group!(benches, bench_mixed_tree_building_comparison);
criterion_main!(benches);