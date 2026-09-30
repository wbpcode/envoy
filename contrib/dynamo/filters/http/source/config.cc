#include "contrib/dynamo/filters/http/source/config.h"

#include <string>

#include "envoy/registry/registry.h"

#include "contrib/dynamo/filters/http/source/dynamo_filter.h"
#include "contrib/dynamo/filters/http/source/dynamo_stats.h"
#include "contrib/envoy/extensions/filters/http/dynamo/v3/dynamo.pb.validate.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Dynamo {

absl::StatusOr<Http::FilterFactoryCb> DynamoFilterConfig::createHttpFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::dynamo::v3::Dynamo&,
    Server::Configuration::ServerFactoryContext& context,
    Server::Configuration::ExtraFactoryContext& extra_context) {
  // The stats live under the stats prefix, so they go in the prefixed scope when the filter chain
  // provides one (the prefix is then part of the scope's name and statsPrefixOr() is empty) and
  // under the stats prefix otherwise.
  auto stats = std::make_shared<DynamoStats>(extra_context.statsPrefixScopeOr(context),
                                             extra_context.statsPrefixOr());
  return [&context, stats](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(
        std::make_shared<Dynamo::DynamoFilter>(context.runtime(), stats, context.timeSource()));
  };
}

/**
 * Static registration for the http dynamodb filter. @see RegisterFactory.
 */
LEGACY_REGISTER_FACTORY(DynamoFilterConfig, Server::Configuration::NamedHttpFilterConfigFactory,
                        "envoy.http_dynamo_filter");

} // namespace Dynamo
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
