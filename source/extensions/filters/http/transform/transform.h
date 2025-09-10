#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "envoy/extensions/filters/http/transform/v3/transform.pb.h"
#include "envoy/http/query_params.h"

#include "source/common/common/logger.h"
#include "source/common/formatter/substitution_formatter.h"
#include "source/common/http/transform.h"
#include "source/common/http/utility.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Transform {

using ProtoConfig = envoy::extensions::filters::http::transform::v3::TransformConfig;
using PerRouteProtoConfig = envoy::extensions::filters::http::transform::v3::TransformConfig;

class PerRouteTransform : public Router::RouteSpecificFilterConfig {
public:
  PerRouteTransform(const PerRouteProtoConfig& config, absl::Status& creation_status);
};
using PerRouteTransformSharedPtr = std::shared_ptr<PerRouteTransform>;

class TransformConfig {
public:
  TransformConfig(const ProtoConfig& config, absl::Status& creation_status);
};
using TransformConfigSharedPtr = std::shared_ptr<TransformConfig>;

class Transform : public Http::PassThroughFilter, public Logger::Loggable<Logger::Id::filter> {
public:
  Transform(TransformConfigSharedPtr config) : config_(std::move(config)) {}

  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers, bool) override;

  // Http::StreamEncoderFilter
  Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap& headers, bool) override;

  // Http::StreamEncoderFilter
  Http::FilterTrailersStatus encodeTrailers(Http::ResponseTrailerMap& trailers) override;

  // Http::StreamEncoderFilter
  Http::FilterTrailersStatus decodeTrailers(Http::RequestTrailerMap& trailers) override;

private:
  void maybeInitializeRouteConfigs(Http::StreamFilterCallbacks* callbacks);

  TransformConfigSharedPtr config_;
  const PerRouteTransform* route_config_ = nullptr;
  bool route_configs_initialized_ = false;
};

} // namespace Transform
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
