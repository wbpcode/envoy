#include "source/extensions/filters/http/router/config.h"

#include "envoy/extensions/filters/http/router/v3/router.pb.h"
#include "envoy/extensions/filters/http/router/v3/router.pb.validate.h"

#include "source/common/router/router.h"
#include "source/common/router/shadow_writer_impl.h"
#include "source/server/generic_factory_context.h"

#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RouterFilter {

absl::StatusOr<Http::FilterFactoryCb> RouterFilterConfig::createHttpFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::router::v3::Router& proto_config,
    Server::Configuration::ServerFactoryContext& context,
    Server::Configuration::ExtraFactoryContext& extra_context) {
  // Unlike the other HTTP filters, the router does not create every stat under its own stat
  // prefix: the virtual host, virtual cluster and route level stats ('vhost.<name>.vcluster.
  // <name>.upstream_rq_*' and friends) are charged to this scope with names of their own and are
  // documented to live at the root. So the router keeps creating its stats in the server's scope
  // and carries the connection manager's prefix in the stat prefix instead, rather than taking the
  // 'http.<stat_prefix>.' scope of its factory context.
  Stats::Scope& filter_scope = extra_context.scopeOr(context);
  const std::string scope_prefix = filter_scope.constSymbolTable().toString(filter_scope.prefix());
  const std::string stats_prefix =
      scope_prefix.empty() ? extra_context.stats_prefix
                           : absl::StrCat(scope_prefix, ".", extra_context.stats_prefix);

  // The stat prefix name must be created in the symbol table of the same scope that will be used
  // to create the stats.
  Stats::Scope& scope = context.serverScope();
  Stats::StatNameManagedStorage prefix(stats_prefix, scope.symbolTable());
  Server::GenericFactoryContextImpl generic_context(context, scope, extra_context.visitor,
                                                    extra_context.init_manager);
  auto config_or_error = Router::FilterConfig::create(
      prefix.statName(), generic_context,
      std::make_unique<Router::ShadowWriterImpl>(context.clusterManager()), proto_config);
  RETURN_IF_NOT_OK_REF(config_or_error.status());
  Router::FilterConfigSharedPtr filter_config(std::move(*config_or_error));

  return [filter_config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamDecoderFilter(
        std::make_shared<Router::ProdFilter>(filter_config, filter_config->default_stats_));
  };
}

/**
 * Static registration for the router filter. @see RegisterFactory.
 */
LEGACY_REGISTER_FACTORY(RouterFilterConfig, Server::Configuration::NamedHttpFilterConfigFactory,
                        "envoy.router");

} // namespace RouterFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
