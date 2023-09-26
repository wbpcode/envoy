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
 * Interface of request stream handler. This is used to handle the request stream and possible
 * multiple request frames.
 */
class StreamHandler {
  virtual ~StreamHandler() = default;

  /**
   * Called when a request/response frame is received. This could be called zero or multiple times
   * for same request/response stream.
   */
  virtual void onStreamFrame(StreamFramePtr stream_frame, bool end_stream) PURE;
};

/**
 * Callback of downstream request decoder and upstream response encoder.
 */
class ServerCodecCallbacks {
public:
  virtual ~ServerCodecCallbacks() = default;

  /**
   * If request decoding success then this method will be called.
   * @param request the decoded request.
   * @param end_stream whether the request is ended.
   * @return the request stream handler to handle following stream frames (if exist)
   * of the request.
   */
  virtual StreamHandler* onDecodingSuccess(RequestPtr request, bool end_stream) PURE;

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
   * @param response the decoded response.
   * @param end_stream whether the response is ended.
   * @return the response stream handler to handle following stream frames (if exist)
   * of the response.
   */
  virtual StreamHandler* onDecodingSuccess(ResponsePtr response, bool end_stream) PURE;

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
