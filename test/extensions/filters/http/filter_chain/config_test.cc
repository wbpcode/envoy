#include "envoy/extensions/filters/http/filter_chain/v3/filter_chain.pb.h"
#include "envoy/extensions/filters/http/filter_chain/v3/filter_chain.pb.validate.h"

#include "source/extensions/filters/http/filter_chain/config.h"
#include "source/extensions/filters/http/filter_chain/filter.h"

#include "test/mocks/server/factory_context.h"
#include "test/mocks/server/instance.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::NiceMock;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace FilterChain {
namespace {

TEST(FilterChainFilterFactoryTest, FilterChainFilterCorrectYaml) {
  const std::string yaml_string = R"EOF(
  filter_chain:
    filters:
    - name: envoy.filters.http.buffer
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.filters.http.buffer.v3.Buffer
        max_request_bytes: 1028
  )EOF";

  envoy::extensions::filters::http::filter_chain::v3::FilterChainConfig proto_config;
  TestUtility::loadFromYaml(yaml_string, proto_config);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  FilterChainFilterFactory factory;
  Http::FilterFactoryCb cb =
      factory.createFilterFactoryFromProto(proto_config, "stats", context).value();
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamFilter(_));
  cb(filter_callback);
}

TEST(FilterChainFilterFactoryTest, FilterChainFilterWithNamedChains) {
  const std::string yaml_string = R"EOF(
  filter_chain:
    filters:
    - name: envoy.filters.http.buffer
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.filters.http.buffer.v3.Buffer
        max_request_bytes: 1024
  filter_chains:
    "chain1":
      filters:
      - name: envoy.filters.http.buffer
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.filters.http.buffer.v3.Buffer
          max_request_bytes: 2048
    "chain2":
      filters:
      - name: envoy.filters.http.buffer
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.filters.http.buffer.v3.Buffer
          max_request_bytes: 4096
  )EOF";

  envoy::extensions::filters::http::filter_chain::v3::FilterChainConfig proto_config;
  TestUtility::loadFromYaml(yaml_string, proto_config);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  FilterChainFilterFactory factory;
  Http::FilterFactoryCb cb =
      factory.createFilterFactoryFromProto(proto_config, "stats", context).value();
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamFilter(_));
  cb(filter_callback);
}

TEST(FilterChainFilterFactoryTest, FilterChainFilterEmptyConfig) {
  envoy::extensions::filters::http::filter_chain::v3::FilterChainConfig proto_config;
  NiceMock<Server::Configuration::MockFactoryContext> context;
  FilterChainFilterFactory factory;
  Http::FilterFactoryCb cb =
      factory.createFilterFactoryFromProto(proto_config, "stats", context).value();
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamFilter(_));
  cb(filter_callback);
}

TEST(FilterChainFilterFactoryTest, FilterChainFilterEmptyProto) {
  FilterChainFilterFactory factory;
  auto empty_proto = factory.createEmptyConfigProto();
  EXPECT_NE(nullptr,
            dynamic_cast<envoy::extensions::filters::http::filter_chain::v3::FilterChainConfig*>(
                empty_proto.get()));
}

TEST(FilterChainFilterFactoryTest, FilterChainFilterEmptyRouteProto) {
  FilterChainFilterFactory factory;
  EXPECT_NO_THROW({
    EXPECT_NE(nullptr,
              dynamic_cast<
                  envoy::extensions::filters::http::filter_chain::v3::FilterChainConfigPerRoute*>(
                  factory.createEmptyRouteConfigProto().get()));
  });
}

TEST(FilterChainFilterFactoryTest, FilterChainFilterRouteSpecificConfig) {
  FilterChainFilterFactory factory;
  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context;

  const std::string yaml_string = R"EOF(
  filter_chain_name: test_chain
  )EOF";

  ProtobufTypes::MessagePtr proto_config = factory.createEmptyRouteConfigProto();
  TestUtility::loadFromYaml(yaml_string, *proto_config);

  Router::RouteSpecificFilterConfigConstSharedPtr route_config =
      factory
          .createRouteSpecificFilterConfig(*proto_config, factory_context,
                                           ProtobufMessage::getNullValidationVisitor())
          .value();
  EXPECT_TRUE(route_config.get());

  const auto* inflated = dynamic_cast<const FilterChainPerRouteConfig*>(route_config.get());
  EXPECT_TRUE(inflated);
  EXPECT_EQ(inflated->filterChainName(), "test_chain");
}

TEST(FilterChainFilterFactoryTest, FilterChainFilterRouteSpecificConfigWithFilterChain) {
  FilterChainFilterFactory factory;
  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context;

  const std::string yaml_string = R"EOF(
  filter_chain:
    filters:
    - name: envoy.filters.http.buffer
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.filters.http.buffer.v3.Buffer
        max_request_bytes: 1024
  )EOF";

  ProtobufTypes::MessagePtr proto_config = factory.createEmptyRouteConfigProto();
  TestUtility::loadFromYaml(yaml_string, *proto_config);

  Router::RouteSpecificFilterConfigConstSharedPtr route_config =
      factory
          .createRouteSpecificFilterConfig(*proto_config, factory_context,
                                           ProtobufMessage::getNullValidationVisitor())
          .value();
  EXPECT_TRUE(route_config.get());

  const auto* inflated = dynamic_cast<const FilterChainPerRouteConfig*>(route_config.get());
  EXPECT_TRUE(inflated);
  EXPECT_NE(inflated->filterChain(), nullptr);
}

TEST(FilterChainFilterFactoryTest, FilterChainValidationError) {
  // Test that validation fails when filter_chain has no filters
  const std::string yaml_string = R"EOF(
  filter_chain:
    filters: []
  )EOF";

  envoy::extensions::filters::http::filter_chain::v3::FilterChainConfig proto_config;
  EXPECT_THROW_WITH_REGEX(TestUtility::loadFromYaml(yaml_string, proto_config), EnvoyException,
                          "Proto constraint validation failed");
}

} // namespace
} // namespace FilterChain
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
