#![allow(non_upper_case_globals)]
#![allow(non_camel_case_types)]
#![allow(non_snake_case)]

include!(concat!(env!("OUT_DIR"), "/bindings.rs"));

pub fn add(left: u64, right: u64) -> u64 {
    left + right
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn it_works() {
        let result = add(2, 2);
        assert_eq!(result, 4);
    }

    #[test]
    fn basic_forest_test() {
        unsafe {
        let forest_handler = forest_int_new();
        //let forest_handler = forest_int_new();
        //let idx = forest_int_create_tree(forest_handler, 10);
        //let val = forest_int_get_tree(forest_handler, idx);
        }
    }
}
