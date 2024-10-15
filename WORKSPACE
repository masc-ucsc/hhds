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
    tag = "v1.9.0",
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
    sha256 = "8d1c6bf44f1bdb098ab70cd60da3ce6b6e731e4eb21dd52b2527cbdcf85d984d",
    strip_prefix = "perfetto-48.1/sdk",
    urls = ["https://github.com/google/perfetto/archive/refs/tags/v48.1.tar.gz"],
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
    sha256 = "fa5ecddd8e728d48175b6a61d0d51408d0a0d24fd1dde9eb676627960ef44317",
    strip_prefix = "bazel_clang_tidy-e85311053ec3c32ff418b433af4469b9c77e6b16",
    urls = [
        "https://github.com/erenon/bazel_clang_tidy/archive/e85311053ec3c32ff418b433af4469b9c77e6b16.zip"
    ],
)

# Boost
http_archive(
    name = "com_github_nelhage_rules_boost",
    sha256 = "a194dfc1f85366718ac4e4f68a49ce67bce15995c7597653bd69120336898031",
    strip_prefix = "rules_boost-5cdb3c83807d1036bebcc60acf87333e5b3cc856",
    urls = [
      "https://github.com/nelhage/rules_boost/archive/5cdb3c83807d1036bebcc60acf87333e5b3cc856.tar.gz",
    ]
)
load("@com_github_nelhage_rules_boost//:boost/boost.bzl", "boost_deps")
boost_deps()
