#include "envoy/extensions/filters/http/csrf/v3/csrf.pb.h"
#include "envoy/extensions/filters/http/csrf/v3/csrf.pb.validate.h"

#include "source/extensions/filters/http/csrf/config.h"

#include "test/mocks/server/factory_context.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Csrf {
namespace {

TEST(CsrfFilterConfigTest, CsrfFilterWithServerContext) {
  const std::string yaml_string = R"EOF(
  filter_enabled:
    default_value:
      numerator: 100
      denominator: HUNDRED
  )EOF";

  envoy::extensions::filters::http::csrf::v3::CsrfPolicy config;
  TestUtility::loadFromYamlAndValidate(yaml_string, config);
  testing::NiceMock<Server::Configuration::MockServerFactoryContext> context;
  CsrfFilterFactory factory;
  Http::FilterFactoryCb cb =
      factory.createFilterFactoryFromProtoWithServerContext(config, "stats", context);
  Http::MockFilterChainFactoryCallbacks filter_callbacks;
  EXPECT_CALL(filter_callbacks, addStreamDecoderFilter(_));
  cb(filter_callbacks);
}

} // namespace
} // namespace Csrf
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
