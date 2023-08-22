#include "envoy/config/trace/v3/http_tracer.pb.h"
#include "envoy/config/trace/v3/opentelemetry.pb.h"
#include "envoy/config/trace/v3/opentelemetry.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/extensions/tracers/opentelemetry/config.h"

#include "test/mocks/server/tracer_factory.h"
#include "test/mocks/server/tracer_factory_context.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

TEST(OpenTelemetryTracerConfigTest, OpenTelemetryTracerWithGrpcExporter) {
  NiceMock<Server::Configuration::MockTracerFactoryContext> context;
  context.server_factory_context_.cluster_manager_.initializeClusters({"fake_cluster"}, {});
  OpenTelemetryTracerFactory factory;

  const std::string yaml_string = R"EOF(
    http:
      name: envoy.tracers.opentelemetry
      typed_config:
        "@type": type.googleapis.com/envoy.config.trace.v3.OpenTelemetryConfig
        grpc_service:
          envoy_grpc:
            cluster_name: fake_cluster
          timeout: 0.250s
        service_name: fake_service_name
  )EOF";
  envoy::config::trace::v3::Tracing configuration;
  TestUtility::loadFromYaml(yaml_string, configuration);

  auto message = Config::Utility::translateToFactoryConfig(
      configuration.http(), ProtobufMessage::getStrictValidationVisitor(), factory);
  auto opentelemetry_tracer = factory.createTracerDriver(*message, context);
  EXPECT_NE(nullptr, opentelemetry_tracer);
}

TEST(OpenTelemetryTracerConfigTest, OpenTelemetryTracerWithHttpExporter) {
  NiceMock<Server::Configuration::MockTracerFactoryContext> context;
  context.server_factory_context_.cluster_manager_.initializeClusters({"fake_cluster"}, {});
  OpenTelemetryTracerFactory factory;

  const std::string yaml_string = R"EOF(
    http:
      name: envoy.tracers.opentelemetry
      typed_config:
        "@type": type.googleapis.com/envoy.config.trace.v3.OpenTelemetryConfig
        http_config:
          cluster_name: "my_o11y_backend"
          traces_path: "/otlp/v1/traces"
          hostname: "some-o11y.com"
          headers:
            - key: "Authorization"
              value: "auth-token"
          timeout: 0.250s
  )EOF";
  envoy::config::trace::v3::Tracing configuration;
  TestUtility::loadFromYaml(yaml_string, configuration);

  auto message = Config::Utility::translateToFactoryConfig(
      configuration.http(), ProtobufMessage::getStrictValidationVisitor(), factory);
  auto opentelemetry_tracer = factory.createTracerDriver(*message, context);
  EXPECT_NE(nullptr, opentelemetry_tracer);
}

TEST(OpenTelemetryTracerConfigTest, OpenTelemetryTracerNoExporter) {
  NiceMock<Server::Configuration::MockTracerFactoryContext> context;
  context.server_factory_context_.cluster_manager_.initializeClusters({"fake_cluster"}, {});
  OpenTelemetryTracerFactory factory;

  const std::string yaml_string = R"EOF(
    http:
      name: envoy.tracers.opentelemetry
      typed_config:
        "@type": type.googleapis.com/envoy.config.trace.v3.OpenTelemetryConfig
  )EOF";
  envoy::config::trace::v3::Tracing configuration;
  TestUtility::loadFromYaml(yaml_string, configuration);

  auto message = Config::Utility::translateToFactoryConfig(
      configuration.http(), ProtobufMessage::getStrictValidationVisitor(), factory);

  EXPECT_THROW_WITH_REGEX(factory.createTracerDriver(*message, context), EnvoyException,
                          "field: \"export_protocol\", reason: is required");
}

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
