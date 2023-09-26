#pragma once

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
 * All these options are optional for the simple ping-pong use case.
 */
class StreamOptions {
public:
  StreamOptions(absl::optional<uint64_t> stream_id, bool wait_response, bool drain_close,
                bool is_heartbeat)
      : stream_id_(stream_id.value_or(0)), has_stream_id_(stream_id.has_value()),
        wait_response_(wait_response), drain_close_(drain_close), is_heartbeat_(is_heartbeat) {}
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
 * Stream frame interface. This is used to represent the stream frame of request or response.
 */
class StreamFrame {
public:
  virtual ~StreamFrame() = default;
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
 * Interface of generic request. This is used to represent the generic request. It could be treated
 * as specilization of StreamFrame that contains the request specific information.
 * NOTE: using interface that provided by the TraceContext as the interface of generic request here
 * to simplify the tracing integration. This is not a good design. This should be changed in the
 * future.
 */
class StreamRequest : public Tracing::TraceContext, public StreamFrame {
public:
  // Used for matcher.
  static constexpr absl::string_view name() { return "generic_proxy"; }

  /**
   * Get request stream options. This is used to control the behavior of the generic proxy
   * filter. Default is empty options.
   *
   * @return generic request stream options.
   */
  virtual StreamOptions streamOptions() const { return {}; }
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
 * Interface of generic response. This is used to represent the generic response. It could be
 * treated as specilization of StreamFrame that contains the response specific information.
 */
class StreamResponse : public StreamBase {
public:
  /**
   * Get response status.
   *
   * @return generic response status.
   */
  virtual Status status() const PURE;

  /**
   * Get response stream options. This is used to control the behavior of the generic proxy
   * filter. Default is empty options.
   *
   * @return generic response stream options.
   */
  virtual StreamOptions streamOptions() const { return {}; }
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
