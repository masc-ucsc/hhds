# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")
load("//tools:copt_default.bzl", "COPTS")

config_setting(
    name = "use_profiling",
    values = {"define": "profiling=1"},
)

cc_library(
    name = "rigtorp",
    hdrs = glob(["tests/*.hpp"]),
    copts = COPTS,
    includes = ["."],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "hhds",
    srcs = glob(
        ["*.cpp"],
        exclude = ["*test*.cpp"],
    ),
    hdrs = glob(["*.hpp"]),
    copts = COPTS,
    defines = select({
        ":use_profiling": [
            "HHDS_PROFILING=1",
        ],
        "//conditions:default": [],
    }),
    includes = ["."],
    visibility = ["//visibility:public"],
    deps = [
        "@fmt",
        "@iassert",
        "@emhash",
    ] + select({
        ":use_profiling": [
            "@com_google_perfetto//:perfetto",
        ],
        "//conditions:default": [],
    }),
)

cc_test(
    name = "graph_bench",
    srcs = [
        "tests/graph_bench.cpp",
    ],
    deps = [
        ":hhds",
        "@com_google_benchmark//:benchmark",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/container:flat_hash_set",
    ],
)

cc_test(
    name = "small_set_bench",
    srcs = [
        "tests/small_set_bench.cpp",
    ],
    deps = [
        ":rigtorp",
        "@fmt",
        "@iassert",
        "@emhash",
        "@com_google_benchmark//:benchmark",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/container:flat_hash_set",
    ],
)

