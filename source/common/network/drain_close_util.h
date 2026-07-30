#pragma once

#include "envoy/common/random_generator.h"
#include "envoy/common/time.h"
#include "envoy/network/drain_decision.h"

namespace Envoy {
namespace Network {

/**
 * Computes whether a connection that has been notified of the given drain event (via
 * Network::Connection::onDrain()) should now be drain-closed. This reproduces the
 * gradual/immediate behavior of Server::DrainManagerImpl::drainClose() at the connection level,
 * without polling a DrainDecision, so callers share a single consistent drain timeline captured on
 * the main thread.
 *
 * For DrainStrategy::Immediate this always returns true. For DrainStrategy::Gradual the probability
 * of returning true ramps linearly from 0 to 1 over the drain window: P(true) = elapsed / drain_time.
 *
 * @param time_source supplies the source of the current monotonic time.
 * @param random supplies the random generator used for the gradual probability ramp.
 * @param drain_event describes the drain sequence (start time, duration and strategy).
 */
bool shouldDrainClose(TimeSource& time_source, Random::RandomGenerator& random,
                      ConnectionDrainEvent drain_event);

} // namespace Network
} // namespace Envoy
