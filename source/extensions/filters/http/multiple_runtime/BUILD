load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "multiple_runtime_lib",
    srcs = ["multiple_runtime.cc"],
    hdrs = ["multiple_runtime.h"],
    deps = [
        ":state_store_interface",
        "//envoy/server:filter_config_interface",
        "//source/common/config:utility_lib",
        "//source/common/protobuf:utility_lib",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
        "@envoy_api//envoy/extensions/filters/http/multiple_runtime/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":multiple_runtime_lib",
        "//envoy/registry",
        "//source/common/protobuf:utility_lib",
        "//source/extensions/filters/http/common:factory_base_lib",
        "@envoy_api//envoy/extensions/filters/http/multiple_runtime/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "state_store_interface",
    hdrs = ["state_store.h"],
    deps = [
        "//envoy/server:factory_context_interface",
        "@com_github_dapr_cpp_sdk//:dapr_proto",
    ],
)
