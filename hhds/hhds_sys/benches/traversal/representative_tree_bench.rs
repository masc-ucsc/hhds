use anyhow::Result;
use criterion::{black_box, criterion_group, criterion_main, BatchSize, Criterion};
use hhds_sys::tree::Tree;
use rand::rngs::StdRng;
use rand::{Rng, SeedableRng};
use std::env;
use std::fs;
use std::time::{SystemTime, UNIX_EPOCH};
use tree_sitter::Parser;

fn create_rng() -> StdRng {
    let now = SystemTime::now().duration_since(UNIX_EPOCH).unwrap();
    let micros = now.as_secs() * 1_000_000 + u64::from(now.subsec_micros());
    StdRng::seed_from_u64(micros)
}

fn generate_random_int(rng: &mut StdRng, min: i32, max: i32) -> i32 {
    rng.random_range(min..max)
}

fn pre_order_traversal(tree: &Tree) {
    let mut cnt = 0;
    for _node in tree.pre_ord_iter(true) {
        cnt += 1;
    }
    black_box(cnt);
}

fn build_tree(rng: &mut StdRng, tree: &Tree, num_nodes: u32) -> Result<()> {
    let mut parser = Parser::new();
    parser
        .set_language(&tree_sitter_cpp::LANGUAGE.into())
        .expect("Error loading CPP grammer");

    let source_code = fs::read_to_string("../SelectionDAG.cpp")?;
    let parser_tree = parser.parse(source_code, None).unwrap();
    let root_node = parser_tree.root_node();

    assert_eq!(root_node.kind(), "translation_unit");
    assert_eq!(root_node.start_position().column, 0);

    let current = tree.add_root(1);
    let mut node_count = 1;
    let mut stack: Vec<i64> = vec![current];
    let mut node_stack = vec![root_node];
    loop {
        if let Some(node) = node_stack.pop() {
            let current = stack.pop().unwrap();
            for i in (0..node.child_count()).rev() {
                if let Some(child) = node.child(i) {
                    node_stack.push(child);
                    stack.push(tree.add_child(current, generate_random_int(rng, 0, 10000)));
                    node_count += 1;
                    if node_count == num_nodes {
                        return Ok(());
                    }
                }
            }
        } else {
            println!("We reached the max number of nodes {}", node_count);
            break;
        }
    }
    Ok(())
}

fn bench_tree_traversal(c: &mut Criterion, num_nodes: u32) {
    let mut group = c.benchmark_group("TreeTraversal");
    group.sample_size(10);
    group.bench_function(format!("preorder_{}_nodes", num_nodes), |b| {
        let mut rng = create_rng();
        b.iter_batched(
            || {
                let tree = Tree::new_no_ref();
                let _ = build_tree(&mut rng, &tree, num_nodes);
                tree
            },
            |tree| {
                pre_order_traversal(black_box(&tree));
            },
            BatchSize::SmallInput,
        );
    });
    group.finish()
}

fn test_mock_tree(c: &mut Criterion) {
    let nodes = env::var("NODES")
        .ok()
        .and_then(|v| v.parse().ok())
        .unwrap_or(10);
    bench_tree_traversal(c, nodes);
}

criterion_group!(benches, test_mock_tree);
criterion_main!(benches);
