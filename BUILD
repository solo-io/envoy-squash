package(default_visibility = ["//visibility:public"])

load(
    "@envoy//bazel:envoy_build_system.bzl",
    "envoy_cc_binary",
    "envoy_cc_library",
    "envoy_cc_test",
)

envoy_cc_binary(
    name = "envoy",
    repository = "@envoy",
    deps = [
        ":squash_filter_config",
        "@proxy//src/envoy/mixer:filter_lib",
        "@envoy//source/exe:envoy_main_entry_lib",
    ],
)

envoy_cc_library(
    name = "squash_filter_lib",
    srcs = ["squash_filter.cc"],
    hdrs = ["squash_filter.h"],
    repository = "@envoy",
    deps = [
        "@envoy//source/exe:envoy_common_lib",
    ],
)

envoy_cc_library(
    name = "squash_filter_config",
    srcs = ["squash_filter_config.cc"],
    repository = "@envoy",
    deps = [
        ":squash_filter_lib",
        "@envoy//source/exe:envoy_common_lib",
    ],
)

envoy_cc_test(
    name = "squash_filter_integration_test",
    srcs = ["squash_filter_integration_test.cc"],
    data = [":envoy-test.conf"],
    repository = "@envoy",
    deps = [
        ":squash_filter_config",
        "@envoy//test/integration:integration_lib",
        "@envoy//test/integration:http_integration_lib"
    ],
)
