load("@bazel_tools//tools/build_defs/repo:git.bzl", "git_repository")
load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

git_repository(
    name = "com_google_googletest",
    remote = "https://github.com/google/googletest",
    tag = "v1.15.2",
)

git_repository(
    name = "com_google_benchmark",
    remote = "https://github.com/google/benchmark.git",
    tag = "v1.8.5",
)

# abseil
http_archive(
  name = "com_google_absl",
  strip_prefix = "abseil-cpp-20240722.0",
  urls = ["https://github.com/abseil/abseil-cpp/archive/refs/tags/20240722.0.zip"],
  sha256 = "95e90be7c3643e658670e0dd3c1b27092349c34b632c6e795686355f67eca89f",
)

# Perfetto
http_archive(
    name = "com_google_perfetto",
    build_file = "perfetto.BUILD",
    sha256 = "9bbd38a0f074038bde6ccbcf5f2ff32587eb60faec254932268ecb6f17f18186",
    strip_prefix = "perfetto-47.0/sdk",
    urls = ["https://github.com/google/perfetto/archive/refs/tags/v47.0.tar.gz"],
)

# fmt
http_archive(
    name = "fmt",
    build_file = "@//tools:fmt.BUILD",
    sha256 = "7aa4b58e361de10b8e5d7b6c18aebd98be1886ab3efe43e368527a75cd504ae4",
    strip_prefix = "fmt-11.0.2",
    urls = [
        "https://github.com/fmtlib/fmt/archive/refs/tags/11.0.2.zip",
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
    sha256 = "3950bc0c09a44869669bb2b5e83bdea42d8038231263c567fd80314ae3dfe117",
    strip_prefix = "rules_boost-1d5cd42547905c0fea655275babfba8c0b07b753",
    urls = [
      "https://github.com/nelhage/rules_boost/archive/1d5cd42547905c0fea655275babfba8c0b07b753.tar.gz",
    ]
)
load("@com_github_nelhage_rules_boost//:boost/boost.bzl", "boost_deps")
boost_deps()
