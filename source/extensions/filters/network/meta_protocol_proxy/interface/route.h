#pragma once

#include <bits/stdint-uintn.h>

#include <chrono>

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/typed_metadata.h"

#include "source/extensions/filters/network/meta_protocol_proxy/interface/stream.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MetaProtocolProxy {

class RouteSpecificFilterConfig {
public:
  virtual ~RouteSpecificFilterConfig() = default;
};
using RouteSpecificFilterConfigConstSharedPtr = std::shared_ptr<const RouteSpecificFilterConfig>;

class RouteTypedMetadataFactory : public Envoy::Config::TypedMetadataFactory {};

class RetryPolicy {
public:
  virtual ~RetryPolicy() = default;

  /**
   * When upstream returns a response or when a specific event occurs, whether it should retry.
   *
   * @param count The number of requests that have been made upstream.
   * @param response The optional upstream response.
   * @param event The optional upstream request or connection event.
   * @return bool should kick off a new retry request or not.
   */
  virtual bool shouldRetry(uint32_t count, const Response* response,
                           absl::optional<Event> event = absl::nullopt) const PURE;

  /**
   * @return std::chrono::milliseconds per upstream request timeout.
   */
  virtual std::chrono::milliseconds timeout() const PURE;
};

class RouteEntry {
public:
  virtual ~RouteEntry() = default;

  virtual const std::string& clusterName() const PURE;

  /**
   * Get route level per filter config by the filter name.
   */
  virtual const RouteSpecificFilterConfig* perFilterConfig(absl::string_view) const PURE;
  template <class T> const T* typedPerFilterConfig(absl::string_view name) const {
    return dynamic_cast<const T*>(perFilterConfig(name));
  }

  /**
   * Update request before encode and send request to upstream.
   */
  virtual void finalizeRequest(Request& request) const PURE;

  /**
   * Update response before encode and send response to downstream.
   */
  virtual void finalizeResponse(Response& response) const PURE;

  /**
   * @return const Envoy::Config::TypedMetadata& return the typed metadata provided in the config
   * for this route.
   */
  virtual const Envoy::Config::TypedMetadata& typedMetadata() const PURE;

  /**
   * @return const envoy::config::core::v3::Metadata& return the metadata provided in the config for
   * this route.
   */
  virtual const envoy::config::core::v3::Metadata& metadata() const PURE;

  /**
   * @return std::chrono::milliseconds the route's timeout.
   */
  virtual std::chrono::milliseconds timeout() const PURE;

  /**
   * const RetryPolicy& the retry policy for the route. All routes have a retry policy even if it is
   * empty and does not allow retries.
   */
  virtual const RetryPolicy& retryPolicy() const PURE;
};
using RouteEntryConstSharedPtr = std::shared_ptr<const RouteEntry>;

class RouteMatcher {
public:
  virtual ~RouteMatcher() = default;

  virtual RouteEntryConstSharedPtr routeEntry(const Request& request) const PURE;
};
using RouteMatcherPtr = std::unique_ptr<RouteMatcher>;

} // namespace MetaProtocolProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
