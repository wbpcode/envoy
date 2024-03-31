#include "source/extensions/common/dubbo/codec.h"

#include <cstdint>
#include <memory>

#include "envoy/registry/registry.h"

#include "source/common/common/assert.h"
#include "source/extensions/common/dubbo/hessian2_serializer.h"
#include "source/extensions/common/dubbo/message.h"
#include "source/extensions/common/dubbo/metadata.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Dubbo {

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

void encodeHeader(Buffer::Instance& buffer, const Metadata& metadata, uint32_t body_size) {
  // Magic number.
  buffer.writeBEInt<uint16_t>(MagicNumber);

  // Serialize type and flag.
  uint8_t flag = static_cast<uint8_t>(SerializeType::Hessian2);

  switch (metadata.messageType()) {
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
    break;
  case MessageType::Exception:
    // Exception response.
    break;
  case MessageType::HeartbeatRequest:
    // Event request.
    flag ^= MessageTypeMask;
    flag ^= TwoWayMask;
    flag ^= EventMask;
    break;
  case MessageType::HeartbeatResponse:
    flag ^= EventMask;
    break;
  default:
    PANIC_DUE_TO_CORRUPT_ENUM;
  }
  buffer.writeByte(flag);

  // Optional response status.
  buffer.writeByte(metadata.response() ? static_cast<uint8_t>(metadata.responseStatus()) : 0x00);

  // Request id.
  buffer.writeBEInt<uint64_t>(metadata.requestId());

  // Because the body size in the metadata is the size of original request or response.
  // It may be changed after the processing of filters. So write the explicit specified
  // body size here.
  buffer.writeBEInt<uint32_t>(body_size);
}

} // namespace

// Consistent with the SerializeType
bool isValidSerializeType(SerializeType type) { return type == SerializeType::Hessian2; }

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
  case ResponseStatus::ServerError:
  case ResponseStatus::ClientError:
  case ResponseStatus::ServerThreadpoolExhaustedError:
    return true;
  }
  return false;
}

absl::Status parseRequestInfoFromBuffer(Buffer::Instance& data, Metadata& metadata) {
  ASSERT(data.length() >= DubboCodec::HeadersSize);
  const uint8_t flag = data.peekInt<uint8_t>(FlagOffset);
  const bool is_two_way = (flag & TwoWayMask) == TwoWayMask ? true : false;

  // Request without two flag should be one way request.
  if (!metadata.heartbeat() && !is_two_way) {
    metadata.setMessageType(MessageType::Oneway);
  }

  return absl::OkStatus();
}

absl::Status parseResponseInfoFromBuffer(Buffer::Instance& buffer, Metadata& metadata) {
  ASSERT(buffer.length() >= DubboCodec::HeadersSize);
  ResponseStatus status = static_cast<ResponseStatus>(buffer.peekInt<uint8_t>(StatusOffset));
  if (!isValidResponseStatus(status)) {
    return absl::InvalidArgumentError(
        absl::StrCat("invalid dubbo message response status ",
                     static_cast<std::underlying_type<ResponseStatus>::type>(status)));
  }
  metadata.setResponseStatus(status);

  if (status != ResponseStatus::Ok) {
    metadata.setMessageType(MessageType::Exception);
  }
  return absl::OkStatus();
}

DubboCodec DubboCodec::codecFromSerializeType(SerializeType type) {
  ASSERT(type == SerializeType::Hessian2);

  DubboCodec codec;
  codec.initilize(std::make_unique<Hessian2SerializerImpl>());
  return codec;
}

DecodeStatus DubboCodec::decodeHeader(Buffer::Instance& buffer, OptMetadata& output_metadata) {
  if (buffer.length() < DubboCodec::HeadersSize) {
    return DecodeStatus::Waiting;
  }

  uint16_t magic_number = buffer.peekBEInt<uint16_t>();
  if (magic_number != MagicNumber) {
    ENVOY_LOG(info, "invalid dubbo message magic number {}", magic_number);
    return DecodeStatus::Failure;
  }

  Metadata metadata;

  const uint8_t flag = buffer.peekInt<uint8_t>(FlagOffset);

  // Decode serialize type.
  const auto serialize_type = static_cast<SerializeType>(flag & SerializeTypeMask);
  if (serialize_type != serializer_->type()) {
    ENVOY_LOG(info,
              absl::StrCat("invalid dubbo message serialization type ",
                           static_cast<std::underlying_type<SerializeType>::type>(serialize_type)));
    return DecodeStatus::Failure;
  }

  // Initial basic type of message.
  MessageType type =
      (flag & MessageTypeMask) == MessageTypeMask ? MessageType::Request : MessageType::Response;

  const bool is_event = (flag & EventMask) == EventMask ? true : false;
  const int64_t request_id = buffer.peekBEInt<int64_t>(RequestIDOffset);
  const int32_t body_size = buffer.peekBEInt<int32_t>(BodySizeOffset);

  // The body size of the heartbeat message is zero.
  if (body_size > MaxBodySize || body_size < 0) {
    ENVOY_LOG(info, absl::StrCat("invalid dubbo message size ", body_size));
    return DecodeStatus::Failure;
  }

  metadata.setRequestId(request_id);

  if (type == MessageType::Request) {
    if (is_event) {
      type = MessageType::HeartbeatRequest;
    }
    metadata.setMessageType(type);
    auto status = parseRequestInfoFromBuffer(buffer, metadata);
    ASSERT(status.ok());
  } else {
    if (is_event) {
      type = MessageType::HeartbeatResponse;
    }
    metadata.setMessageType(type);

    auto status = parseResponseInfoFromBuffer(buffer, metadata);
    if (!status.ok()) {
      ENVOY_LOG(info, "parseResponseInfoFromBuffer failed: {}", status.message());
      return DecodeStatus::Failure;
    }
  }

  metadata.setBodySize(body_size);

  // Drain headers bytes.
  buffer.drain(DubboCodec::HeadersSize);

  output_metadata = metadata;
  return DecodeStatus::Success;
}

DecodeStatus DubboCodec::decodeData(Buffer::Instance& buffer, Metadata& metadata,
                                    RpcRequestPtr& output_request) {
  if (!metadata.request() && !metadata.heartbeat()) {
    ENVOY_LOG(info, "invalid dubbo message type {} for request",
              static_cast<uint8_t>(metadata.messageType()));

    return DecodeStatus::Failure;
  }
  if (buffer.length() < metadata.bodySize()) {
    return DecodeStatus::Waiting;
  }

  auto result = serializer_->deserializeRpcRequest(buffer, metadata);
  if (!result.ok()) {
    ENVOY_LOG(info, "failed to deserialize rpc request: {}", result.status().message());
    return DecodeStatus::Failure;
  }

  output_request = std::move(result.value());
  return DecodeStatus::Success;
}

DecodeStatus DubboCodec::decodeData(Buffer::Instance& buffer, Metadata& metadata,
                                    RpcResponsePtr& output_response) {
  if (!metadata.response() && !metadata.heartbeat()) {
    ENVOY_LOG(info, "invalid dubbo message type {} for response",
              static_cast<uint8_t>(metadata.messageType()));

    return DecodeStatus::Failure;
  }

  if (buffer.length() < metadata.bodySize()) {
    return DecodeStatus::Waiting;
  }

  auto result = serializer_->deserializeRpcResponse(buffer, metadata);
  if (!result.ok()) {
    ENVOY_LOG(info, "failed to deserialize rpc response: {}", result.status().message());
    return DecodeStatus::Failure;
  }

  output_response = std::move(result.value());
  return DecodeStatus::Success;
}

void DubboCodec::encodeEntireMessage(Buffer::Instance& buffer, const Metadata& metadata,
                                     OptRef<const RpcRequest> request) {
  Buffer::OwnedImpl body_buffer;
  serializer_->serializeRpcRequest(body_buffer, metadata, request);
  encodeHeader(buffer, metadata, body_buffer.length());
  buffer.move(body_buffer);
}

void DubboCodec::encodeEntireMessage(Buffer::Instance& buffer, const Metadata& metadata,
                                     OptRef<const RpcResponse> response) {
  Buffer::OwnedImpl body_buffer;
  serializer_->serializeRpcResponse(body_buffer, metadata, response);
  encodeHeader(buffer, metadata, body_buffer.length());
  buffer.move(body_buffer);
}

RpcResponsePtr DirectResponseUtil::localResponse(absl::Status status, absl::string_view content) {
  // Set response.
  auto response = std::make_unique<RpcResponse>();
  Attachments attachments;
  if (status.ok()) {
    response->setResponseType(RpcResponseType::ResponseValueWithAttachments);
    attachments["reason"] = "envoy-response";
  } else {
    response->setResponseType(RpcResponseType::ResponseWithExceptionWithAttachments);
    attachments["reason"] = status.message();
  }

  response->content().initialize(std::make_unique<Hessian2::StringObject>(content),
                                 std::move(attachments));
  return response;
}

} // namespace Dubbo
} // namespace Common
} // namespace Extensions
} // namespace Envoy
