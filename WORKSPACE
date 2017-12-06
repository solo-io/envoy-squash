
PROXY_SHA = "6a9fe308431755e19358b76da38738c5af250b04"

http_archive(
    name = "proxy",
    strip_prefix = "proxy-" + PROXY_SHA,
    url = "https://github.com/istio/proxy/archive/" + PROXY_SHA + ".zip",
)

load(
    "@proxy//src/envoy/mixer:repositories.bzl",
    "mixer_client_repositories",
)

mixer_client_repositories()

load(
    "@mixerclient_git//:repositories.bzl",
    "mixerapi_repositories",
)

mixerapi_repositories()

bind(
    name = "boringssl_crypto",
    actual = "//external:ssl",
)

ENVOY_SHA = "e593fedc3232fbb694f3ec985567a2c7dff05212"  # Oct 31, 2017

http_archive(
    name = "envoy",
    strip_prefix = "envoy-" + ENVOY_SHA,
    url = "https://github.com/envoyproxy/envoy/archive/" + ENVOY_SHA + ".zip",
)

load("@envoy//bazel:repositories.bzl", "envoy_dependencies")

envoy_dependencies(repository="@envoy", skip_targets=["io_bazel_rules_go"])

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

load("@mixerapi_git//:api_dependencies.bzl", "mixer_api_for_proxy_dependencies")
mixer_api_for_proxy_dependencies()

ISTIO_SHA = "9386e6c1cc95f2f405383c547b9d8329e557397b"

git_repository(
    name = "io_istio_istio",
    commit = ISTIO_SHA,
    remote = "https://github.com/istio/istio",
)

load("@proxy//src/envoy/mixer/integration_test:repositories.bzl", "mixer_test_repositories")
mixer_test_repositories()
