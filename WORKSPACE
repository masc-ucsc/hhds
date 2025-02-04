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
  strip_prefix = "abseil-cpp-20250127.0",
  urls = ["https://github.com/abseil/abseil-cpp/archive/refs/tags/20250127.0.zip"],
  sha256 = "f470da4226a532560e9ee69868da3598d9d37dc00408a56db83e2fd19e3e2ae6",
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
    sha256 = "e1440a34d7abb720005d7ba1db4b50fbe93850fbb88c0e28f8865f35dd936245",
    strip_prefix = "bazel_clang_tidy-f23d924918c581c68cd5cda5f12b4f8198ac0c35",
    urls = [
        "https://github.com/erenon/bazel_clang_tidy/archive/f23d924918c581c68cd5cda5f12b4f8198ac0c35.zip"
    ],
)

# Boost
http_archive(
    name = "com_github_nelhage_rules_boost",
    sha256 = "085aadb6cd1323740810efcde29e99838286f44e0ab060af6e2a645380316cd6",
    strip_prefix = "rules_boost-504e4dbc8c480fac5da33035490bc2ccc59db749",
    urls = [
      "https://github.com/nelhage/rules_boost/archive/504e4dbc8c480fac5da33035490bc2ccc59db749.tar.gz",
    ]
)
load("@com_github_nelhage_rules_boost//:boost/boost.bzl", "boost_deps")
boost_deps()
