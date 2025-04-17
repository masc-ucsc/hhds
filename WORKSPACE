load("@bazel_tools//tools/build_defs/repo:git.bzl", "git_repository")
load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")


# Perfetto
http_archive(
    name = "com_google_perfetto",
    build_file = "perfetto.BUILD",
    sha256 = "c2230d04790eb50231a58616a3f1ff6dcf772d8e220333a7711605f99c5c6db9",
    strip_prefix = "perfetto-50.1/sdk",
    urls = ["https://github.com/google/perfetto/archive/refs/tags/v50.1.tar.gz"],
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

