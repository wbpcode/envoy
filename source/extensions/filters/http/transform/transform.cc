#include "source/extensions/filters/http/transform/transform.h"

#include <cstdint>
#include <memory>

#include "source/common/config/utility.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/http/utility.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Transform {

PerRouteTransform::PerRouteTransform(const PerRouteProtoConfig& config,
                                     absl::Status& creation_status) {}

TransformConfig::TransformConfig(const ProtoConfig& config, absl::Status& creation_status) {}

void Transform::maybeInitializeRouteConfigs(Http::StreamFilterCallbacks* callbacks) {
  // Ensure that route configs are initialized only once and the same route configs are used
  // for both decoding and encoding paths.
  // An independent flag is used to ensure even at the case where the route configs is empty,
  // we still won't try to initialize it again.
  if (route_configs_initialized_) {
    return;
  }
  route_configs_initialized_ = true;

  // Traverse through all route configs to retrieve all available header mutations.
  // `getAllPerFilterConfig` returns in ascending order of specificity (i.e., route table
  // first, then virtual host, then per route).
  route_config_ = Http::Utility::resolveMostSpecificPerFilterConfig<PerRouteTransform>(callbacks);
}

Http::FilterHeadersStatus Transform::decodeHeaders(Http::RequestHeaderMap& headers, bool) {
  return Http::FilterHeadersStatus::Continue;
}

Http::FilterHeadersStatus Transform::encodeHeaders(Http::ResponseHeaderMap& headers, bool) {
  return Http::FilterHeadersStatus::Continue;
}

Http::FilterTrailersStatus Transform::encodeTrailers(Http::ResponseTrailerMap& trailers) {
  return Http::FilterTrailersStatus::Continue;
}

Http::FilterTrailersStatus Transform::decodeTrailers(Http::RequestTrailerMap& trailers) {
  return Http::FilterTrailersStatus::Continue;
}

} // namespace Transform
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
