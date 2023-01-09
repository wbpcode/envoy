#pragma once

#include "contrib/generic_proxy/filters/network/source/interface/codec.h"
#include "contrib/generic_proxy/filters/network/source/interface/filter.h"
#include "contrib/generic_proxy/filters/network/source/interface/route.h"

#include "source/common/http/conn_manager_config.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace GenericProxy {

using TracingConnectionManagerConfig = Http::TracingConnectionManagerConfig;
using TracingConnectionManagerConfigPtr = std::unique_ptr<TracingConnectionManagerConfig>;

/**
 * Filter config interface for generic proxy read filter.
 */
class FilterConfig : public FilterChainFactory {
public:
  /**
   * Get route entry by generic request.
   * @param request request.
   * @return RouteEntryConstSharedPtr route entry.
   */
  virtual RouteEntryConstSharedPtr routeEntry(const Request& request) const PURE;

  /**
   * Get codec factory for  decoding/encoding of request/response.
   * @return CodecFactory codec factory.
   */
  virtual const CodecFactory& codecFactory() const PURE;

  /**
   * @return const Network::DrainDecision& a drain decision that filters can use to
   * determine if they should be doing graceful closes on connections when possible.
   */
  virtual const Network::DrainDecision& drainDecision() const PURE;

  /**
   *  @return Tracing::Driver tracing driver to use.
   */
  virtual OptRef<Tracing::Driver> tracingProvider() const PURE;

  /**
   * @return tracing config.
   */
  virtual OptRef<const TracingConnectionManagerConfig> tracingConfig() const PURE;
};

} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
