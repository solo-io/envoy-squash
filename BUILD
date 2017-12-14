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
        "@envoy//source/exe:envoy_main_entry_lib",
        "@proxy//src/envoy/mixer:filter_lib",
    ],
)

envoy_cc_library(
    name = "squash_filter_lib",
    srcs = [
        "squash_filter.cc",
        "squash_filter_config.cc",
    ],
    hdrs = [
        "squash_filter.h",
        "squash_filter_config.h",
    ],
    repository = "@envoy",
    deps = [
        ":squash_cc_proto",
        "@envoy//source/exe:envoy_common_lib",
    ],
)

proto_library(
    name = "squash_proto",
    srcs = ["squash.proto"],
    deps = ["@com_google_protobuf//:duration_proto"],
)

cc_proto_library(
    name = "squash_cc_proto",
    deps = [":squash_proto"],
)

envoy_cc_library(
    name = "squash_filter_config",
    srcs = ["squash_filter_config_factory.cc"],
    hdrs = ["squash_filter_config_factory.h"],
    repository = "@envoy",
    visibility = ["//visibility:public"],
    deps = [
        ":squash_filter_lib",
        "@envoy//source/exe:envoy_common_lib",
    ],
)

envoy_cc_test(
    name = "squash_filter_integration_test",
    srcs = ["squash_filter_integration_test.cc"],
    data = [":envoy-test.yaml"],
    repository = "@envoy",
    deps = [
        ":squash_filter_config",
        "@envoy//test/integration:http_integration_lib",
        "@envoy//test/integration:integration_lib",
    ],
)

envoy_cc_test(
    name = "squash_filter_test",
    srcs = ["squash_filter_test.cc"],
    repository = "@envoy",    
    deps = [
        ":squash_filter_lib",
        "@envoy//test/mocks/upstream:upstream_mocks",
        "@envoy//test/test_common:utility_lib",
    ],
)
