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
    tag = "v1.9.1",
)

# abseil
http_archive(
  name = "com_google_absl",
  strip_prefix = "abseil-cpp-20250127.0",
  urls = ["https://github.com/abseil/abseil-cpp/archive/refs/tags/20250127.0.zip"],
  sha256 = "f470da4226a532560e9ee69868da3598d9d37dc00408a56db83e2fd19e3e2ae6",
)

# Perfetto
http_archive(
    name = "com_google_perfetto",
    build_file = "perfetto.BUILD",
    sha256 = "0871a92a162ac5655b7d724f9359b15a75c4e92472716edbc4227a64a4680be4",
    strip_prefix = "perfetto-49.0/sdk",
    urls = ["https://github.com/google/perfetto/archive/refs/tags/v49.0.tar.gz"],
)

# fmt
http_archive(
    name = "fmt",
    build_file = "@//tools:fmt.BUILD",
    sha256 = "b25334cefaa19eca8441586303639cd014c38c8992f3fead5f0621152a5ce76e",
    strip_prefix = "fmt-11.1.3",
    urls = [
        "https://github.com/fmtlib/fmt/archive/refs/tags/11.1.3.zip",
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
    sha256 = "8eb86907abcff8e23b167356222e398a4d3ab7e3277b7dacd29e7d710d8c8ca5",
    strip_prefix = "bazel_clang_tidy-db677011c7363509a288a9fb3bf0a50830bbf791",
    urls = [
        "https://github.com/erenon/bazel_clang_tidy/archive/db677011c7363509a288a9fb3bf0a50830bbf791.zip"
    ],
)

# Boost
http_archive(
    name = "com_github_nelhage_rules_boost",
    sha256 = "48f9cc42df396f944e6be53a6d23fb7f4feefc521aa47cad0730733cb6582dd5",
    strip_prefix = "rules_boost-c3edeeb93c47ee87d01fc96aec8e48ca9449d10c",
    urls = [
      "https://github.com/nelhage/rules_boost/archive/c3edeeb93c47ee87d01fc96aec8e48ca9449d10c.tar.gz",
    ]
)
load("@com_github_nelhage_rules_boost//:boost/boost.bzl", "boost_deps")
boost_deps()
