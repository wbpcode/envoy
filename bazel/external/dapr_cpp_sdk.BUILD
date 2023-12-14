licenses(["notice"])  # Apache 2

cc_library(
    name = "dapr_proto",
    hdrs = glob([
        "src/dapr/proto/*.h",
    ]),
    srcs = glob([
        "src/dapr/proto/*.cc",
    ]),
    includes = ["src"],
    visibility = ["//visibility:public"],
    deps = ["//external:protobuf", "//external:grpc",],
)
