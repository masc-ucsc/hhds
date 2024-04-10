load("@bazel_tools//tools/build_defs/repo:git.bzl", "git_repository")
load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

git_repository(
    name = "com_google_googletest",
    remote = "https://github.com/google/googletest",
    tag = "v1.14.0",
)

git_repository(
    name = "com_google_benchmark",
    remote = "https://github.com/google/benchmark.git",
    tag = "v1.8.3",
)

# abseil
http_archive(
  name = "com_google_absl",
  strip_prefix = "abseil-cpp-20240116.1",
  urls = ["https://github.com/abseil/abseil-cpp/archive/refs/tags/20240116.1.zip"],
  sha256 = "edc6a93163af5b2a186d468717f6fe23653a5cb31a1e6932f0aba05af7d762e9",
)

# Perfetto
http_archive(
    name = "com_google_perfetto",
    build_file = "perfetto.BUILD",
    sha256 = "db4162ee6495b1fcc13ba7aca77d67f9fd1766d184743137a04af8b1e3906b9d",
    strip_prefix = "perfetto-44.0/sdk",
    urls = ["https://github.com/google/perfetto/archive/refs/tags/v44.0.tar.gz"],
)

# fmt
http_archive(
    name = "fmt",
    build_file = "@//tools:fmt.BUILD",
    sha256 = "d368f9c39a33a3aef800f5be372ec1df1c12ad57ada1f60adc62f24c0e348469",
    strip_prefix = "fmt-10.2.1",
    urls = [
        "https://github.com/fmtlib/fmt/archive/refs/tags/10.2.1.zip",
    ],
)

# emhash
http_archive(
    name = "emhash",
    build_file = "@//tools:emhash.BUILD",
    sha256 = "9ec42a978c34653bc6f0c02b5c2e874d34265f8543cede787c79eb71f6e22f0f",
    strip_prefix = "emhash-60127291d95b5e345ed26c719554130f3dab3bbd",
    urls = [
        "https://github.com/renau/emhash/archive/60127291d95b5e345ed26c719554130f3dab3bbd.zip",
    ],
)

# iassert
http_archive(
    name = "iassert",
    sha256 = "c6bf66a76d5a1de57c45dba137c9b51ab3b4f3a31e5de9e3c3496d7d36a128f8",
    strip_prefix = "iassert-5c18eb082262532f621a23023f092f4119a44968",
    urls = [
        "https://github.com/masc-ucsc/iassert/archive/5c18eb082262532f621a23023f092f4119a44968.zip",
    ],
)

# clang-tidy
http_archive(
    name = "bazel_clang_tidy",
    sha256 = "f0e2871f32652b2dbc54739b55b4c2ece88eb8189133f20e7c4327eb893994b2",
    strip_prefix = "bazel_clang_tidy-bff5c59c843221b05ef0e37cef089ecc9d24e7da",
    urls = [
        "https://github.com/erenon/bazel_clang_tidy/archive/bff5c59c843221b05ef0e37cef089ecc9d24e7da.zip"
    ],
)

# Boost
http_archive(
    name = "com_github_nelhage_rules_boost",
    sha256 = "8d7525bb046049614d2ca28d3be6b3f44e144abf90d0d67a25370d56e6918b28",
    strip_prefix = "rules_boost-e1854fb177ae91dc82ce9534737f5238d2cee9d0",
    urls = [
      "https://github.com/nelhage/rules_boost/archive/e1854fb177ae91dc82ce9534737f5238d2cee9d0.tar.gz",
    ]
)
load("@com_github_nelhage_rules_boost//:boost/boost.bzl", "boost_deps")
boost_deps()
