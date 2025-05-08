load("@bazel_tools//tools/build_defs/repo:git.bzl", "git_repository")
load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")


# Perfetto
http_archive(
    name = "com_google_perfetto",
    build_file = "perfetto.BUILD",
    sha256 = "0871a92a162ac5655b7d724f9359b15a75c4e92472716edbc4227a64a4680be4",
    strip_prefix = "perfetto-49.0/sdk",
    urls = ["https://github.com/google/perfetto/archive/refs/tags/v49.0.tar.gz"],
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
    sha256 = "9479439270fc42c58a3959d7aa5ecd207ebf0bbe9210e1d17b426530f0b096b2",
    strip_prefix = "iassert-c2136ed8809ec1addbc48eb836c58d5b895e3f2b",
    urls = [
        "https://github.com/masc-ucsc/iassert/archive/c2136ed8809ec1addbc48eb836c58d5b895e3f2b.zip",
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

