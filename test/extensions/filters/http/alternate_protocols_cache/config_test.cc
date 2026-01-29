#include "envoy/extensions/filters/http/alternate_protocols_cache/v3/alternate_protocols_cache.pb.h"
#include "envoy/extensions/filters/http/alternate_protocols_cache/v3/alternate_protocols_cache.pb.validate.h"

#include "source/extensions/filters/http/alternate_protocols_cache/config.h"

#include "test/mocks/server/factory_context.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AlternateProtocolsCache {
namespace {

TEST(AlternateProtocolsCacheFilterConfigTest, CreateFilterFactoryFromProtoWithServerContext) {
  const std::string yaml_string = R"EOF(
  alternate_protocols_cache_options:
    name: default_cache
  )EOF";

  envoy::extensions::filters::http::alternate_protocols_cache::v3::FilterConfig proto_config;
  TestUtility::loadFromYaml(yaml_string, proto_config);

  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  AlternateProtocolsCacheFilterFactory factory;

  Http::FilterFactoryCb cb =
      factory.createFilterFactoryFromProtoWithServerContext(proto_config, "stats", context);
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, dispatcher()).WillRepeatedly(ReturnRef(context.dispatcher_));
  EXPECT_CALL(filter_callback, addStreamEncoderFilter(_));
  cb(filter_callback);
}

} // namespace
} // namespace AlternateProtocolsCache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
