<<<<<<< HEAD
fn main() {
    println!("Hello, world!");
=======
use anyhow::Result;
use serde_json::{json, Value};

fn main() -> Result<()> {
    println!("Hello, world!");
    let j: Value = json!("{}");
    Ok(())
>>>>>>> 49c0365bc7a75feb3f017d1296600bed77394d36
}
