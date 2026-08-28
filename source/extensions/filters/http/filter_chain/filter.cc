#include "source/extensions/filters/http/filter_chain/filter.h"

#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "source/common/common/macros.h"
#include "source/common/config/utility.h"
#include "source/common/http/utility.h"
#include "source/extensions/filters/http/common/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace FilterChain {

namespace {

constexpr absl::string_view FilterChainName = "envoy.filters.http.filter_chain";

// Stats prefix used by the filter chains that are embedded in a route configuration. It is a
// static string because the extra factory context only holds a reference to it.
const std::string& perRouteStatsPrefix() { CONSTRUCT_ON_FIRST_USE(std::string, "filter_chain."); }

// Helper to process filter config and create filter factories.
absl::StatusOr<FilterFactoriesVector> createFilterFactoriesFromConfig(
    const envoy::extensions::filters::http::filter_chain::v3::FilterChain& proto_config,
    Server::Configuration::ServerFactoryContext& context,
    Server::Configuration::ExtraFactoryContext& extra_context) {
  FilterFactoriesVector filter_factories;
  filter_factories.reserve(proto_config.filters_size());

  for (const auto& filter_config : proto_config.filters()) {
    auto& factory =
        Config::Utility::getAndCheckFactory<Server::Configuration::NamedHttpFilterConfigFactory>(
            filter_config);
    if (factory.name() == FilterChainName) {
      return absl::InvalidArgumentError("FilterChain filter cannot be configured recursively.");
    }

    ProtobufTypes::MessagePtr message =
        Config::Utility::translateToFactoryConfig(filter_config, extra_context.visitor, factory);
    auto callback_or_error =
        Common::createHttpFilterFactory(factory, *message, context, extra_context);
    RETURN_IF_NOT_OK_REF(callback_or_error.status());

    auto filter_config_provider =
        Http::FilterChainUtility::createSingletonDownstreamFilterConfigProviderManager(context)
            ->createStaticFilterConfigProvider(std::move(callback_or_error.value()),
                                               filter_config.name());
    filter_factories.push_back({std::move(filter_config_provider)});
  }
  return filter_factories;
}

} // namespace

FilterChain::FilterChain(
    const envoy::extensions::filters::http::filter_chain::v3::FilterChain& proto_config,
    Server::Configuration::ServerFactoryContext& context,
    Server::Configuration::ExtraFactoryContext& extra_context, absl::Status& creation_status) {
  auto filter_factories_or = createFilterFactoriesFromConfig(proto_config, context, extra_context);
  SET_AND_RETURN_IF_NOT_OK(filter_factories_or.status(), creation_status);
  filter_factories_ = std::move(filter_factories_or.value());
  for (const auto& factory : filter_factories_) {
    filters_.insert(factory->name());
  }
}

FilterChainPerRouteConfig::FilterChainPerRouteConfig(
    const envoy::extensions::filters::http::filter_chain::v3::FilterChainConfigPerRoute&
        proto_config,
    Server::Configuration::ServerFactoryContext& context,
    Server::Configuration::ExtraFactoryContext& extra_context, absl::Status& creation_status) {
  // TODO(wbpcode): use the route name or vhost name as stats prefix?
  // The init manager of the enclosing route configuration, if any, is handed to the filters of the
  // embedded filter chain so that they can warm up their own resources. No concrete factory
  // context is carried over, so the embedded filters are always created by the unified factory
  // interface.
  Server::Configuration::ExtraFactoryContext chain_context{extra_context.visitor,
                                                           perRouteStatsPrefix()};
  chain_context.init_manager = extra_context.init_manager;
  filter_chain_ = std::make_shared<FilterChain>(proto_config.filter_chain(), context, chain_context,
                                                creation_status);
}

FilterChainConfig::FilterChainConfig(const FilterChainConfigProto& proto_config,
                                     Server::Configuration::ServerFactoryContext& context,
                                     Server::Configuration::ExtraFactoryContext& extra_context,
                                     absl::Status& creation_status)
    : stats_(createStats(extra_context.stats_prefix, extra_context.scopeOr(context))) {
  if (proto_config.has_default_filter_chain()) {
    default_filter_chain_ = std::make_shared<FilterChain>(proto_config.default_filter_chain(),
                                                          context, extra_context, creation_status);
  }
}

} // namespace FilterChain
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
