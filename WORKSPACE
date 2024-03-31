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
    tag = "v1.8.2",
)

# abseil
http_archive(
  name = "com_google_absl",
  strip_prefix = "abseil-cpp-20230802.0",
  urls = ["https://github.com/abseil/abseil-cpp/archive/refs/tags/20230802.0.zip"],
  sha256 = "2942db09db29359e0c1982986167167d226e23caac50eea1f07b2eb2181169cf",
)

# Perfetto
http_archive(
    name = "com_google_perfetto",
    build_file = "perfetto.BUILD",
    sha256 = "615d336a5c5c6fc0bb0aad1ef8d4c4575a97be2989973d617fa552bfd4886980",
    strip_prefix = "perfetto-43.2/sdk",
    urls = ["https://github.com/google/perfetto/archive/refs/tags/v43.2.tar.gz"],
)

# fmt
http_archive(
    name = "fmt",
    build_file = "@//tools:fmt.BUILD",
    sha256 = "c1fd1b6e1bc695a47454b8402f08f269489715aaed6dd49744a7ed5a6b9e7487",
    strip_prefix = "fmt-10.1.0",
    urls = [
        "https://github.com/fmtlib/fmt/archive/refs/tags/10.1.0.zip",
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
    sha256 = "e08f52bb2960cca0c81d4bd65cb94b9671b80847f41e57b5f1a7836931063ab2",
    strip_prefix = "bazel_clang_tidy-133d89a6069ce253a92d32a93fdb7db9ef100e9d",
    urls = [
        "https://github.com/erenon/bazel_clang_tidy/archive/133d89a6069ce253a92d32a93fdb7db9ef100e9d.zip"
    ],
)

# Boost
http_archive(
    name = "com_github_nelhage_rules_boost",
    sha256 = "5ea00abc70cdf396a23fb53201db19ebce2837d28887a08544429d27783309ed",
    strip_prefix = "rules_boost-96e9b631f104b43a53c21c87b01ac538ad6f3b48",
    urls = [
      "https://github.com/nelhage/rules_boost/archive/96e9b631f104b43a53c21c87b01ac538ad6f3b48.tar.gz",
    ]
)
load("@com_github_nelhage_rules_boost//:boost/boost.bzl", "boost_deps")
boost_deps()
