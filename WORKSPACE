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
    sha256 = "c6bf66a76d5a1de57c45dba137c9b51ab3b4f3a31e5de9e3c3496d7d36a128f8",
    strip_prefix = "iassert-5c18eb082262532f621a23023f092f4119a44968",
    urls = [
        "https://github.com/masc-ucsc/iassert/archive/5c18eb082262532f621a23023f092f4119a44968.zip",
    ],
)

# clang-tidy
http_archive(
    name = "bazel_clang_tidy",
    sha256 = "41e3930fa97b2b6572121c8221ade280edc0694e3e21e06a19d761ad4d09aca3",
    strip_prefix = "bazel_clang_tidy-a965fb4cb5286812643e205a5f0db8927107b120",
    urls = [
        "https://github.com/erenon/bazel_clang_tidy/archive/a965fb4cb5286812643e205a5f0db8927107b120.zip"
    ],
)

