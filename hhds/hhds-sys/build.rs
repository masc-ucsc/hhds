extern crate bindgen;
use std::env;
use std::path::PathBuf;

fn main() {
    // println!("cargo:rustc-link-lib=bz2");
    println!("cargo:rustc-link-search=native=../../bazel-bin/external");
    println!("cargo:rustc-link-search=native=../../bazel-bin/hhds"); //TODO: Adjustments
    println!("cargo:rustc-link-lib=static=core"); // set to static but dylib if it's .so
    println!("cargo:rustc-link-search=native=../../bazel-bin/external/iassert+");
    println!("cargo:rustc-link-lib=static=iassert");
    // Link to the appropriate C++ standard library based on platform
    if cfg!(target_os = "macos") {
        println!("cargo:rustc-link-lib=c++");
    } else {
        println!("cargo:rustc-link-lib=stdc++");
    }

    // The bindgen::Builder is the main entry point
    // to bindgen, and lets you build up options for
    // the resulting bindings.
    let bindings = bindgen::Builder::default()
        // The input header we would like to generate
        // bindings for.
        .header("ffi_tree.hpp")
        // functions from the wrapper we allow bindgen to process
        .allowlist_function("tree_int_.*")
        .allowlist_function("get_.*")
        .allowlist_function("add_.*")
        .allowlist_function("set_.*")
        .allowlist_function("append_.*")
        .allowlist_function("insert_.*")
        .allowlist_function("delete.*")
        .allowlist_type("hhds_Tree.*")
        // Tell cargo to invalidate the built crate whenever any of the
        // included header files changed.
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
