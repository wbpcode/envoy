load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "http",
    srcs = [
        "http.cc",
    ],
    hdrs = [
        "http.h",
    ],
    deps = [
        "//source/common/http/http1:codec_lib",
        "//source/extensions/filters/network/generic_proxy/interface:codec_interface",
    ],
)
