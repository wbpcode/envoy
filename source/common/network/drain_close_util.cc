#include "source/common/network/drain_close_util.h"

#include <chrono>
#include <cstdint>

#include "source/common/common/assert.h"

namespace Envoy {
namespace Network {

bool shouldDrainClose(TimeSource& time_source, Random::RandomGenerator& random,
                      ConnectionDrainEvent drain_event) {
  // An immediate strategy drains as soon as the connection has been notified.
  if (drain_event.strategy == Server::DrainStrategy::Immediate) {
    return true;
  }
  ASSERT(drain_event.strategy == Server::DrainStrategy::Gradual);

  // Gradual strategy: P(return true) = elapsed time / drain time, matching
  // Server::DrainManagerImpl::drainClose(). The drain start time was captured once on the main
  // thread so every connection shares the same drain deadline.
  const MonotonicTime now = time_source.monotonicTime();
  // Guard against a clock reading earlier than the recorded start time (should not happen with a
  // monotonic clock, but be defensive).
  const auto elapsed =
      now <= drain_event.start_time
          ? std::chrono::seconds{0}
          : std::chrono::duration_cast<std::chrono::seconds>(now - drain_event.start_time);
  if (elapsed >= drain_event.drain_time) {
    return true;
  }
  const auto drain_time_count = drain_event.drain_time.count();
  // If no drain time is configured, drain immediately once notified.
  if (drain_time_count == 0) {
    return true;
  }
  return static_cast<uint64_t>(elapsed.count()) >
         (random.random() % static_cast<uint64_t>(drain_time_count));
}

} // namespace Network
} // namespace Envoy
