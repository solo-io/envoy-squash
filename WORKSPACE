
PROXY_SHA = "7813a9ad548f63ce010c5ba90370989932b9acc9"  # Sep 26, 2017 (use github to download tclap instead of sourceforge)

# http_archive(
#     name = "proxy",
#     strip_prefix = "proxy-" + PROXY_SHA,
#     url = "https://github.com/istio/proxy/archive/" + PROXY_SHA + ".zip",
# )

local_repository(
    name = "proxy",
    path = "/home/yuval/Projects/solo/proxy/",
)

load(
    "@proxy//src/envoy/mixer:repositories.bzl",
    "mixer_client_repositories",
)

mixer_client_repositories()

load(
    "@mixerclient_git//:repositories.bzl",
    "googleapis_repositories",
    "mixerapi_repositories",
)

googleapis_repositories()

mixerapi_repositories()

bind(
    name = "boringssl_crypto",
    actual = "//external:ssl",
)

ENVOY_SHA = "6cb0983a1ce74c55aaf0124bd2227be8f9efa2de"  # Sep 26, 2017 (use github to download tclap instead of sourceforge)

http_archive(
    name = "envoy",
    strip_prefix = "envoy-" + ENVOY_SHA,
    url = "https://github.com/envoyproxy/envoy/archive/" + ENVOY_SHA + ".zip",
)

load("@envoy//bazel:repositories.bzl", "envoy_dependencies")

envoy_dependencies()

load("@envoy//bazel:cc_configure.bzl", "cc_configure")

cc_configure()

load("@envoy_api//bazel:repositories.bzl", "api_dependencies")

api_dependencies()

# Following go repositories are for building go integration test for mixer filter.
git_repository(
    name = "io_bazel_rules_go",
    commit = "9cf23e2aab101f86e4f51d8c5e0f14c012c2161c",  # Oct 12, 2017 (Add `build_external` option to `go_repository`)
    remote = "https://github.com/bazelbuild/rules_go.git",
)

load("@io_bazel_rules_go//go:def.bzl", "go_rules_dependencies", "go_register_toolchains")
go_rules_dependencies()
go_register_toolchains()

load("@io_bazel_rules_go//proto:def.bzl", "proto_register_toolchains")
proto_register_toolchains()

MIXER = "ba8ad5ca8ae77b946366e423d28b47cf3c8e1550"

git_repository(
    name = "com_github_istio_mixer",
    commit = MIXER,
    remote = "https://github.com/istio/mixer",
)