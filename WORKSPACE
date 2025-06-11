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
    sha256 = "111e5665bdcb767677fa082f7e5e628584a9efd9c51ab7308800eb95cee062eb",
    strip_prefix = "iassert-b46c8a715096e52047676e93818a967a457647d4",
    urls = [
        "https://github.com/masc-ucsc/iassert/archive/b46c8a715096e52047676e93818a967a457647d4.zip",
    ],
)

# clang-tidy
http_archive(
    name = "bazel_clang_tidy",
    sha256 = "8c659583be062e26f939bda051f20570320aa0582a56e863c7a5b24f896bbce9",
    strip_prefix = "bazel_clang_tidy-9e9bfc5582ee8acc453a3539e69bfe90efff94f2",
    urls = [
        "https://github.com/erenon/bazel_clang_tidy/archive/9e9bfc5582ee8acc453a3539e69bfe90efff94f2.zip"
    ],
)

