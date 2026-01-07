#include "source/extensions/filters/http/filter_chain/filter.h"

#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "source/common/config/utility.h"
#include "source/common/http/utility.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace FilterChain {

namespace {

// Helper to process filter config and create filter factories
Http::FilterChainUtility::FilterFactoriesList createFilterFactoriesFromConfig(
    const envoy::extensions::filters::http::filter_chain::v3::FilterChain& proto_config,
    Server::Configuration::ServerFactoryContext& context, const std::string& stats_prefix) {
  Http::FilterChainUtility::FilterFactoriesList filter_factories;
  filter_factories.reserve(proto_config.filters_size());

  for (const auto& filter_config : proto_config.filters()) {
    auto& factory =
        Config::Utility::getAndCheckFactory<Server::Configuration::NamedHttpFilterConfigFactory>(
            filter_config);

    ProtobufTypes::MessagePtr message = Config::Utility::translateToFactoryConfig(
        filter_config, context.messageValidationVisitor(), factory);
    auto callback_or_error =
        factory.createFilterFactoryFromProtoWithServerContext(*message, stats_prefix, context);

    auto filter_config_provider =
        Http::FilterChainUtility::createSingletonDownstreamFilterConfigProviderManager(context)
            ->createStaticFilterConfigProvider(callback_or_error, filter_config.name());
    filter_factories.push_back({std::move(filter_config_provider), false});
  }

  return filter_factories;
}

} // namespace

FilterChainSettings::FilterChainSettings(
    const envoy::extensions::filters::http::filter_chain::v3::FilterChain& proto_config,
    Server::Configuration::ServerFactoryContext& context, const std::string& stats_prefix)
    : filter_factories_(createFilterFactoriesFromConfig(proto_config, context, stats_prefix)) {}

FilterChainPerRouteConfig::FilterChainPerRouteConfig(
    const envoy::extensions::filters::http::filter_chain::v3::FilterChainConfigPerRoute&
        proto_config,
    Server::Configuration::ServerFactoryContext& context, const std::string& stats_prefix)
    : filter_chain_name_(proto_config.filter_chain_name()) {
  if (proto_config.has_filter_chain()) {
    filter_chain_ =
        std::make_shared<FilterChainSettings>(proto_config.filter_chain(), context, stats_prefix);
  }
}

FilterChainConfig::FilterChainConfig(
    const envoy::extensions::filters::http::filter_chain::v3::FilterChainConfig& proto_config,
    Server::Configuration::ServerFactoryContext& context, const std::string& stats_prefix) {
  if (proto_config.has_filter_chain()) {
    default_filter_chain_ =
        std::make_shared<FilterChainSettings>(proto_config.filter_chain(), context, stats_prefix);
  }

  for (const auto& [name, chain] : proto_config.filter_chains()) {
    named_filter_chains_[name] =
        std::make_shared<FilterChainSettings>(chain, context, stats_prefix);
  }
}

// DelegatedFilterChain implementation

Http::FilterHeadersStatus DelegatedFilterChain::decodeHeaders(Http::RequestHeaderMap& headers,
                                                              bool end_stream) {
  for (auto& filter : filters_) {
    auto status = filter->decodeHeaders(headers, end_stream);
    if (status != Http::FilterHeadersStatus::Continue) {
      return status;
    }
  }
  return Http::FilterHeadersStatus::Continue;
}

Http::FilterDataStatus DelegatedFilterChain::decodeData(Buffer::Instance& data, bool end_stream) {
  for (auto& filter : filters_) {
    auto status = filter->decodeData(data, end_stream);
    if (status != Http::FilterDataStatus::Continue) {
      return status;
    }
  }
  return Http::FilterDataStatus::Continue;
}

Http::FilterTrailersStatus DelegatedFilterChain::decodeTrailers(Http::RequestTrailerMap& trailers) {
  for (auto& filter : filters_) {
    auto status = filter->decodeTrailers(trailers);
    if (status != Http::FilterTrailersStatus::Continue) {
      return status;
    }
  }
  return Http::FilterTrailersStatus::Continue;
}

Http::FilterMetadataStatus DelegatedFilterChain::decodeMetadata(Http::MetadataMap& metadata_map) {
  for (auto& filter : filters_) {
    auto status = filter->decodeMetadata(metadata_map);
    if (status != Http::FilterMetadataStatus::Continue) {
      return status;
    }
  }
  return Http::FilterMetadataStatus::Continue;
}

void DelegatedFilterChain::setDecoderFilterCallbacks(
    Http::StreamDecoderFilterCallbacks& callbacks) {
  for (auto& filter : filters_) {
    filter->setDecoderFilterCallbacks(callbacks);
  }
}

void DelegatedFilterChain::decodeComplete() {
  for (auto& filter : filters_) {
    filter->decodeComplete();
  }
}

Http::Filter1xxHeadersStatus
DelegatedFilterChain::encode1xxHeaders(Http::ResponseHeaderMap& headers) {
  // Encoding happens in reverse order
  for (auto it = filters_.rbegin(); it != filters_.rend(); ++it) {
    auto status = (*it)->encode1xxHeaders(headers);
    if (status != Http::Filter1xxHeadersStatus::Continue) {
      return status;
    }
  }
  return Http::Filter1xxHeadersStatus::Continue;
}

Http::FilterHeadersStatus DelegatedFilterChain::encodeHeaders(Http::ResponseHeaderMap& headers,
                                                              bool end_stream) {
  // Encoding happens in reverse order
  for (auto it = filters_.rbegin(); it != filters_.rend(); ++it) {
    auto status = (*it)->encodeHeaders(headers, end_stream);
    if (status != Http::FilterHeadersStatus::Continue) {
      return status;
    }
  }
  return Http::FilterHeadersStatus::Continue;
}

Http::FilterDataStatus DelegatedFilterChain::encodeData(Buffer::Instance& data, bool end_stream) {
  // Encoding happens in reverse order
  for (auto it = filters_.rbegin(); it != filters_.rend(); ++it) {
    auto status = (*it)->encodeData(data, end_stream);
    if (status != Http::FilterDataStatus::Continue) {
      return status;
    }
  }
  return Http::FilterDataStatus::Continue;
}

Http::FilterTrailersStatus
DelegatedFilterChain::encodeTrailers(Http::ResponseTrailerMap& trailers) {
  // Encoding happens in reverse order
  for (auto it = filters_.rbegin(); it != filters_.rend(); ++it) {
    auto status = (*it)->encodeTrailers(trailers);
    if (status != Http::FilterTrailersStatus::Continue) {
      return status;
    }
  }
  return Http::FilterTrailersStatus::Continue;
}

Http::FilterMetadataStatus DelegatedFilterChain::encodeMetadata(Http::MetadataMap& metadata_map) {
  // Encoding happens in reverse order
  for (auto it = filters_.rbegin(); it != filters_.rend(); ++it) {
    auto status = (*it)->encodeMetadata(metadata_map);
    if (status != Http::FilterMetadataStatus::Continue) {
      return status;
    }
  }
  return Http::FilterMetadataStatus::Continue;
}

void DelegatedFilterChain::setEncoderFilterCallbacks(
    Http::StreamEncoderFilterCallbacks& callbacks) {
  for (auto& filter : filters_) {
    filter->setEncoderFilterCallbacks(callbacks);
  }
}

void DelegatedFilterChain::encodeComplete() {
  // Encoding happens in reverse order
  for (auto it = filters_.rbegin(); it != filters_.rend(); ++it) {
    (*it)->encodeComplete();
  }
}

void DelegatedFilterChain::onDestroy() {
  for (auto& filter : filters_) {
    filter->onDestroy();
  }
}

void DelegatedFilterChain::onStreamComplete() {
  for (auto& filter : filters_) {
    filter->onStreamComplete();
  }
}

// Filter implementation

Filter::Filter(FilterChainConfigSharedPtr config) : config_(std::move(config)) {}

FilterChainSettingsConstSharedPtr Filter::resolveFilterChain() {
  // Check if there's a per-route configuration
  const auto* per_route_config =
      Http::Utility::resolveMostSpecificPerFilterConfig<FilterChainPerRouteConfig>(
          decoder_callbacks_);

  if (per_route_config != nullptr) {
    // If the per-route config has a direct filter chain, use it
    if (per_route_config->filterChain() != nullptr) {
      ENVOY_LOG(debug, "filter_chain: using per-route filter chain");
      return per_route_config->filterChain();
    }

    // If the per-route config has a filter chain name, look it up
    if (!per_route_config->filterChainName().empty()) {
      auto named_chain = config_->findFilterChainByName(per_route_config->filterChainName());
      if (named_chain != nullptr) {
        ENVOY_LOG(debug, "filter_chain: using named filter chain '{}'",
                  per_route_config->filterChainName());
        return named_chain;
      }
      ENVOY_LOG(debug, "filter_chain: named filter chain '{}' not found, using default",
                per_route_config->filterChainName());
    }
  }

  // Fall back to the default filter chain
  ENVOY_LOG(debug, "filter_chain: using default filter chain");
  return config_->defaultFilterChain();
}

Http::FilterHeadersStatus Filter::decodeHeaders(Http::RequestHeaderMap& headers, bool end_stream) {
  // Resolve which filter chain to use
  auto filter_chain_settings = resolveFilterChain();

  if (filter_chain_settings == nullptr || filter_chain_settings->filterFactories().empty()) {
    // No filter chain configured, pass through
    return Http::FilterHeadersStatus::Continue;
  }

  // Create filter instances from the filter factories using FactoryCallbacksWrapper pattern
  struct FactoryCallbacksWrapper : public Http::FilterChainFactoryCallbacks {
    void addStreamDecoderFilter(Http::StreamDecoderFilterSharedPtr filter) override {
      auto wrapper = std::make_shared<StreamFilterWrapper>(std::move(filter));
      filters.push_back(wrapper);
    }
    void addStreamEncoderFilter(Http::StreamEncoderFilterSharedPtr filter) override {
      auto wrapper = std::make_shared<StreamFilterWrapper>(std::move(filter));
      filters.push_back(wrapper);
    }
    void addStreamFilter(Http::StreamFilterSharedPtr filter) override { filters.push_back(filter); }
    void addAccessLogHandler(AccessLog::InstanceSharedPtr) override {}
    Event::Dispatcher& dispatcher() override { return dispatcher_; }

    // Wraps a stream encoder OR decoder filter into a stream filter
    struct StreamFilterWrapper : public Http::StreamFilter {
      explicit StreamFilterWrapper(Http::StreamEncoderFilterSharedPtr encoder_filter)
          : encoder_filter_(std::move(encoder_filter)) {}
      explicit StreamFilterWrapper(Http::StreamDecoderFilterSharedPtr decoder_filter)
          : decoder_filter_(std::move(decoder_filter)) {}

      // Http::StreamDecoderFilter
      Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers,
                                              bool end_stream) override {
        if (decoder_filter_) {
          return decoder_filter_->decodeHeaders(headers, end_stream);
        }
        return Http::FilterHeadersStatus::Continue;
      }
      Http::FilterDataStatus decodeData(Buffer::Instance& data, bool end_stream) override {
        if (decoder_filter_) {
          return decoder_filter_->decodeData(data, end_stream);
        }
        return Http::FilterDataStatus::Continue;
      }
      Http::FilterTrailersStatus decodeTrailers(Http::RequestTrailerMap& trailers) override {
        if (decoder_filter_) {
          return decoder_filter_->decodeTrailers(trailers);
        }
        return Http::FilterTrailersStatus::Continue;
      }
      Http::FilterMetadataStatus decodeMetadata(Http::MetadataMap& metadata_map) override {
        if (decoder_filter_) {
          return decoder_filter_->decodeMetadata(metadata_map);
        }
        return Http::FilterMetadataStatus::Continue;
      }
      void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) override {
        if (decoder_filter_) {
          decoder_filter_->setDecoderFilterCallbacks(callbacks);
        }
      }
      void decodeComplete() override {
        if (decoder_filter_) {
          decoder_filter_->decodeComplete();
        }
      }

      // Http::StreamEncoderFilter
      Http::Filter1xxHeadersStatus encode1xxHeaders(Http::ResponseHeaderMap& headers) override {
        if (encoder_filter_) {
          return encoder_filter_->encode1xxHeaders(headers);
        }
        return Http::Filter1xxHeadersStatus::Continue;
      }
      Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap& headers,
                                              bool end_stream) override {
        if (encoder_filter_) {
          return encoder_filter_->encodeHeaders(headers, end_stream);
        }
        return Http::FilterHeadersStatus::Continue;
      }
      Http::FilterDataStatus encodeData(Buffer::Instance& data, bool end_stream) override {
        if (encoder_filter_) {
          return encoder_filter_->encodeData(data, end_stream);
        }
        return Http::FilterDataStatus::Continue;
      }
      Http::FilterTrailersStatus encodeTrailers(Http::ResponseTrailerMap& trailers) override {
        if (encoder_filter_) {
          return encoder_filter_->encodeTrailers(trailers);
        }
        return Http::FilterTrailersStatus::Continue;
      }
      Http::FilterMetadataStatus encodeMetadata(Http::MetadataMap& metadata_map) override {
        if (encoder_filter_) {
          return encoder_filter_->encodeMetadata(metadata_map);
        }
        return Http::FilterMetadataStatus::Continue;
      }
      void setEncoderFilterCallbacks(Http::StreamEncoderFilterCallbacks& callbacks) override {
        if (encoder_filter_) {
          encoder_filter_->setEncoderFilterCallbacks(callbacks);
        }
      }
      void encodeComplete() override {
        if (encoder_filter_) {
          encoder_filter_->encodeComplete();
        }
      }

      // Http::StreamFilterBase
      void onDestroy() override {
        if (decoder_filter_) {
          decoder_filter_->onDestroy();
        } else if (encoder_filter_) {
          encoder_filter_->onDestroy();
        }
      }
      void onStreamComplete() override {
        if (decoder_filter_) {
          decoder_filter_->onStreamComplete();
        } else if (encoder_filter_) {
          encoder_filter_->onStreamComplete();
        }
      }

      Http::StreamEncoderFilterSharedPtr encoder_filter_;
      Http::StreamDecoderFilterSharedPtr decoder_filter_;
    };

    FactoryCallbacksWrapper(Event::Dispatcher& d) : dispatcher_(d) {}
    Event::Dispatcher& dispatcher_;
    std::vector<Http::StreamFilterSharedPtr> filters;
  };

  FactoryCallbacksWrapper callbacks(decoder_callbacks_->dispatcher());

  // Create the filters from the factories
  Http::FilterChainUtility::createFilterChainForFactories(
      callbacks, Http::EmptyFilterChainOptions{}, filter_chain_settings->filterFactories());

  if (callbacks.filters.empty()) {
    return Http::FilterHeadersStatus::Continue;
  }

  // Create the delegated filter chain
  delegated_filter_ = std::make_shared<DelegatedFilterChain>(std::move(callbacks.filters));
  delegated_filter_->setDecoderFilterCallbacks(*decoder_callbacks_);
  delegated_filter_->setEncoderFilterCallbacks(*encoder_callbacks_);

  return delegated_filter_->decodeHeaders(headers, end_stream);
}

Http::FilterDataStatus Filter::decodeData(Buffer::Instance& data, bool end_stream) {
  if (delegated_filter_) {
    return delegated_filter_->decodeData(data, end_stream);
  }
  return Http::FilterDataStatus::Continue;
}

Http::FilterTrailersStatus Filter::decodeTrailers(Http::RequestTrailerMap& trailers) {
  if (delegated_filter_) {
    return delegated_filter_->decodeTrailers(trailers);
  }
  return Http::FilterTrailersStatus::Continue;
}

Http::FilterMetadataStatus Filter::decodeMetadata(Http::MetadataMap& metadata_map) {
  if (delegated_filter_) {
    return delegated_filter_->decodeMetadata(metadata_map);
  }
  return Http::FilterMetadataStatus::Continue;
}

void Filter::decodeComplete() {
  if (delegated_filter_) {
    delegated_filter_->decodeComplete();
  }
}

Http::Filter1xxHeadersStatus Filter::encode1xxHeaders(Http::ResponseHeaderMap& headers) {
  if (delegated_filter_) {
    return delegated_filter_->encode1xxHeaders(headers);
  }
  return Http::Filter1xxHeadersStatus::Continue;
}

Http::FilterHeadersStatus Filter::encodeHeaders(Http::ResponseHeaderMap& headers, bool end_stream) {
  if (delegated_filter_) {
    return delegated_filter_->encodeHeaders(headers, end_stream);
  }
  return Http::FilterHeadersStatus::Continue;
}

Http::FilterDataStatus Filter::encodeData(Buffer::Instance& data, bool end_stream) {
  if (delegated_filter_) {
    return delegated_filter_->encodeData(data, end_stream);
  }
  return Http::FilterDataStatus::Continue;
}

Http::FilterTrailersStatus Filter::encodeTrailers(Http::ResponseTrailerMap& trailers) {
  if (delegated_filter_) {
    return delegated_filter_->encodeTrailers(trailers);
  }
  return Http::FilterTrailersStatus::Continue;
}

Http::FilterMetadataStatus Filter::encodeMetadata(Http::MetadataMap& metadata_map) {
  if (delegated_filter_) {
    return delegated_filter_->encodeMetadata(metadata_map);
  }
  return Http::FilterMetadataStatus::Continue;
}

void Filter::encodeComplete() {
  if (delegated_filter_) {
    delegated_filter_->encodeComplete();
  }
}

void Filter::onDestroy() {
  if (delegated_filter_) {
    delegated_filter_->onDestroy();
  }
}

void Filter::onStreamComplete() {
  if (delegated_filter_) {
    delegated_filter_->onStreamComplete();
  }
}

} // namespace FilterChain
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
