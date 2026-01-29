#include "envoy/extensions/filters/http/on_demand/v3/on_demand.pb.h"
#include "envoy/extensions/filters/http/on_demand/v3/on_demand.pb.validate.h"

#include "source/extensions/filters/http/on_demand/config.h"

#include "test/mocks/server/factory_context.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace OnDemand {
namespace {

TEST(OnDemandFilterConfigTest, OnDemandFilterWithServerContext) {
  envoy::extensions::filters::http::on_demand::v3::OnDemand config;
  testing::NiceMock<Server::Configuration::MockServerFactoryContext> context;
  OnDemandFilterFactory factory;
  Http::FilterFactoryCb cb =
      factory.createFilterFactoryFromProtoWithServerContext(config, "stats", context);
  Http::MockFilterChainFactoryCallbacks filter_callbacks;
  EXPECT_CALL(filter_callbacks, addStreamDecoderFilter(_));
  cb(filter_callbacks);
}

} // namespace
} // namespace OnDemand
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
