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
    tag = "v1.8.4",
)

# abseil
http_archive(
  name = "com_google_absl",
  strip_prefix = "abseil-cpp-20240116.2",
  urls = ["https://github.com/abseil/abseil-cpp/archive/refs/tags/20240116.2.zip"],
  sha256 = "69909dd729932cbbabb9eeaff56179e8d124515f5d3ac906663d573d700b4c7d",
)

# Perfetto
http_archive(
    name = "com_google_perfetto",
    build_file = "perfetto.BUILD",
    sha256 = "dcb815fb54370fa20a657552288016cb66e7a98237c1a1d47e7645a4325ac75e",
    strip_prefix = "perfetto-45.0/sdk",
    urls = ["https://github.com/google/perfetto/archive/refs/tags/v45.0.tar.gz"],
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
    sha256 = "ea7170a98f6846a6e49c4c115a77de83601a733e00c55d14f0ab403f116aa838",
    strip_prefix = "rules_boost-83ce910e5a266a08bd5634e8ab480d6c2e32a571",
    urls = [
      "https://github.com/nelhage/rules_boost/archive/83ce910e5a266a08bd5634e8ab480d6c2e32a571.tar.gz",
    ]
)
load("@com_github_nelhage_rules_boost//:boost/boost.bzl", "boost_deps")
boost_deps()
