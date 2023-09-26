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
class Request : public Tracing::TraceContext, public StreamFrame {
public:
  // Used for matcher.
  static constexpr absl::string_view name() { return "generic_proxy"; }
};

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
class Response : public StreamBase {
public:
  /**
   * Get response status.
   *
   * @return generic response status.
   */
  virtual Status status() const PURE;
};

using ResponsePtr = std::unique_ptr<Response>;
using ResponseSharedPtr = std::shared_ptr<Response>;

} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
