extern crate bindgen;
use std::env;
use std::path::PathBuf;

fn main() {
    // Tell cargo to look for shared libraries in the specified directory
    println!("cargo:rustc-link-search=../../bazel-bin/hhds");
    // Tell cargo to tell rustc to link the system bzip2
    // shared library.
    println!("cargo:rustc-link-lib=static=core");
    println!("cargo:rustc-link-lib=bz2");
    println!("cargo:rustc-link-lib=dylib=stdc++");

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
        .allowlist_function("forest.*")
        .allowlist_function("tree.*")
        //.layout_tests(false)
        //.blocklist_type("_RehashPolicy")
        //.blocklist_type("fmt_arg_join.*")
        //.blocklist
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
