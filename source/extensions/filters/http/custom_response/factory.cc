#include "source/extensions/filters/http/custom_response/factory.h"

#include "source/extensions/filters/http/custom_response/custom_response_filter.h"

#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CustomResponse {

absl::StatusOr<::Envoy::Http::FilterFactoryCb>
CustomResponseFilterFactory::createHttpFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::custom_response::v3::CustomResponse& config,
    Envoy::Server::Configuration::ServerFactoryContext& context,
    Server::Configuration::ExtraFactoryContext& extra_context) {
  // The policies of this filter create their stats in the server's scope rather than in the scope
  // of this context, so the prefix they are given must be joined with the prefix of that scope,
  // otherwise the prefix would be missing from the resulting stat names.
  Stats::Scope& scope = extra_context.scopeOr(context);
  const std::string scope_prefix = scope.constSymbolTable().toString(scope.prefix());
  const std::string policy_stats_prefix =
      scope_prefix.empty() ? extra_context.stats_prefix
                           : absl::StrCat(scope_prefix, ".", extra_context.stats_prefix);
  Stats::StatNameManagedStorage prefix(policy_stats_prefix, context.scope().symbolTable());
  auto config_ptr = std::make_shared<FilterConfig>(config, context, prefix.statName());
  return [config_ptr](::Envoy::Http::FilterChainFactoryCallbacks& callbacks) mutable -> void {
    callbacks.addStreamFilter(std::make_shared<CustomResponseFilter>(config_ptr));
  };
}

absl::StatusOr<Router::RouteSpecificFilterConfigConstSharedPtr>
CustomResponseFilterFactory::createRouteSpecificFilterConfigTyped(
    const envoy::extensions::filters::http::custom_response::v3::CustomResponse& config,
    Envoy::Server::Configuration::ServerFactoryContext& context,
    ProtobufMessage::ValidationVisitor&) {
  return std::make_shared<FilterConfig>(config, context, context.scope().prefix());
}
/**
 * Static registration for the filter. @see RegisterFactory.
 */
REGISTER_FACTORY(CustomResponseFilterFactory, Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace CustomResponse
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
