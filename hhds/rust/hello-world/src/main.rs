use anyhow::Result;
use serde_json::{json, Value};

fn main() -> Result<()> {
    println!("Hello, world!");
    let j: Value = json!("{}");
    Ok(())
}
