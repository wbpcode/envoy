load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_contrib_extension",
    "envoy_cc_library",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

# Peer Metadata L7 HTTP filter

envoy_cc_library(
    name = "filter_lib",
    srcs = ["filter.cc"],
    hdrs = ["filter.h"],
    deps = [
        "//contrib/common/metadata_object/source:metadata_object_lib",
        "//contrib/common/workload_discovery/source:api_lib",
        "//envoy/http:filter_interface",
        "//envoy/registry",
        "//envoy/server:factory_context_interface",
        "//source/common/common:base64_lib",
        "//source/common/common:hash_lib",
        "//source/common/http:header_utility_lib",
        "//source/common/http:utility_lib",
        "//source/common/network:utility_lib",
        "//source/common/singleton:const_singleton",
        "//source/extensions/filters/common/expr:cel_state_lib",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
        "@envoy_api//contrib/envoy/extensions/filters/http/peer_metadata/v3alpha:pkg_cc_proto",
    ],
)

envoy_cc_contrib_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":filter_lib",
        "//envoy/registry",
        "//source/extensions/filters/http/common:factory_base_lib",
        "@envoy_api//contrib/envoy/extensions/filters/http/peer_metadata/v3alpha:pkg_cc_proto",
    ],
)
