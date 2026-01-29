#include "envoy/extensions/filters/http/mcp_router/v3/mcp_router.pb.h"

#include "source/extensions/filters/http/mcp_router/config.h"

#include "test/mocks/server/factory_context.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace McpRouter {
namespace {

TEST(McpRouterFilterConfigTest, McpRouterFilterWithServerContext) {
  envoy::extensions::filters::http::mcp_router::v3::McpRouter proto_config;
  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  McpRouterFilterConfigFactory factory;
  Http::FilterFactoryCb cb =
      factory.createFilterFactoryFromProtoWithServerContext(proto_config, "stats", context);
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamDecoderFilter(_));
  cb(filter_callback);
}

} // namespace
} // namespace McpRouter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
