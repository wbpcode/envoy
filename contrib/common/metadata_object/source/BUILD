load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

envoy_cc_library(
    name = "metadata_object_lib",
    srcs = ["metadata_object.cc"],
    hdrs = ["metadata_object.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//envoy/common:hashable_interface",
        "//envoy/registry",
        "//envoy/stream_info:filter_state_interface",
        "//source/common/common:hash_lib",
        "//source/common/protobuf:utility_lib",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/types:optional",
    ],
)
