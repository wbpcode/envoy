load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_fuzz_test",
    "envoy_cc_test_library",
    "envoy_package",
    "envoy_proto_library",
)
load(
    "//source/extensions:all_extensions.bzl",
    "envoy_all_http_filters",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_proto_library(
    name = "jwt_verify_lib_fuzz_input_proto",
    srcs = ["jwt_verify_lib_fuzz_input.proto"],
)

envoy_cc_fuzz_test(
    name = "jwt_verify_lib_fuzz_test",
    srcs = [
        "jwt_verify_lib_fuzz_test.cc",
    ],
    corpus = "corpus",
    deps = [
        ":jwt_verify_lib_fuzz_input_proto_cc_proto",
        "//source/extensions/filters/http/common/jwt:jwt_verify_lib",
    ],
)

envoy_cc_fuzz_test(
    name = "corpus_format_test",
    srcs = [
        "corpus_format_test.cc",
    ],
    corpus = "corpus",
    deps = [
        ":jwt_verify_lib_fuzz_input_proto_cc_proto",
        "//source/extensions/filters/http/common/jwt:jwt_verify_lib",
    ],
)
