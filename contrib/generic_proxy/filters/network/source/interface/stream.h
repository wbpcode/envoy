#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "envoy/common/pure.h"
#include "envoy/tracing/trace_context.h"

#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace GenericProxy {

/**
 * Stream options from request or response to control the behavior of the
 * generic proxy filter.
 * All these options could be ignored for the simple ping-pong use case.
 */
class StreamOptions {
public:
  StreamOptions(uint64_t stream_id, bool one_way_stream, bool drain_close, bool is_heartbeat)
      : stream_id_(stream_id), one_way_stream_(one_way_stream), drain_close_(drain_close),
        is_heartbeat_(is_heartbeat) {}
  StreamOptions() = default;

  /**
   * @return the stream id of the request or response. This is used to match the
   * downstream request with the upstream response.

   * NOTE: In most cases, the stream id is not needed and will be ignored completely.
   * The stream id is only used when we can't match the downstream request
   * with the upstream response by the active stream instance self directly.
   * For example, when the multiple downstream requests are multiplexed into one
   * upstream connection.
   */
  uint64_t streamId() const { return stream_id_; }

  /**
   * @return whether the stream is one way stream. If request is one way stream, the
   * generic proxy filter will not wait for the response from the upstream.
   */
  bool oneWayStream() const { return one_way_stream_; }

  /**
   * @return whether the downstream/upstream connection should be drained after
   * current active stream are finished.
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

  bool one_way_stream_{false};
  bool drain_close_{false};
  bool is_heartbeat_{false};
};

/**
 * Frame options from request or response to control the behavior of the
 * generic proxy filter.
 * All these options could be ignored for the simple ping-pong use case.
 */
class FrameOptions {
public:
  FrameOptions(bool end_stream) : end_stream_(end_stream) {}
  FrameOptions() = default;

  /**
   * @return whether the frame is the last frame of the stream.
   */
  bool endStream() const { return end_stream_; }

private:
  // Default be true for backward compatibility. In most cases, only
  // one frame is used for request or response the simple ping-pong use case.
  const bool end_stream_{true};
};

/**
 * Stream frame interface. This is used to represent the stream frame of request or response.
 */
class StreamFrame {
public:
  virtual ~StreamFrame() = default;

  /**
   * Get options of stream that the frame belongs to. The options MUST be same for all frames
   * of the same stream. Copy semantics is used because the options are lightweight.
   * @return StreamOptions of the stream.
   */
  virtual StreamOptions streamOptions() { return {}; }

  /**
   * Get options of frame. The options are used to control the behavior of the generic proxy filter.
   * @return FrameOptions of the frame.
   */
  virtual FrameOptions frameOptions() { return {}; }
};

using StreamFramePtr = std::unique_ptr<StreamFrame>;
using StreamFrameSharedPtr = std::shared_ptr<StreamFrame>;

class StreamBase : public StreamFrame {
public:
  using IterateCallback = std::function<bool(absl::string_view key, absl::string_view val)>;

  /**
   * Get application protocol of generic stream.
   *
   * @return A string view representing the application protocol of the generic stream behind
   * the context.
   */
  virtual absl::string_view protocol() const PURE;

  /**
   * Iterate over all generic stream metadata entries.
   *
   * @param callback supplies the iteration callback.
   */
  virtual void forEach(IterateCallback callback) const PURE;

  /**
   * Get generic stream metadata value by key.
   *
   * @param key The metadata key of string view type.
   * @return The optional metadata value of string_view type.
   */
  virtual absl::optional<absl::string_view> getByKey(absl::string_view key) const PURE;

  /**
   * Set new generic stream metadata key/value pair.
   *
   * @param key The metadata key of string view type.
   * @param val The metadata value of string view type.
   */
  virtual void setByKey(absl::string_view key, absl::string_view val) PURE;

  /**
   * Set new generic stream metadata key/value pair. The key MUST point to data that will live
   * beyond the lifetime of any generic stream that using the string.
   *
   * @param key The metadata key of string view type.
   * @param val The metadata value of string view type.
   */
  virtual void setByReferenceKey(absl::string_view key, absl::string_view val) PURE;

  /**
   * Set new generic stream metadata key/value pair. Both key and val MUST point to data that
   * will live beyond the lifetime of any generic stream that using the string.
   *
   * @param key The metadata key of string view type.
   * @param val The metadata value of string view type.
   */
  virtual void setByReference(absl::string_view key, absl::string_view val) PURE;

  // Used for matcher.
  static constexpr absl::string_view name() { return "generic_proxy"; }
};

/**
 * Interface of generic request. This is derived from StreamFrame that contains the request
 * specific information. First frame of the request MUST be a StreamRequest.
 *
 * NOTE: using interface that provided by the TraceContext as the interface of generic request here
 * to simplify the tracing integration. This is not a good design. This should be changed in the
 * future.
 */
class StreamRequest : public Tracing::TraceContext, public StreamFrame {
public:
  // Used for matcher.
  static constexpr absl::string_view name() { return "generic_proxy"; }
};

using StreamRequestPtr = std::unique_ptr<StreamRequest>;
using StreamRequestSharedPtr = std::shared_ptr<StreamRequest>;
// Alias for backward compatibility.
using Request = StreamRequest;
using RequestPtr = std::unique_ptr<Request>;
using RequestSharedPtr = std::shared_ptr<Request>;

enum class Event {
  Timeout,
  ConnectionTimeout,
  ConnectionClosed,
  LocalConnectionClosed,
  ConnectionFailure,
};

using Status = absl::Status;
using StatusCode = absl::StatusCode;

/**
 * Interface of generic response. This is derived from StreamFrame that contains the response
 * specific information. First frame of the response MUST be a StreamResponse.
 */
class StreamResponse : public StreamBase {
public:
  /**
   * Get response status.
   *
   * @return generic response status.
   */
  virtual Status status() const PURE;
};

using StreamResponsePtr = std::unique_ptr<StreamResponse>;
using StreamResponseSharedPtr = std::shared_ptr<StreamResponse>;
// Alias for backward compatibility.
using Response = StreamResponse;
using ResponsePtr = std::unique_ptr<Response>;
using ResponseSharedPtr = std::shared_ptr<Response>;

} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
