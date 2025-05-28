use anyhow::Result;
use criterion::{criterion_group, criterion_main, Criterion};
use hhds_sys::tree::Tree;
use rand::rngs::StdRng;
use rand::{Rng, SeedableRng};
use std::env;
use std::fs;
use std::time::{SystemTime, UNIX_EPOCH};
use tree_sitter::{Node, Parser, Tree as TsTree};

fn create_rng() -> StdRng {
    let now = SystemTime::now().duration_since(UNIX_EPOCH).unwrap();
    let micros = now.as_secs() * 1_000_000 + u64::from(now.subsec_micros());
    StdRng::seed_from_u64(micros)
}

fn generate_random_int(rng: &mut StdRng, min: i32, max: i32) -> i32 {
    rng.random_range(min..max)
}

fn build_hhds_tree(
    rng: &mut StdRng,
    parser_tree: &TsTree,
    tree: &mut Tree,
    num_nodes: u32,
) -> Result<()> {
    let root_node: Node<'_> = parser_tree.root_node();
    let mut node_count = 1;
    let current = tree.add_root(1);
    let mut stack: Vec<i64> = vec![current];
    let mut node_stack = vec![root_node];
    loop {
        if let Some(node) = node_stack.pop() {
            let current = stack.pop().unwrap();
            for i in (0..node.child_count()).rev() {
                if let Some(child) = node.child(i) {
                    node_stack.push(child);
                    let child_id = tree.add_child(current, generate_random_int(rng, 0, 10000));
                    stack.push(child_id);
                    node_count += 1;
                    if node_count == num_nodes {
                        return Ok(());
                    }
                }
            }
        } else {
            //println!("We reached the max number of nodes {}", node_count);
            break;
        }
    }
    Ok(())
}

fn parser_tree() -> Result<TsTree> {
    let mut parser = Parser::new();
    parser
        .set_language(&tree_sitter_cpp::LANGUAGE.into())
        .expect("Error loading CPP grammer");

    let source_code = fs::read_to_string("../SelectionDAG.cpp")?;
    let parser_tree = parser.parse(source_code, None).unwrap();
    Ok(parser_tree)
}

fn bench_tree_traversal(c: &mut Criterion, num_nodes: u32) {
    let mut group = c.benchmark_group("TreeCreationRepresentative");
    group.sample_size(10);
    group.bench_function(format!("tree_{}_nodes", num_nodes), |b| {
        let mut rng = create_rng();
        b.iter_batched(
            || {
                let tree = Tree::new_no_ref();
                let ts_tree = parser_tree().unwrap();
                (tree, ts_tree)
            },
            |(mut tree, parser)| {
                let _ = build_hhds_tree(&mut rng, &parser, &mut tree, num_nodes);
            },
            criterion::BatchSize::SmallInput,
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
