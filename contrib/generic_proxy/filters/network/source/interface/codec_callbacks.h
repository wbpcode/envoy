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
 * Callbacks for downstream request decoding and upstream response encoding.
 */
class ServerCodecCallbacks {
public:
  virtual ~ServerCodecCallbacks() = default;
  /**
   * If request decoding success then this method will be called.
   * @param frame request frame from decoding. Frist frame should be StreamRequest
   * frame.
   */
  virtual void onDecodingSuccess(StreamFramePtr frame) PURE;

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
 * Callbacks for downstream request encoding and upstream response decoding.
 */
class ClientCodecCallbacks {
public:
  virtual ~ClientCodecCallbacks() = default;

  /**
   * If response decoding success then this method will be called.
   * @param frame response frame from decoding. Frist frame should be StreamResponse
   * frame.
   */
  virtual void onDecodingSuccess(StreamFramePtr frame) PURE;

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

/**
 * Encoding callbacks of request frame.
 */
class ClientEncodingCallbacks {
public:
  virtual ~ClientEncodingCallbacks() = default;

  /**
   * If request frame encoding success then this method will be called.
   * @param end_stream true if the frame is the last frame.
   */
  virtual void onEncodingSuccess(bool end_stream) PURE;
};

/**
 * Encoding callbacks of response frame.
 */
class ServerEncodingCallbacks {
public:
  virtual ~ServerEncodingCallbacks() = default;

  /**
   * If response frame encoding success then this method will be called.
   * @param end_stream true if the frame is the last frame.q
   */
  virtual void onEncodingSuccess(bool end_stream) PURE;
};

} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
