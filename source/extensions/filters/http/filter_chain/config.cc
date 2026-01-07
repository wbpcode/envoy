#include "source/extensions/filters/http/filter_chain/config.h"

#include "envoy/registry/registry.h"

#include "source/extensions/filters/http/filter_chain/filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace FilterChain {

Http::FilterFactoryCb FilterChainFilterFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::filter_chain::v3::FilterChainConfig& proto_config,
    const std::string& stats_prefix, Server::Configuration::FactoryContext& context) {
  auto filter_config = std::make_shared<FilterChainConfig>(proto_config,
                                                           context.serverFactoryContext(),
                                                           stats_prefix);
  return [filter_config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(std::make_shared<Filter>(filter_config));
  };
}

absl::StatusOr<Router::RouteSpecificFilterConfigConstSharedPtr>
FilterChainFilterFactory::createRouteSpecificFilterConfigTyped(
    const envoy::extensions::filters::http::filter_chain::v3::FilterChainConfigPerRoute&
        proto_config,
    Server::Configuration::ServerFactoryContext& context, ProtobufMessage::ValidationVisitor&) {
  return std::make_shared<FilterChainPerRouteConfig>(proto_config, context, "filter_chain.");
}

/**
 * Static registration for the filter chain filter. @see RegisterFactory.
 */
REGISTER_FACTORY(FilterChainFilterFactory, Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace FilterChain
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
