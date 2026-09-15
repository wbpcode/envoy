#pragma once

#include "envoy/extensions/filters/http/credential_injector/v3/credential_injector.pb.h"
#include "envoy/extensions/filters/http/credential_injector/v3/credential_injector.pb.validate.h"

#include "source/extensions/filters/http/common/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CredentialInjector {

class CredentialInjectorFilterFactory
    : public Common::UnifiedFactoryBase<
          envoy::extensions::filters::http::credential_injector::v3::CredentialInjector> {
public:
  CredentialInjectorFilterFactory()
      : UnifiedFactoryBase("envoy.filters.http.credential_injector") {}

protected:
  // stats_prefix is relative to the given scope and is used for the filter's own stats, while the
  // configured credential extension creates its stats in the server's scope and is therefore given
  // the full prefix of the filter chain in credential_stats_prefix.
  absl::StatusOr<Http::FilterFactoryCb> createFilterFactoryFromProtoHelper(
      const envoy::extensions::filters::http::credential_injector::v3::CredentialInjector& config,
      const std::string& stats_prefix, const std::string& credential_stats_prefix,
      Server::Configuration::ServerFactoryContext& context, Stats::Scope& scope,
      Init::Manager& init_manager) const;

private:
  absl::StatusOr<Http::FilterFactoryCb> createHttpFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::credential_injector::v3::CredentialInjector& config,
      Server::Configuration::ServerFactoryContext& context,
      Server::Configuration::ExtraFactoryContext& extra_context) override;
};

using UpstreamCredentialInjectorFilterFactory = CredentialInjectorFilterFactory;

} // namespace CredentialInjector
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
