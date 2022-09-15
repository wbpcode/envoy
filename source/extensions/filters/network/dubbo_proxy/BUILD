load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "decoder_lib",
    srcs = ["decoder.cc"],
    hdrs = ["decoder.h"],
    deps = [
        ":decoder_events_lib",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:logger_lib",
        "//source/extensions/common/dubbo:codec_lib",
        "//source/extensions/common/dubbo:hessian2_serializer_lib",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":conn_manager_lib",
        "//envoy/registry",
        "//envoy/stats:stats_interface",
        "//envoy/stats:stats_macros",
        "//source/common/common:utility_lib",
        "//source/common/config:utility_lib",
        "//source/extensions/filters/network:well_known_names",
        "//source/extensions/filters/network/common:factory_base_lib",
        "//source/extensions/filters/network/dubbo_proxy/filters:factory_base_lib",
        "//source/extensions/filters/network/dubbo_proxy/router:config",
        "//source/extensions/filters/network/dubbo_proxy/router:rds_lib",
        "//source/extensions/filters/network/dubbo_proxy/router:route_matcher",
        "//source/extensions/filters/network/dubbo_proxy/router:router_lib",
        "@envoy_api//envoy/extensions/filters/network/dubbo_proxy/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "decoder_events_lib",
    hdrs = ["decoder_event_handler.h"],
    deps = [
        "//envoy/network:connection_interface",
        "//envoy/network:filter_interface",
        "//source/common/buffer:buffer_lib",
        "//source/extensions/common/dubbo:metadata_lib",
    ],
)

envoy_cc_library(
    name = "stats_lib",
    hdrs = ["stats.h"],
    deps = [
        "//envoy/stats:stats_interface",
        "//envoy/stats:stats_macros",
    ],
)

envoy_cc_library(
    name = "app_exception_lib",
    srcs = ["app_exception.cc"],
    hdrs = ["app_exception.h"],
    deps = [
        "//envoy/buffer:buffer_interface",
        "//source/common/buffer:buffer_lib",
        "//source/extensions/common/dubbo:message_lib",
        "//source/extensions/common/dubbo:metadata_lib",
        "//source/extensions/filters/network/dubbo_proxy/filters:filter_interface",
    ],
)

envoy_cc_library(
    name = "heartbeat_response_lib",
    srcs = ["heartbeat_response.cc"],
    hdrs = ["heartbeat_response.h"],
    deps = [
        "//envoy/buffer:buffer_interface",
        "//source/extensions/common/dubbo:metadata_lib",
        "//source/extensions/filters/network/dubbo_proxy/filters:filter_interface",
    ],
)

envoy_cc_library(
    name = "conn_manager_lib",
    srcs = [
        "active_message.cc",
        "conn_manager.cc",
    ],
    hdrs = [
        "active_message.h",
        "conn_manager.h",
    ],
    deps = [
        ":app_exception_lib",
        ":decoder_events_lib",
        ":decoder_lib",
        ":heartbeat_response_lib",
        ":stats_lib",
        "//envoy/event:deferred_deletable",
        "//envoy/event:dispatcher_interface",
        "//envoy/network:connection_interface",
        "//envoy/network:filter_interface",
        "//envoy/stats:stats_interface",
        "//envoy/stats:timespan_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/buffer:watermark_buffer_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:linked_object",
        "//source/common/common:logger_lib",
        "//source/common/network:filter_lib",
        "//source/common/stats:timespan_lib",
        "//source/common/stream_info:stream_info_lib",
        "//source/extensions/common/dubbo:codec_lib",
        "//source/extensions/common/dubbo:hessian2_serializer_lib",
        "//source/extensions/filters/network/dubbo_proxy/filters:filter_interface",
        "//source/extensions/filters/network/dubbo_proxy/router:router_interface",
        "@envoy_api//envoy/extensions/filters/network/dubbo_proxy/v3:pkg_cc_proto",
    ],
)
