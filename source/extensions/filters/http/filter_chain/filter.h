#pragma once

#include <memory>
#include <vector>

#include "envoy/extensions/filters/http/filter_chain/v3/filter_chain.pb.h"
#include "envoy/http/filter.h"

#include "source/common/http/filter_chain_helper.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace FilterChain {

class FilterChainSettings;
using FilterChainSettingsConstSharedPtr = std::shared_ptr<const FilterChainSettings>;

/**
 * Configuration for a single filter chain.
 */
class FilterChainSettings : public Router::RouteSpecificFilterConfig {
public:
  FilterChainSettings(
      const envoy::extensions::filters::http::filter_chain::v3::FilterChain& proto_config,
      Server::Configuration::ServerFactoryContext& context, const std::string& stats_prefix);

  const Http::FilterChainUtility::FilterFactoriesList& filterFactories() const {
    return filter_factories_;
  }

private:
  Http::FilterChainUtility::FilterFactoriesList filter_factories_;
};

/**
 * Per-route configuration for the filter chain filter.
 */
class FilterChainPerRouteConfig : public Router::RouteSpecificFilterConfig {
public:
  FilterChainPerRouteConfig(
      const envoy::extensions::filters::http::filter_chain::v3::FilterChainConfigPerRoute&
          proto_config,
      Server::Configuration::ServerFactoryContext& context, const std::string& stats_prefix);

  FilterChainSettingsConstSharedPtr filterChain() const { return filter_chain_; }
  const std::string& filterChainName() const { return filter_chain_name_; }

private:
  FilterChainSettingsConstSharedPtr filter_chain_;
  std::string filter_chain_name_;
};

using FilterChainPerRouteConfigConstSharedPtr = std::shared_ptr<const FilterChainPerRouteConfig>;

/**
 * Filter-level configuration for the filter chain filter.
 */
class FilterChainConfig {
public:
  FilterChainConfig(
      const envoy::extensions::filters::http::filter_chain::v3::FilterChainConfig& proto_config,
      Server::Configuration::ServerFactoryContext& context, const std::string& stats_prefix);

  FilterChainSettingsConstSharedPtr defaultFilterChain() const { return default_filter_chain_; }
  FilterChainSettingsConstSharedPtr findFilterChainByName(const std::string& name) const {
    auto it = named_filter_chains_.find(name);
    if (it != named_filter_chains_.end()) {
      return it->second;
    }
    return nullptr;
  }

private:
  FilterChainSettingsConstSharedPtr default_filter_chain_;
  absl::flat_hash_map<std::string, FilterChainSettingsConstSharedPtr> named_filter_chains_;
};

using FilterChainConfigSharedPtr = std::shared_ptr<FilterChainConfig>;

/**
 * A wrapper class that chains multiple filters together and delegates calls to each in sequence.
 * For decoding, filters are called in order from first to last.
 * For encoding, filters are called in reverse order from last to first.
 */
class DelegatedFilterChain : public Http::StreamFilter {
public:
  explicit DelegatedFilterChain(std::vector<Http::StreamFilterSharedPtr> filters)
      : filters_(std::move(filters)) {}

  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers,
                                          bool end_stream) override;
  Http::FilterDataStatus decodeData(Buffer::Instance& data, bool end_stream) override;
  Http::FilterTrailersStatus decodeTrailers(Http::RequestTrailerMap& trailers) override;
  Http::FilterMetadataStatus decodeMetadata(Http::MetadataMap& metadata_map) override;
  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) override;
  void decodeComplete() override;

  // Http::StreamEncoderFilter
  Http::Filter1xxHeadersStatus encode1xxHeaders(Http::ResponseHeaderMap& headers) override;
  Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap& headers,
                                          bool end_stream) override;
  Http::FilterDataStatus encodeData(Buffer::Instance& data, bool end_stream) override;
  Http::FilterTrailersStatus encodeTrailers(Http::ResponseTrailerMap& trailers) override;
  Http::FilterMetadataStatus encodeMetadata(Http::MetadataMap& metadata_map) override;
  void setEncoderFilterCallbacks(Http::StreamEncoderFilterCallbacks& callbacks) override;
  void encodeComplete() override;

  // Http::StreamFilterBase
  void onDestroy() override;
  void onStreamComplete() override;

private:
  std::vector<Http::StreamFilterSharedPtr> filters_;
};

/**
 * The filter chain filter. This filter acts as a wrapper that applies
 * a configurable chain of HTTP filters to incoming requests.
 */
class Filter : public Http::StreamFilter, Logger::Loggable<Logger::Id::filter> {
public:
  Filter(FilterChainConfigSharedPtr config);

  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers,
                                          bool end_stream) override;
  Http::FilterDataStatus decodeData(Buffer::Instance& data, bool end_stream) override;
  Http::FilterTrailersStatus decodeTrailers(Http::RequestTrailerMap& trailers) override;
  Http::FilterMetadataStatus decodeMetadata(Http::MetadataMap& metadata_map) override;
  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) override {
    decoder_callbacks_ = &callbacks;
  }
  void decodeComplete() override;

  // Http::StreamEncoderFilter
  Http::Filter1xxHeadersStatus encode1xxHeaders(Http::ResponseHeaderMap& headers) override;
  Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap& headers,
                                          bool end_stream) override;
  Http::FilterDataStatus encodeData(Buffer::Instance& data, bool end_stream) override;
  Http::FilterTrailersStatus encodeTrailers(Http::ResponseTrailerMap& trailers) override;
  Http::FilterMetadataStatus encodeMetadata(Http::MetadataMap& metadata_map) override;
  void setEncoderFilterCallbacks(Http::StreamEncoderFilterCallbacks& callbacks) override {
    encoder_callbacks_ = &callbacks;
  }
  void encodeComplete() override;

  // Http::StreamFilterBase
  void onDestroy() override;
  void onStreamComplete() override;

private:
  FilterChainSettingsConstSharedPtr resolveFilterChain();

  FilterChainConfigSharedPtr config_;
  Http::StreamFilterSharedPtr delegated_filter_;
  Http::StreamEncoderFilterCallbacks* encoder_callbacks_{};
  Http::StreamDecoderFilterCallbacks* decoder_callbacks_{};
};

} // namespace FilterChain
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
