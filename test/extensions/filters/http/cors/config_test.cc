#include "envoy/extensions/filters/http/cors/v3/cors.pb.h"
#include "envoy/extensions/filters/http/cors/v3/cors.pb.validate.h"

#include "source/extensions/filters/http/cors/config.h"

#include "test/mocks/server/factory_context.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cors {
namespace {

TEST(CorsFilterConfigTest, CorsFilterWithServerContext) {
  envoy::extensions::filters::http::cors::v3::Cors config;
  testing::NiceMock<Server::Configuration::MockServerFactoryContext> context;
  CorsFilterFactory factory;
  Http::FilterFactoryCb cb =
      factory.createFilterFactoryFromProtoWithServerContext(config, "stats", context);
  Http::MockFilterChainFactoryCallbacks filter_callbacks;
  EXPECT_CALL(filter_callbacks, addStreamFilter(_));
  cb(filter_callbacks);
}

} // namespace
} // namespace Cors
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
