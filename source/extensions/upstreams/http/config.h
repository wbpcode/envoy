#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <list>
#include <map>
#include <string>

#include "envoy/config/core/v3/extension.pb.h"
#include "envoy/config/core/v3/protocol.pb.h"
#include "envoy/extensions/upstreams/http/v3/http_protocol_options.pb.h"
#include "envoy/extensions/upstreams/http/v3/http_protocol_options.pb.validate.h"
#include "envoy/extensions/filters/http/upstream_codec/v3/upstream_codec.pb.h"

#include "envoy/http/filter.h"
#include "envoy/http/header_validator.h"
#include "envoy/server/filter_config.h"
#include "envoy/server/transport_socket_config.h"

#include "source/common/common/logger.h"
#include "source/common/http/filter_chain_helper.h"
#include "source/common/protobuf/message_validator_impl.h"
#include "source/common/upstream/upstream_http_factory_context_impl.h"

namespace Envoy {
namespace Extensions {
namespace Upstreams {
namespace Http {

class ProtocolOptionsConfigImpl : public Upstream::HttpProtocolOptionsConfig {
public:
  ProtocolOptionsConfigImpl(
      const envoy::config::cluster::v3::Cluster& cluster_config,
      const envoy::extensions::upstreams::http::v3::HttpProtocolOptions& options,
      ProtobufMessage::ValidationVisitor& validation_visitor,
      Server::Configuration::ServerFactoryContext& server_context, Init::Manager& init_manager,
      Stats::Scope& scope);
  // Constructor for legacy (deprecated) config.
  ProtocolOptionsConfigImpl(
      const envoy::config::cluster::v3::Cluster& cluster_config,
      const envoy::config::core::v3::Http1ProtocolOptions& http1_settings,
      const envoy::config::core::v3::Http2ProtocolOptions& http2_options,
      const envoy::config::core::v3::HttpProtocolOptions& common_options,
      const absl::optional<envoy::config::core::v3::UpstreamHttpProtocolOptions> upstream_options,
      bool use_downstream_protocol, bool use_http2,
      ProtobufMessage::ValidationVisitor& validation_visitor,
      Server::Configuration::ServerFactoryContext& server_context, Init::Manager& init_manager,
      Stats::Scope& scope);

  // Given the supplied cluster config, and protocol options configuration,
  // returns a unit64_t representing the enabled Upstream::ClusterInfo::Features.
  static uint64_t parseFeatures(const envoy::config::cluster::v3::Cluster& config,
                                const ProtocolOptionsConfigImpl& options);

  const Envoy::Http::Http1Settings& http1Settings() const override { return http1_settings_; }
  const envoy::config::core::v3::Http2ProtocolOptions& http2Options() const override {
    return http2_options_;
  }
  const envoy::config::core::v3::Http3ProtocolOptions& http3Options() const override {
    return http3_options_;
  }
  const envoy::config::core::v3::HttpProtocolOptions& commonHttpProtocolOptions() const override {
    return common_http_protocol_options_;
  }
  const absl::optional<envoy::config::core::v3::UpstreamHttpProtocolOptions>&
  upstreamHttpProtocolOptions() const override {
    return upstream_http_protocol_options_;
  }
  const absl::optional<const envoy::config::core::v3::AlternateProtocolsCacheOptions>&
  alternateProtocolsCacheOptions() const override {
    return alternate_protocol_cache_options_;
  }
  const absl::optional<std::chrono::milliseconds> idleTimeout() const override {
    return idle_timeout_;
  }
  uint64_t maxRequestsPerConnection() const override { return max_requests_per_connection_; }
  uint32_t maxResponseHeadersCount() const override { return max_response_headers_count_; }
  bool hasConfiguredHttpFilters() const override { return has_configured_http_filters_; }

  // Http::FilterChainFactory
  bool createFilterChain(Envoy::Http::FilterChainManager& manager,
                         bool only_create_if_configured) const override {
    if (!has_configured_http_filters_ && only_create_if_configured) {
      return false;
    }
    Envoy::Http::FilterChainUtility::createFilterChainForFactories(manager, http_filter_factories_);
    return true;
  }
  bool createUpgradeFilterChain(absl::string_view, const UpgradeMap*,
                                Envoy::Http::FilterChainManager&) const override {
    // Upgrade filter chains not yet supported for upstream filters.
    return false;
  }

  const Envoy::Http::HeaderValidatorFactoryPtr& headerValidatorFactory() const {
    return header_validator_factory_;
  }

private:
  using FiltersList = Protobuf::RepeatedPtrField<
      envoy::extensions::filters::network::http_connection_manager::v3::HttpFilter>;

  void initialize(const envoy::config::cluster::v3::Cluster& cluster_config,
                  FiltersList http_filters);

  const Envoy::Http::Http1Settings http1_settings_;
  const envoy::config::core::v3::Http2ProtocolOptions http2_options_;
  const envoy::config::core::v3::Http3ProtocolOptions http3_options_;
  const envoy::config::core::v3::HttpProtocolOptions common_http_protocol_options_;
  const absl::optional<envoy::config::core::v3::UpstreamHttpProtocolOptions>
      upstream_http_protocol_options_;
  const absl::optional<const envoy::config::core::v3::AlternateProtocolsCacheOptions>
      alternate_protocol_cache_options_;
  const Envoy::Http::HeaderValidatorFactoryPtr header_validator_factory_;

  Envoy::Upstream::UpstreamHttpFactoryContextImpl upstream_context_;
  Envoy::Http::FilterChainUtility::FilterFactoriesList http_filter_factories_;

  absl::optional<std::chrono::milliseconds> idle_timeout_;

  const uint64_t max_requests_per_connection_{};
  const uint32_t max_response_headers_count_{};

  const bool use_downstream_protocol_{};
  const bool use_http2_{};
  const bool use_http3_{};
  const bool use_alpn_{};

  // true iff the cluster proto specified upstream http filters.
  bool has_configured_http_filters_{};
};

class ProtocolOptionsConfigFactory : public Server::Configuration::ProtocolOptionsFactory {
public:
  Upstream::ProtocolOptionsConfigConstSharedPtr createProtocolOptionsConfig(
      const Protobuf::Message& config,
      Server::Configuration::ProtocolOptionsFactoryContext& context) override {
    const auto& typed_config = MessageUtil::downcastAndValidate<
        const envoy::extensions::upstreams::http::v3::HttpProtocolOptions&>(
        config, context.messageValidationVisitor());
    return std::make_shared<ProtocolOptionsConfigImpl>(typed_config,
                                                       context.messageValidationVisitor());
  }
  std::string category() const override { return "envoy.upstream_options"; }
  std::string name() const override {
    return "envoy.extensions.upstreams.http.v3.HttpProtocolOptions";
  }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<envoy::extensions::upstreams::http::v3::HttpProtocolOptions>();
  }
  ProtobufTypes::MessagePtr createEmptyProtocolOptionsProto() override {
    return std::make_unique<envoy::extensions::upstreams::http::v3::HttpProtocolOptions>();
  }
};

DECLARE_FACTORY(ProtocolOptionsConfigFactory);

} // namespace Http
} // namespace Upstreams
} // namespace Extensions
} // namespace Envoy
