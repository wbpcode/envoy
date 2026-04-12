#include "source/extensions/filters/http/geoip/geoip_filter.h"

#include "envoy/extensions/filters/http/geoip/v3/geoip.pb.h"

#include "source/common/http/utility.h"
#include "source/common/network/utility.h"
#include "source/common/runtime/runtime_features.h"
#include "source/common/stats/tag_utility.h"

#include "absl/memory/memory.h"
#include "absl/strings/match.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Geoip {

Stats::ScopeSharedPtr createGeoipScope(Stats::Scope& scope, absl::string_view parent_stat_prefix) {
  if (Runtime::runtimeFeatureEnabled("envoy.reloadable_features.no_stats_tag_extraction")) {
    Stats::StatElementViewVec elements;
    if (!parent_stat_prefix.empty()) {
      Stats::TagUtility::populateParentStatPrefix(parent_stat_prefix, elements);
    }
    elements.push_back(Stats::StatElementView{.value_ = "geoip"});
    return scope.createScope(elements);
  }
  return scope.createScope(absl::StrCat(parent_stat_prefix, "geoip."));
}

GeoipFilterConfig::GeoipFilterConfig(
    const envoy::extensions::filters::http::geoip::v3::Geoip& config,
    const std::string& stat_prefix, Stats::Scope& scope)
    : scope_(createGeoipScope(scope, stat_prefix)),
      stat_name_set_(scope.symbolTable().makeSet("Geoip")), parent_stat_prefix_(stat_prefix),
      geoip_(stat_name_set_->add("geoip")),
      stats_prefix_(stat_name_set_->add(stat_prefix + "geoip")), use_xff_(config.has_xff_config()),
      xff_num_trusted_hops_(config.has_xff_config() ? config.xff_config().xff_num_trusted_hops()
                                                    : 0),
      ip_address_header_(config.has_custom_header_config()
                             ? absl::make_optional<Http::LowerCaseString>(
                                   config.custom_header_config().header_name())
                             : absl::nullopt) {
  stat_name_set_->rememberBuiltin("total");
}

void GeoipFilterConfig::incCounter(Stats::StatName name) {
  scope_->counterFromStatName(name).inc();
}

GeoipFilter::GeoipFilter(GeoipFilterConfigSharedPtr config, Geolocation::DriverSharedPtr driver)
    : config_(config), driver_(std::move(driver)) {}

GeoipFilter::~GeoipFilter() = default;

void GeoipFilter::onDestroy() {}

Http::FilterHeadersStatus GeoipFilter::decodeHeaders(Http::RequestHeaderMap& headers, bool) {
  // Save request headers for later header manipulation once geolocation lookups are complete.
  request_headers_ = headers;

  Network::Address::InstanceConstSharedPtr remote_address;
  const auto& ip_address_header = config_->ipAddressHeader();
  if (ip_address_header.has_value()) {
    // Extract IP address from the configured custom header.
    const auto header_value = headers.get(ip_address_header.value());
    if (!header_value.empty()) {
      const std::string ip_string(header_value[0]->value().getStringView());
      remote_address = Network::Utility::parseInternetAddressNoThrow(ip_string);
      if (remote_address == nullptr) {
        ENVOY_LOG(debug, "Geoip filter: failed to parse IP address from header '{}': '{}'",
                  ip_address_header->get(), ip_string);
      }
    } else {
      ENVOY_LOG(debug, "Geoip filter: configured header '{}' is missing from request",
                ip_address_header->get());
    }
  } else if (config_->useXff() && config_->xffNumTrustedHops() > 0) {
    remote_address =
        Envoy::Http::Utility::getLastAddressFromXFF(headers, config_->xffNumTrustedHops()).address_;
  }
  // Fallback to the downstream connection source address if no other address source is configured
  // or if extraction from the configured source failed.
  if (!remote_address) {
    remote_address = decoder_callbacks_->streamInfo().downstreamAddressProvider().remoteAddress();
  }

  ASSERT(driver_, "No driver is available to perform geolocation lookup");

  // Capturing weak_ptr to GeoipFilter so that filter can be safely accessed in the posted callback.
  // This is a safe measure to protect against the case when filter gets deleted before the callback
  // is run.
  GeoipFilterWeakPtr self = weak_from_this();
  driver_->lookup(
      Geolocation::LookupRequest{std::move(remote_address)},
      [self, &dispatcher = decoder_callbacks_->dispatcher()](Geolocation::LookupResult&& result) {
        dispatcher.post([self, result]() {
          if (GeoipFilterSharedPtr filter = self.lock()) {
            filter->onLookupComplete(std::move(result));
          }
        });
      });

  // Stop the iteration for headers and data (POST request) for the current filter and the filters
  // following.
  return Http::FilterHeadersStatus::StopAllIterationAndWatermark;
}

Http::FilterDataStatus GeoipFilter::decodeData(Buffer::Instance&, bool) {
  return Http::FilterDataStatus::Continue;
}

Http::FilterTrailersStatus GeoipFilter::decodeTrailers(Http::RequestTrailerMap&) {
  return Http::FilterTrailersStatus::Continue;
}

void GeoipFilter::setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) {
  decoder_callbacks_ = &callbacks;
}

void GeoipFilter::onLookupComplete(Geolocation::LookupResult&& result) {
  ASSERT(request_headers_);
  for (auto it = result.cbegin(); it != result.cend();) {
    const auto& geo_header = it->first;
    const auto& lookup_result = it++->second;
    if (!lookup_result.empty()) {
      request_headers_->setCopy(Http::LowerCaseString(geo_header), lookup_result);
    }
  }
  config_->incTotal();
  ENVOY_LOG(debug, "Geoip filter: finished decoding geolocation headers");
  decoder_callbacks_->continueDecoding();
}

} // namespace Geoip
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
