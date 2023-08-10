load("@bazel_tools//tools/build_defs/repo:git.bzl", "git_repository")
load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

git_repository(
    name = "com_google_googletest",
    remote = "https://github.com/google/googletest",
    tag = "release-1.11.0",
)

git_repository(
    name = "com_google_benchmark",
    remote = "https://github.com/google/benchmark.git",
    tag = "v1.7.1",
)

# abseil
http_archive(
  name = "com_google_absl",
  strip_prefix = "abseil-cpp-20211102.0",
  urls = ["https://github.com/abseil/abseil-cpp/archive/refs/tags/20211102.0.zip"],
  sha256 = "a4567ff02faca671b95e31d315bab18b42b6c6f1a60e91c6ea84e5a2142112c2",
)

# Perfetto
http_archive(
    name = "com_google_perfetto",
    build_file = "perfetto.BUILD",
    sha256 = "39d7b3635834398828cfd189bd61afb0657ca2a3a08efbfd9866bfbcd440810b",
    strip_prefix = "perfetto-37.0/sdk",
    urls = ["https://github.com/google/perfetto/archive/refs/tags/v37.0.tar.gz"],
)

# fmt
http_archive(
    name = "fmt",
    build_file = "@//tools:fmt.BUILD",
    sha256 = "cdc885473510ae0ea909b5589367f8da784df8b00325c55c7cbbab3058424120",
    strip_prefix = "fmt-9.1.0",
    urls = [
        "https://github.com/fmtlib/fmt/archive/refs/tags/9.1.0.zip",
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
    sha256 = "59b8faf1a2e0ec4a5520130739c1c20b80e331b91026461136babfbe4f7d3f9b",
    strip_prefix = "bazel_clang_tidy-674fac7640ae0469b7a58017018cb1497c26e2bf",
    urls = [
        "https://github.com/erenon/bazel_clang_tidy/archive/674fac7640ae0469b7a58017018cb1497c26e2bf.zip"
    ],
)
