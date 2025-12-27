load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_contrib_extension",
    "envoy_cc_library",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

envoy_cc_library(
    name = "metadata_exchange",
    srcs = [
        "metadata_exchange.cc",
        "metadata_exchange_initial_header.cc",
    ],
    hdrs = [
        "metadata_exchange.h",
        "metadata_exchange_initial_header.h",
    ],
    repository = "@envoy",
    deps = [
        "//extensions/common:metadata_object_lib",
        "//source/extensions/common/workload_discovery:api_lib",
        "@envoy_api//contrib/envoy/extensions/filters/network/metadata_exchange/v3:pkg_cc_proto",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/strings",
        "@envoy//envoy/local_info:local_info_interface",
        "@envoy//envoy/network:connection_interface",
        "@envoy//envoy/network:filter_interface",
        "@envoy//envoy/runtime:runtime_interface",
        "@envoy//envoy/stats:stats_macros",
        "@envoy//envoy/stream_info:filter_state_interface",
        "@envoy//source/common/http:utility_lib",
        "@envoy//source/common/network:filter_state_dst_address_lib",
        "@envoy//source/common/network:utility_lib",
        "@envoy//source/common/protobuf",
        "@envoy//source/common/protobuf:utility_lib",
        "@envoy//source/common/stream_info:bool_accessor_lib",
        "@envoy//source/extensions/filters/common/expr:cel_state_lib",
    ],
)

envoy_cc_contrib_extension(
    name = "config_lib",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    repository = "@envoy",
    deps = [
        ":metadata_exchange",
        "@envoy_api//contrib/envoy/extensions/filters/network/metadata_exchange/v3:pkg_cc_proto",
        "@envoy//envoy/registry",
        "@envoy//envoy/server:filter_config_interface",
    ],
)

envoy_cc_test(
    name = "metadataexchange_test",
    srcs = [
        "metadata_exchange_test.cc",
    ],
    repository = "@envoy",
    deps = [
        ":config_lib",
        ":metadata_exchange",
        "@envoy//source/common/protobuf",
        "@envoy//test/mocks/local_info:local_info_mocks",
        "@envoy//test/mocks/network:network_mocks",
        "@envoy//test/mocks/protobuf:protobuf_mocks",
        "@envoy//test/mocks/server:server_factory_context_mocks",
    ],
)
