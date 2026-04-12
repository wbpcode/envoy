#include "source/common/router/context_impl.h"

#include "source/common/config/utility.h"
#include "source/common/config/well_known_names.h"
#include "source/common/runtime/runtime_features.h"
#include "source/common/stats/context_impl.h"
#include "source/common/stats/utility.h"

namespace Envoy {
namespace Router {

namespace {

Stats::ScopeSharedPtr
createRouteStatsScope(Stats::Scope& scope, const RouteStatNames& route_stat_names,
                      const Stats::WellKnownTagStatNames& well_known_tag_stat_names,
                      Stats::StatName vhost_stat_name, Stats::StatName route_stat_name) {
  if (Runtime::runtimeFeatureEnabled("envoy.reloadable_features.no_stats_tag_extraction")) {
    return scope.createScope({
        Stats::StatElement{.value_ = route_stat_names.vhost_},
        Stats::StatElement{.value_ = vhost_stat_name,
                           .name_ = well_known_tag_stat_names.virtual_host_,
                           .ignore_name_ = true},
        Stats::StatElement{.value_ = route_stat_names.route_},
        Stats::StatElement{.value_ = route_stat_name,
                           .name_ = well_known_tag_stat_names.route_,
                           .ignore_name_ = true},
    });
  }
  return Stats::Utility::scopeFromStatNames(
      scope, {route_stat_names.vhost_, vhost_stat_name, route_stat_names.route_, route_stat_name});
}

} // namespace

ContextImpl::ContextImpl(Stats::SymbolTable& symbol_table)
    : stat_names_(symbol_table), route_stat_names_(symbol_table),
      virtual_cluster_stat_names_(symbol_table),
      generic_conn_pool_factory_(Envoy::Config::Utility::getFactoryByName<GenericConnPoolFactory>(
          "envoy.filters.connection_pools.http.generic")) {}

RouteStatsContextImpl::RouteStatsContextImpl(
    Stats::Scope& scope, const RouteStatNames& route_stat_names,
    const Stats::WellKnownTagStatNames& well_known_tag_stat_names,
    const Stats::StatName& vhost_stat_name, const std::string& stat_prefix)
    : route_stat_name_storage_(stat_prefix, scope.symbolTable()),
      route_stats_scope_(createRouteStatsScope(scope, route_stat_names, well_known_tag_stat_names,
                                               vhost_stat_name,
                                               route_stat_name_storage_.statName())),
      route_stat_name_(route_stat_name_storage_.statName()),
      route_stats_(route_stat_names, *route_stats_scope_) {}

} // namespace Router
} // namespace Envoy
