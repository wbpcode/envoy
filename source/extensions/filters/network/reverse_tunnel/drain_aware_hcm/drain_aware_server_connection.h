#pragma once

#include <functional>
#include <memory>
#include <utility>

#include "envoy/common/random_generator.h"
#include "envoy/event/dispatcher.h"
#include "envoy/http/codec.h"
#include "envoy/network/connection.h"
#include "envoy/network/drain_decision.h"

#include "source/common/common/logger.h"
#include "source/common/network/drain_close_util.h"
#include "source/common/runtime/runtime_features.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ReverseTunnel {

// Interposes between the HTTP/2 server codec and the HCM's ServerConnectionCallbacks to observe a
// peer GOAWAY, which the default server path ignores (ConnectionManagerImpl::onGoAway is a no-op
// for servers). For reverse tunnels we treat it as "this tunnel is going away" and dial a
// replacement immediately, while the old tunnel serves in-flight streams until the peer's final
// GOAWAY closes it. All other callbacks are forwarded unchanged.
class DrainAwareServerConnectionCallbacks : public Http::ServerConnectionCallbacks,
                                            public Logger::Loggable<Logger::Id::filter> {
public:
  DrainAwareServerConnectionCallbacks(Http::ServerConnectionCallbacks& inner,
                                      std::function<void()> on_peer_goaway)
      : inner_(inner), on_peer_goaway_(std::move(on_peer_goaway)) {}

  // Http::ServerConnectionCallbacks
  Http::RequestDecoder& newStream(Http::ResponseEncoder& response_encoder,
                                  bool is_internally_created = false) override {
    return inner_.newStream(response_encoder, is_internally_created);
  }

  // Http::ConnectionCallbacks
  void onGoAway(Http::GoAwayErrorCode error_code) override {
    // Envoy's codec only delivers the first GOAWAY, but guard anyway so a re-dial fires at most
    // once per tunnel from this path.
    if (!peer_goaway_handled_ && on_peer_goaway_ != nullptr) {
      peer_goaway_handled_ = true;
      ENVOY_LOG(info,
                "drain_aware_hcm: peer GOAWAY for connection (code={}); draining tunnel and "
                "dialing replacement",
                static_cast<int>(error_code));
      on_peer_goaway_();
    }
    inner_.onGoAway(error_code);
  }
  void onSettings(Http::ReceivedSettings& settings) override { inner_.onSettings(settings); }
  void onMaxStreamsChanged(uint32_t num_streams) override {
    inner_.onMaxStreamsChanged(num_streams);
  }

private:
  Http::ServerConnectionCallbacks& inner_;
  std::function<void()> on_peer_goaway_;
  bool peer_goaway_handled_{false};
};

// Wraps an Http::ServerConnection and proactively sends an HTTP/2 GOAWAY frame when the listener
// that owns this connection begins draining. Drain is detected on a short timer. When the runtime
// feature "envoy.reloadable_features.use_connection_level_drain" is enabled, the drain decision is
// derived from the connection-level drain event delivered via Network::Connection::onDrain();
// otherwise it falls back to polling DrainDecision::drainClose() (which avoids calling
// addOnDrainCloseCb(), intentionally unsupported on PerFilterChainFactoryContextImpl).
class DrainAwareServerConnection : public Http::ServerConnection,
                                   public Logger::Loggable<Logger::Id::filter> {
public:
  // `on_local_drain` (optional) fires once when this connection begins draining locally (the HCM
  // sends a shutdownNotice due to max_connection_duration/graceful shutdown, or the listener
  // drains). For reverse tunnels this asks the initiator to dial a replacement tunnel immediately
  // while the old one finishes in-flight streams.
  DrainAwareServerConnection(
      Http::ServerConnectionPtr inner, Network::Connection& connection,
      const Network::DrainDecision& drain_decision, Random::RandomGenerator& random,
      std::function<void()> on_local_drain = nullptr,
      std::unique_ptr<DrainAwareServerConnectionCallbacks> callbacks_wrapper = nullptr)
      : callbacks_wrapper_(std::move(callbacks_wrapper)), inner_(std::move(inner)),
        connection_(connection), drain_decision_(drain_decision), random_(random),
        on_local_drain_(std::move(on_local_drain)) {
    ENVOY_LOG(debug, "drain_aware_hcm: created server connection wrapper, protocol={}",
              static_cast<int>(inner_->protocol()));
    // Observe connection-level drain notifications so onDrainCheckTimer() can react to them.
    connection_.addConnectionCallbacks(drain_callbacks_);
    drain_check_timer_ = connection_.dispatcher().createTimer([this]() { onDrainCheckTimer(); });
    drain_check_timer_->enableTimer(std::chrono::milliseconds(100));
  }

  ~DrainAwareServerConnection() override {
    connection_.removeConnectionCallbacks(drain_callbacks_);
    if (drain_check_timer_ != nullptr) {
      drain_check_timer_->disableTimer();
    }
  }

  Http::Status dispatch(Buffer::Instance& data) override { return inner_->dispatch(data); }
  void goAway() override { inner_->goAway(); }
  Http::Protocol protocol() override { return inner_->protocol(); }
  void shutdownNotice() override {
    // The HCM calls this at the start of a graceful drain (e.g. max_connection_duration). For
    // reverse tunnels (on_local_drain_ set) we use it as the "tunnel draining" signal to dial a
    // replacement now, but SUPPRESS the early GOAWAY so the peer keeps using this tunnel during the
    // grace window. The HCM's final GOAWAY at drain_timeout (via goAway()) then migrates new
    // requests to the established replacement while in-flight requests finish here.
    if (on_local_drain_ != nullptr) {
      notifyLocalDrain();
      return;
    }
    inner_->shutdownNotice();
  }
  bool wantsToWrite() override { return inner_->wantsToWrite(); }

  void onUnderlyingConnectionAboveWriteBufferHighWatermark() override {
    inner_->onUnderlyingConnectionAboveWriteBufferHighWatermark();
  }

  void onUnderlyingConnectionBelowWriteBufferLowWatermark() override {
    inner_->onUnderlyingConnectionBelowWriteBufferLowWatermark();
  }

private:
  // Observes connection-level drain notifications and records the drain event for the timer to act
  // on. All other connection callbacks are no-ops here (the HCM handles them via its own callbacks).
  struct DrainCallbacks : public Network::ConnectionCallbacks {
    DrainCallbacks(DrainAwareServerConnection& parent) : parent_(parent) {}
    void onEvent(Network::ConnectionEvent) override {}
    void onAboveWriteBufferHighWatermark() override {}
    void onBelowWriteBufferLowWatermark() override {}
    void onDrain(Network::ConnectionDrainEvent event) override {
      parent_.connection_drain_event_ = event;
    }
    DrainAwareServerConnection& parent_;
  };

  // Returns true if the connection should begin draining (send GOAWAY). When connection-level drain
  // is enabled this is derived from the drain event delivered via onDrain(); otherwise it polls the
  // DrainDecision.
  bool drainDetected() {
    if (Runtime::runtimeFeatureEnabled("envoy.reloadable_features.use_connection_level_drain")) {
      return connection_drain_event_.has_value() &&
             Network::shouldDrainClose(connection_.dispatcher().timeSource(), random_,
                                       *connection_drain_event_);
    }
    return drain_decision_.drainClose(Network::DrainDirection::All);
  }

  void onDrainCheckTimer() {
    if (drain_goaway_sent_) {
      return;
    }
    if (drainDetected()) {
      ENVOY_LOG(info, "drain_aware_hcm: drain detected, sending GOAWAY");
      drain_goaway_sent_ = true;
      notifyLocalDrain();
      inner_->goAway();
      return;
    }
    drain_check_timer_->enableTimer(std::chrono::milliseconds(100));
  }

  // Fires the local-drain callback at most once.
  void notifyLocalDrain() {
    if (local_drain_notified_ || on_local_drain_ == nullptr) {
      return;
    }
    local_drain_notified_ = true;
    on_local_drain_();
  }

  // Declared before inner_ so the codec (which holds a reference to this wrapper) is destroyed
  // before the wrapper. Null when the peer-GOAWAY re-dial path is disabled.
  std::unique_ptr<DrainAwareServerConnectionCallbacks> callbacks_wrapper_;
  Http::ServerConnectionPtr inner_;
  Network::Connection& connection_;
  const Network::DrainDecision& drain_decision_;
  Random::RandomGenerator& random_;
  DrainCallbacks drain_callbacks_{*this};
  // Set when the connection is notified of a drain sequence via onDrain().
  absl::optional<Network::ConnectionDrainEvent> connection_drain_event_;
  std::function<void()> on_local_drain_;
  Event::TimerPtr drain_check_timer_;
  bool drain_goaway_sent_{false};
  bool local_drain_notified_{false};
};

} // namespace ReverseTunnel
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
