#pragma once

#include "envoy/network/connection_handler.h"
#include "envoy/network/listener.h"
#include "envoy/stats/scope.h"

#include "source/common/stats/prefix_utility.h"
#include "source/common/stats/utility.h"
#include "source/server/listener_stats.h"

#include "absl/strings/strip.h"

namespace Envoy {
namespace Server {

/**
 * Wrapper for an active listener owned by this handler.
 */
class ActiveListenerImplBase : public virtual Network::ConnectionHandler::ActiveListener {
public:
  ActiveListenerImplBase(Network::ConnectionHandler& parent, Network::ListenerConfig* config)
      : stats_({ALL_LISTENER_STATS(POOL_COUNTER(config->listenerScope()),
                                   POOL_GAUGE(config->listenerScope()),
                                   POOL_HISTOGRAM(config->listenerScope()))}),
        // listener.<address>.(worker_<id>.)*: the handler's stat prefix is its dispatcher's name
        // (with a trailing dot), and a worker's name contributes the envoy.worker_id tag.
        per_worker_prefix_(Stats::workerStatPrefix(config->listenerScope().symbolTable(), "",
                                                   parent.statPrefix())),
        per_worker_stats_({ALL_PER_HANDLER_LISTENER_STATS(
            POOL_COUNTER_TAGGED(config->listenerScope(), per_worker_prefix_),
            POOL_GAUGE_TAGGED(config->listenerScope(), per_worker_prefix_))}),
        config_(config) {}

  // Network::ConnectionHandler::ActiveListener.
  uint64_t listenerTag() override { return config_->listenerTag(); }

  ListenerStats stats_;
  const Stats::TaggedStatName per_worker_prefix_;
  PerHandlerListenerStats per_worker_stats_;
  Network::ListenerConfig* config_{};
};

} // namespace Server
} // namespace Envoy
