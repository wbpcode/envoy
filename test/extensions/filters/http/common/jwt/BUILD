load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_cc_test_library",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "check_audience_test",

    srcs = [
        "check_audience_test.cc",
    ],
    deps = [
        "//source/extensions/filters/http/common/jwt:jwt_verify_lib",


    ],
)

envoy_cc_test(
    name = "jwt_test",

    srcs = [
        "jwt_test.cc",
    ],

    deps = [
        "//source/extensions/filters/http/common/jwt:jwt_verify_lib",


    ],
)

envoy_cc_test(
    name = "jwks_test",

    srcs = [
        "jwks_test.cc",
        "test_common.h",
    ],
    deps = [
        "//source/extensions/filters/http/common/jwt:jwt_verify_lib",


    ],
)

envoy_cc_test(
    name = "simple_lru_cache_test",

    srcs = [
        "simple_lru_cache_test.cc",
    ],

    deps = [
        "//source/extensions/filters/http/common/jwt:simple_lru_cache_lib",


    ],
)

envoy_cc_test(
    name = "verify_x509_test",

    srcs = [
        "test_common.h",
        "verify_x509_test.cc",
    ],

    deps = [
        "//source/extensions/filters/http/common/jwt:jwt_verify_lib",


    ],
)

envoy_cc_test(
    name = "verify_audiences_test",

    srcs = [
        "test_common.h",
        "verify_audiences_test.cc",
    ],

    deps = [
        "//source/extensions/filters/http/common/jwt:jwt_verify_lib",


    ],
)

envoy_cc_test(
    name = "jwt_time_test",

    srcs = [
        "jwt_time_test.cc",
        "test_common.h",
    ],

    deps = [
        "//source/extensions/filters/http/common/jwt:jwt_verify_lib",


    ],
)

envoy_cc_test(
    name = "verify_jwk_rsa_test",

    srcs = [
        "test_common.h",
        "verify_jwk_rsa_test.cc",
    ],

    deps = [
        "//source/extensions/filters/http/common/jwt:jwt_verify_lib",


    ],
)

envoy_cc_test(
    name = "verify_jwk_rsa_pss_test",

    srcs = [
        "test_common.h",
        "verify_jwk_rsa_pss_test.cc",
    ],

    deps = [
        "//source/extensions/filters/http/common/jwt:jwt_verify_lib",


    ],
)

envoy_cc_test(
    name = "verify_jwk_ec_test",

    srcs = [
        "test_common.h",
        "verify_jwk_ec_test.cc",
    ],

    deps = [
        "//source/extensions/filters/http/common/jwt:jwt_verify_lib",


    ],
)

envoy_cc_test(
    name = "verify_jwk_hmac_test",

    srcs = [
        "test_common.h",
        "verify_jwk_hmac_test.cc",
    ],

    deps = [
        "//source/extensions/filters/http/common/jwt:jwt_verify_lib",


    ],
)

envoy_cc_test(
    name = "verify_jwk_okp_test",

    srcs = [
        "test_common.h",
        "verify_jwk_okp_test.cc",
    ],

    deps = [
        "//source/extensions/filters/http/common/jwt:jwt_verify_lib",


    ],
)

envoy_cc_test(
    name = "verify_pem_rsa_test",

    srcs = [
        "test_common.h",
        "verify_pem_rsa_test.cc",
    ],

    deps = [
        "//source/extensions/filters/http/common/jwt:jwt_verify_lib",


    ],
)

envoy_cc_test(
    name = "verify_pem_ec_test",

    srcs = [
        "test_common.h",
        "verify_pem_ec_test.cc",
    ],

    deps = [
        "//source/extensions/filters/http/common/jwt:jwt_verify_lib",


    ],
)

envoy_cc_test(
    name = "verify_pem_okp_test",

    srcs = [
        "test_common.h",
        "verify_pem_okp_test.cc",
    ],

    deps = [
        "//source/extensions/filters/http/common/jwt:jwt_verify_lib",


    ],
)
