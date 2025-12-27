load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

envoy_cc_library(
    name = "api_lib",
    srcs = ["api.cc"],
    hdrs = ["api.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//contrib/common/metadata_object/source:metadata_object_lib",
        "//envoy/network:address_interface",
        "//envoy/server:factory_context_interface",
        "//envoy/singleton:manager_interface",
    ],
)
