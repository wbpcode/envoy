#include "codec.h"
#include "message.h"
#include "metadata.h"
#include "source/extensions/filters/network/dubbo_proxy/dubbo_protocol_impl.h"

#include "envoy/registry/registry.h"

#include "source/common/common/assert.h"
#include "source/extensions/common/dubbo/message_impl.h"
#include <memory>

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {
namespace {

constexpr uint16_t MagicNumber = 0xdabb;
constexpr uint8_t MessageTypeMask = 0x80;
constexpr uint8_t EventMask = 0x20;
constexpr uint8_t TwoWayMask = 0x40;
constexpr uint8_t SerializeTypeMask = 0x1f;
constexpr uint64_t FlagOffset = 2;
constexpr uint64_t StatusOffset = 3;
constexpr uint64_t RequestIDOffset = 4;
constexpr uint64_t BodySizeOffset = 12;

void encodeHeader(Buffer::Instance& buffer, MessageContext& context) {
  buffer.writeBEInt<uint16_t>(MagicNumber);
  uint8_t flag = static_cast<uint8_t>(context.serializeType());

  switch (context.messageType()) {
  case MessageType::Response:
    // Normal response
    break;
  case MessageType::Request:
    // Normal request.
    flag ^= MessageTypeMask;
    flag ^= TwoWayMask;
    break;
  case MessageType::Oneway:
    // Oneway request.
    flag ^= MessageTypeMask;
  case MessageType::HeartbeatRequest:
    // Event request.
    flag ^= MessageTypeMask;
    flag ^= EventMask;
  case

  }

  flag = flag ^ EventMask;
  buffer.writeByte(flag);
  buffer.writeByte(static_cast<uint8_t>(context.responseStatus().value()));
  buffer.writeBEInt<uint64_t>(context.requestId());
  // Body of heart beat response is null.
  // TODO(wbpcode): Currently we only support the Hessian2 serialization scheme, so here we
  // directly use the 'N' for null object in Hessian2. This coupling should be unnecessary.
  buffer.writeBEInt<uint32_t>(1u);
  buffer.writeByte('N');
}

} // namespace

// Consistent with the SerializeType
bool isValidSerializeType(SerializeType type) {
  switch (type) {
  case SerializeType::Hessian2:
    break;
  default:
    return false;
  }
  return true;
}

// Consistent with the ResponseStatus
bool isValidResponseStatus(ResponseStatus status) {
  switch (status) {
  case ResponseStatus::Ok:
  case ResponseStatus::ClientTimeout:
  case ResponseStatus::ServerTimeout:
  case ResponseStatus::BadRequest:
  case ResponseStatus::BadResponse:
  case ResponseStatus::ServiceNotFound:
  case ResponseStatus::ServiceError:
  case ResponseStatus::ClientError:
  case ResponseStatus::ServerThreadpoolExhaustedError:
    break;
  default:
    return false;
  }
  return true;
}

void parseRequestInfoFromBuffer(Buffer::Instance& data, MessageContext& context) {
  ASSERT(data.length() >= DubboCodec::HeadersSize);

  uint8_t flag = data.peekInt<uint8_t>(FlagOffset);
  bool is_two_way = (flag & TwoWayMask) == TwoWayMask ? true : false;
  SerializeType type = static_cast<SerializeType>(flag & SerializeTypeMask);
  if (!isValidSerializeType(type)) {
    throw EnvoyException(
        absl::StrCat("invalid dubbo message serialization type ",
                     static_cast<std::underlying_type<SerializeType>::type>(type)));
  }

  // Request without two flag should be one way request.
  if (!is_two_way && context.messageType() != MessageType::HeartbeatRequest) {
    context.setMessageType(MessageType::Oneway);
  }

  context.setTwoWayFlag(is_two_way);
  context.setSerializeType(type);
}

void parseResponseInfoFromBuffer(Buffer::Instance& buffer, MessageContext& context) {
  ASSERT(buffer.length() >= DubboCodec::HeadersSize);
  ResponseStatus status = static_cast<ResponseStatus>(buffer.peekInt<uint8_t>(StatusOffset));
  if (!isValidResponseStatus(status)) {
    throw EnvoyException(
        absl::StrCat("invalid dubbo message response status ",
                     static_cast<std::underlying_type<ResponseStatus>::type>(status)));
  }

  context.setResponseStatus(status);
}

DecodeStatus DubboCodec::decodeHeader(Buffer::Instance& buffer, MessageMetadata& metadata) {
  // Empty metadata.
  ASSERT(!metadata.hasMessageContextInfo());

  if (buffer.length() < DubboCodec::HeadersSize) {
    return DecodeStatus::Waiting;
  }

  uint16_t magic_number = buffer.peekBEInt<uint16_t>();
  if (magic_number != MagicNumber) {
    throw EnvoyException(absl::StrCat("invalid dubbo message magic number ", magic_number));
  }

  auto context = std::make_shared<MessageContext>();

  uint8_t flag = buffer.peekInt<uint8_t>(FlagOffset);

  // Intial basic type of message.
  MessageType type =
      (flag & MessageTypeMask) == MessageTypeMask ? MessageType::Request : MessageType::Response;

  bool is_event = (flag & EventMask) == EventMask ? true : false;

  int64_t request_id = buffer.peekBEInt<int64_t>(RequestIDOffset);

  int32_t body_size = buffer.peekBEInt<int32_t>(BodySizeOffset);

  // The body size of the heartbeat message is zero.
  if (body_size > MaxBodySize || body_size < 0) {
    throw EnvoyException(absl::StrCat("invalid dubbo message size ", body_size));
  }

  context->setRequestId(request_id);

  if (type == MessageType::Request) {
    if (is_event) {
      type = MessageType::HeartbeatRequest;
    }
    context->setMessageType(type);
    parseRequestInfoFromBuffer(buffer, *context);
  } else {
    if (is_event) {
      type = MessageType::HeartbeatResponse;
    }
    context->setMessageType(type);
    parseResponseInfoFromBuffer(buffer, *context);
  }

  context->setBodySize(body_size);
  context->setHeartbeat(is_event);

  metadata.setMessageContextInfo(std::move(context));

  return DecodeStatus::Success;
}

DecodeStatus DubboCodec::decodeData(Buffer::Instance& buffer, MessageMetadata& metadata) {
  ASSERT(metadata.hasMessageContextInfo());
  ASSERT(serializer_ ! = nullptr);

  auto& context = metadata.mutableMessageContextInfo();

  if (buffer.length() < context.bodySize()) {
    return DecodeStatus::Waiting;
  }

  switch (context.messageType()) {
  case MessageType::Oneway:
  case MessageType::Request: {
    auto ret = serializer_->deserializeRpcRequest(buffer, context);
    metadata.setRequestInfo(std::move(ret));
    break;
  }
  case MessageType::Response: {
    auto ret = serializer_->deserializeRpcResponse(buffer, context);
    if (ret->hasException()) {
      context.setMessageType(MessageType::Exception);
    }
    metadata.setResponseInfo(std::move(ret));
    break;
  }
  default:
    PANIC("not handled");
  }

  return DecodeStatus::Success;
}

void DubboCodec::encode(Buffer::Instance& buffer, MessageMetadata& metadata) {
  ASSERT(metadata.hasMessageContextInfo());
  ASSERT(serializer_);

  auto& context = metadata.mutableMessageContextInfo();

  switch (context.messageType()) {
  case MessageType::HeartbeatResponse: {
    ASSERT(context.hasResponseStatus());
    ASSERT(!metadata.hasResponseInfo());
    buffer.writeBEInt<uint16_t>(MagicNumber);
    uint8_t flag = static_cast<uint8_t>(context.serializeType());
    flag = flag ^ EventMask;
    buffer.writeByte(flag);
    buffer.writeByte(static_cast<uint8_t>(context.responseStatus().value()));
    buffer.writeBEInt<uint64_t>(context.requestId());
    // Body of heart beat response is null.
    // TODO(wbpcode): Currently we only support the Hessian2 serialization scheme, so here we
    // directly use the 'N' for null object in Hessian2. This coupling should be unnecessary.
    buffer.writeBEInt<uint32_t>(1u);
    buffer.writeByte('N');
    return;
  }
  case MessageType::Response: {
    ASSERT(metadata.hasResponseStatus());
    ASSERT(metadata.has);
    Buffer::OwnedImpl body_buffer;
    size_t serialized_body_size = serializer_->serializeRpcResponse(body_buffer, content, type);

    buffer.writeBEInt<uint16_t>(MagicNumber);
    buffer.writeByte(static_cast<uint8_t>(metadata.serializationType()));
    buffer.writeByte(static_cast<uint8_t>(metadata.responseStatus()));
    buffer.writeBEInt<uint64_t>(metadata.requestId());
    buffer.writeBEInt<uint32_t>(serialized_body_size);

    buffer.move(body_buffer, serialized_body_size);
    return true;
  }
  case MessageType::Request:
  case MessageType::Oneway:
  case MessageType::Exception:
    PANIC("not implemented");
  default:
    PANIC("not implemented");
  }
}

} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
