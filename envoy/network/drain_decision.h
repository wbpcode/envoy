#pragma once

#include <chrono>
#include <functional>

#include "envoy/common/callback.h"
#include "envoy/common/pure.h"
#include "envoy/common/time.h"
#include "envoy/server/drain_strategy.h"

#include "absl/base/attributes.h"
#include "absl/status/status.h"

namespace Envoy {
namespace Network {

enum class DrainDirection {
  /**
   * Not draining yet. Default value, should not be externally set.
   */
  None = 0,

  /**
   * Drain inbound connections only.
   */
  InboundOnly,

  /**
   * Drain both inbound and outbound connections.
   */
  All,
};

/**
 * Describes a drain sequence that a connection has been notified of via
 * Network::Connection::onDrain(). The values are captured once on the main thread at the moment
 * the drain sequence is initiated and are propagated unchanged to
 * every affected connection so that all connections share a single, consistent view of the drain
 * timeline regardless of when each connection is notified. Callbacks can use these values to
 * reproduce the same gradual/immediate drain behavior that Server::DrainManagerImpl::drainClose()
 * would apply, without polling a DrainDecision.
 */
struct ConnectionDrainEvent {
  // The monotonic time at which the drain sequence was initiated on the main thread.
  MonotonicTime start_time;
  // The total configured drain duration (Server::Options::drainTime()).
  std::chrono::seconds drain_time;
  // The configured drain strategy (Server::Options::drainStrategy()): gradual ramp-up vs.
  // immediate.
  Server::DrainStrategy strategy;
};

class DrainDecision {
public:
  using DrainCloseCb = std::function<absl::Status(std::chrono::milliseconds)>;

  virtual ~DrainDecision() = default;

  /**
   * @return TRUE if a connection should be drained and closed. It is up to individual network
   *         filters to determine when this should be called for the least impact possible.
   * @param direction supplies the direction for which the caller is checking drain close.
   */
  virtual bool drainClose(DrainDirection scope) const PURE;

  /**
   * @brief Register a callback to be called proactively when a drain decision enters into a
   *        'close' state.
   *        NOTE: this API is used in proprietary builds of Envoy and can not be decommissioned.
   *        TODO(yanavlasov): cleanup unused parts of this change without removing this API.
   *
   * @param cb Callback to be called once drain decision enters close state
   * @return handle to remove callback
   */
  ABSL_MUST_USE_RESULT
  virtual Common::CallbackHandlePtr addOnDrainCloseCb(DrainDirection scope,
                                                      DrainCloseCb cb) const PURE;
};

} // namespace Network
} // namespace Envoy
