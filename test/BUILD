package(default_visibility = ["//visibility:public"])

load(
    "@envoy//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
)

envoy_cc_test(
    name = "squash_filter_integration_test",
    srcs = ["squash_filter_integration_test.cc"],
    data = [":envoy-test.yaml"],
    repository = "@envoy",
    deps = [
        "//:squash_filter_config",
        "@envoy//test/integration:http_integration_lib",
        "@envoy//test/integration:integration_lib",
    ],
)

envoy_cc_test(
    name = "squash_filter_test",
    srcs = ["squash_filter_test.cc", "squash_filter_config_test.cc"],
    repository = "@envoy",
    deps = [
        "//:squash_filter_config",
        "@envoy//test/mocks/upstream:upstream_mocks",
        "@envoy//test/mocks/server:server_mocks",
        "@envoy//test/test_common:utility_lib",
    ],
)
