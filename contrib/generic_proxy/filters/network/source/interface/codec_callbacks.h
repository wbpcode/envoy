#pragma once

#include "envoy/buffer/buffer.h"
#include "envoy/common/optref.h"
#include "envoy/network/connection.h"
#include "envoy/network/drain_decision.h"

#include "contrib/generic_proxy/filters/network/source/interface/stream.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace GenericProxy {

/**
 * Extended options from request or response to control the behavior of the
 * generic proxy filter.
 * All these options are optional for the simple ping-pong use case.
 */
class ExtendedOptions {
public:
  ExtendedOptions(absl::optional<uint64_t> stream_id, bool wait_response, bool drain_close,
                  bool is_heartbeat)
      : stream_id_(stream_id.value_or(0)), has_stream_id_(stream_id.has_value()),
        wait_response_(wait_response), drain_close_(drain_close), is_heartbeat_(is_heartbeat) {}
  ExtendedOptions() = default;

  /**
   * @return the stream id of the request or response. This is used to match the
   * downstream request with the upstream response.

   * NOTE: In most cases, the stream id is not needed and will be ignored completely.
   * The stream id is only used when we can't match the downstream request
   * with the upstream response by the active stream instance self directly.
   * For example, when the multiple downstream requests are multiplexed into one
   * upstream connection.
   */
  absl::optional<uint64_t> streamId() const {
    return has_stream_id_ ? absl::optional<uint64_t>(stream_id_) : absl::nullopt;
  }

  /**
   * @return whether the current request requires an upstream response.
   * NOTE: This is only used for the request.
   */
  bool waitResponse() const { return wait_response_; }

  /**
   * @return whether the downstream/upstream connection should be drained after
   * current active requests are finished.
   * NOTE: This is only used for the response.
   */
  bool drainClose() const { return drain_close_; }

  /**
   * @return whether the current request/response is a heartbeat request/response.
   * NOTE: It would be better to handle heartbeat request/response by another L4
   * filter. Then the generic proxy filter can be used for the simple ping-pong
   * use case.
   */
  bool isHeartbeat() const { return is_heartbeat_; }

private:
  uint64_t stream_id_{0};
  bool has_stream_id_{false};

  bool wait_response_{true};
  bool drain_close_{false};
  bool is_heartbeat_{false};
};

/**
 * Interface of request stream handler. This is used to handle the request stream and possible
 * multiple request frames.
 */
class RequestStreamHandler {
  virtual ~RequestStreamHandler() = default;

  /**
   * Called only once for same request stream when the request stream is started.
   */
  virtual void onRequestStart(RequestPtr request, bool end_stream) PURE;

  /**
   * Called when a request frame is received. This could be called zero or multiple times for same
   * request stream.
   */
  virtual void onRequestFrame(StreamFramePtr stream_frame, bool end_stream) PURE;
};

/**
 * Interface of response stream handler. This is used to handle the response stream and possible
 * multiple response frames.
 */
class ResponseStreamHandler {
  virtual ~ResponseStreamHandler() = default;

  /**
   * Called only once for same response stream when the response stream is started.
   */
  virtual void onResponseStart(ResponsePtr response, bool end_stream) PURE;

  /**
   * Called when a response frame is received. This could be called zero or multiple times for same
   * response stream.
   */
  virtual void onResponseFrame(StreamFramePtr stream_frame, bool end_stream) PURE;
};

/**
 * Callback of downstream request decoder and upstream response encoder.
 */
class ServerCodecCallbacks {
public:
  virtual ~ServerCodecCallbacks() = default;

  /**
   * If request decoding success then this method will be called.
   * @param options extended options from request.
   * @return the request stream handler to handle the current request stream.
   */
  virtual RequestStreamHandler* onDecodingSuccess(ExtendedOptions opts) PURE;

  /**
   * If request decoding failure then this method will be called.
   */
  virtual void onDecodingFailure() PURE;

  /**
   * Write specified data to the downstream connection. This is could be used to write
   * some raw binary to peer before the onDecodingSuccess()/onDecodingFailure() is
   * called. By this way, when some special data is received from peer, the custom
   * codec could handle it directly and write some reply to peer without notifying
   * the generic proxy filter.
   * @param buffer data to write.
   */
  virtual void writeToConnection(Buffer::Instance& buffer) PURE;

  /**
   * @return the downstream connection that the request is received from. This gives
   * the custom codec the full power to control the downstream connection.
   */
  virtual OptRef<Network::Connection> connection() PURE;
};

/**
 * Callback of upstream response decoder and downstream request encoder.
 */
class ClientCodecCallbacks {
public:
  virtual ~ClientCodecCallbacks() = default;

  /**
   * If response decoding success then this method will be called.
   * @param options extended options from response.
   * @return the response stream handler to handle the current response stream.
   */
  virtual ResponseStreamHandler* onDecodingSuccess(ExtendedOptions opts) PURE;

  /**
   * If response decoding failure then this method will be called.
   */
  virtual void onDecodingFailure() PURE;

  /**
   * Write specified data to the upstream connection. This is could be used to write
   * some raw binary to peer before the onDecodingSuccess()/onDecodingFailure() is
   * called. By this way, when some special data is received from peer, the custom
   * codec could handle it directly and write some reply to peer without notifying
   * the generic proxy filter.
   * @param buffer data to write.
   */
  virtual void writeToConnection(Buffer::Instance& buffer) PURE;

  /**
   * @return the upstream connection that the response is received from. This gives
   * the custom codec the full power to control the upstream connection.
   */
  virtual OptRef<Network::Connection> connection() PURE;
};

} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
