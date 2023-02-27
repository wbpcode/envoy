#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "envoy/extensions/filters/http/header_mutation/v3/header_mutation.pb.h"

#include "source/common/common/logger.h"
#include "source/common/http/header_mutation.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace HeaderMutation {

using ProtoConfig = envoy::extensions::filters::http::header_mutation::v3::HeaderMutation;
using PerRouteProtoConfig =
    envoy::extensions::filters::http::header_mutation::v3::HeaderMutationPerRoute;

class PerRouteHeaderMutation : public Router::RouteSpecificFilterConfig {
public:
  PerRouteHeaderMutation(const PerRouteProtoConfig& config);

  void mutateRequestHeaders(Http::RequestHeaderMap& request_headers,
                            const StreamInfo::StreamInfo& stream_info) const;
  void mutateResponseHeaders(const Http::RequestHeaderMap& request_headers,
                             Http::ResponseHeaderMap& response_headers,
                             const StreamInfo::StreamInfo& stream_info) const;

private:
  Http::HeaderMutations request_mutations_;
  Http::HeaderMutations response_mutations_;
};
using PerRouteHeaderMutationSharedPtr = std::shared_ptr<PerRouteHeaderMutation>;

class HeaderMutation : public Http::PassThroughFilter, public Logger::Loggable<Logger::Id::filter> {
public:
  HeaderMutation() = default;

  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers, bool) override;

  // Http::StreamEncoderFilter
  Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap& headers, bool) override;

private:
  const PerRouteHeaderMutation* route_config_{};
};

} // namespace HeaderMutation
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
