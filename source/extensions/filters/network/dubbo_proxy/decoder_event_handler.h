#pragma once

#include "envoy/common/pure.h"
#include "envoy/network/filter.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/extensions/common/dubbo/message.h"
#include "source/extensions/common/dubbo/metadata.h"
#include "source/extensions/common/dubbo/codec.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {

using MessageMetadata = Envoy::Extensions::Common::Dubbo::MessageMetadata;
using MessageMetadataSharedPtr = Envoy::Extensions::Common::Dubbo::MessageMetadataSharedPtr;
using Protocol = Envoy::Extensions::Common::Dubbo::DubboCodec;
using ProtocolPtr = std::unique_ptr<Protocol>;
using CommonDecodeStatus = Envoy::Extensions::Common::Dubbo::DecodeStatus;
using MessageType = Envoy::Extensions::Common::Dubbo::MessageType;
using ResponseStatus = Envoy::Extensions::Common::Dubbo::ResponseStatus;
using ProtocolType = Envoy::Extensions::Common::Dubbo::ProtocolType;
using SerializationType = Envoy::Extensions::Common::Dubbo::SerializeType;
using Serializer = Envoy::Extensions::Common::Dubbo::Serializer;
using SerializerPtr = Envoy::Extensions::Common::Dubbo::SerializerPtr;
using Utility = Envoy::Extensions::Common::Dubbo::Utility;
using DirectResponseUtil = Envoy::Extensions::Common::Dubbo::DirectResponseUtil;

enum class FilterStatus : uint8_t {
  // Continue filter chain iteration.
  Continue,
  // Pause iterating to any of the remaining filters in the chain.
  // The current message remains in the connection manager to wait to be continued.
  // ContinueDecoding()/continueEncoding() MUST be called to continue filter iteration.
  StopIteration,
  // Abort the iteration and remove the current message from the connection manager.
  AbortIteration,
};

class StreamDecoder {
public:
  virtual ~StreamDecoder() = default;

  /**
   * Indicates that the message had been decoded.
   * @param metadata MessageMetadataSharedPtr describing the message
   * @return FilterStatus to indicate if filter chain iteration should continue
   */
  virtual FilterStatus onMessageDecoded(MessageMetadataSharedPtr metadata) PURE;
};

using StreamDecoderSharedPtr = std::shared_ptr<StreamDecoder>;

class StreamEncoder {
public:
  virtual ~StreamEncoder() = default;

  /**
   * Indicates that the message had been encoded.
   * @param metadata MessageMetadataSharedPtr describing the message
   * @return FilterStatus to indicate if filter chain iteration should continue
   */
  virtual FilterStatus onMessageEncoded(MessageMetadataSharedPtr metadata) PURE;
};

using StreamEncoderSharedPtr = std::shared_ptr<StreamEncoder>;

class StreamHandler {
public:
  virtual ~StreamHandler() = default;

  /**
   * Indicates that the message had been decoded.
   * @param metadata MessageMetadataSharedPtr describing the message
   * @return FilterStatus to indicate if filter chain iteration should continue
   */
  virtual void onStreamDecoded(MessageMetadataSharedPtr metadata) PURE;
};

using StreamDecoderSharedPtr = std::shared_ptr<StreamDecoder>;

class DecoderCallbacksBase {
public:
  virtual ~DecoderCallbacksBase() = default;

  /**
   * @return StreamDecoder* a new StreamDecoder for a message.
   */
  virtual StreamHandler& newStream() PURE;

  /**
   * Indicates that the message is a heartbeat.
   */
  virtual void onHeartbeat(MessageMetadataSharedPtr) PURE;
};

class RequestDecoderCallbacks : public DecoderCallbacksBase {};
class ResponseDecoderCallbacks : public DecoderCallbacksBase {};

} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
