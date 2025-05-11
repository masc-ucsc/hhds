extern crate bindgen;
use std::env;
use std::path::PathBuf;

fn main() {
    //let iassert_include = PathBuf::from("../../../bazel-bin/external/_main~_repo_rules~iassert");

    // Tell cargo to look for shared libraries in the specified directory
    // Tell cargo to tell rustc to link the system bzip2
    // shared library.
    //println!("cargo:rustc-link-lib=static=core");
    println!("cargo:rustc-link-lib=bz2");
    println!("cargo:rustc-link-search=native=../../bazel-bin/external");
    println!("cargo:rustc-link-search=native=../../bazel-bin/hhds"); // adjust to real path
    println!("cargo:rustc-link-lib=static=core"); // or dylib if it's .so
    println!("cargo:rustc-link-search=native=../../bazel-bin/hhds/iassert");
    println!("cargo:rustc-link-lib=static=iassert");
    println!("cargo:rustc-link-lib=dylib=stdc++");
    println!("cargo:rustc-link-lib=c++");

    // The bindgen::Builder is the main entry point
    // to bindgen, and lets you build up options for
    // the resulting bindings.
    let bindings = bindgen::Builder::default()
        // The input header we would like to generate
        // bindings for.
        .header("../wrapper.hpp")
        //.header("../tree.hpp")
        .clang_arg("-std=c++17")
        .clang_arg("-I../iassert/src")
        .clang_arg("-isystem/usr/include/c++/11")
        .clang_arg("-isystem/usr/include/x86_64-linux-gnu/c++/11")
        .clang_arg("-I/home/cheeky/.cache/bazel/_bazel_cheeky/18f301c280c22aed902c61d1cca66028/external/_main~_repo_rules~iassert/src")
        .allowlist_function("forest.*")
        .allowlist_function("tree.*")
        .allowlist_function("print_tree.*")
        .allowlist_function("get_.*") // getters
        .allowlist_function("add_.*") // setters - add
        .allowlist_function("delete.*")
        .allowlist_function(".*pre_order_iterator.*")
        .allowlist_function(".*post_order_iterator.*")
        // Tell cargo to invalidate the built crate whenever any of the
        // included header files changed.
        //.allowlist_type("ForestIntHandle")
        .parse_callbacks(Box::new(bindgen::CargoCallbacks::new()))
        // Finish the builder and generate the bindings.
        .generate()
        // Unwrap the Result and panic on failure.
        .expect("Unable to generate bindings");

    // Write the bindings to the $OUT_DIR/bindings.rs file.
    let out_path = PathBuf::from(env::var("OUT_DIR").unwrap());
    bindings
        .write_to_file(out_path.join("bindings.rs"))
        .expect("Couldn't write bindings!");
}
