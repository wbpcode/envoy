#include "source/extensions/common/dubbo/hessian2_serializer.h"

#include <cstddef>

#include "envoy/common/exception.h"

#include "source/common/common/assert.h"
#include "source/common/common/macros.h"
#include "source/extensions/common/dubbo/hessian2_utils.h"
#include "source/extensions/common/dubbo/message.h"
#include "source/extensions/common/dubbo/metadata.h"

#include "hessian2/object.hpp"
#include "absl/status/statusor.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Dubbo {

absl::StatusOr<RpcRequestPtr>
Hessian2SerializerImpl::deserializeRpcRequest(Buffer::Instance& buffer, Metadata& metadata) const {
  // The caller should be able to handle the case where the buffer is not large enough to
  // deserialize the entire message.
  ASSERT(metadata.bodySize() <= buffer.length());

  // Handle heartbeat.
  if (metadata.heartbeat()) {
    buffer.drain(metadata.bodySize());
    return RpcRequestPtr{nullptr};
  }

  // Handle normal request or oneway request.
  ASSERT(metadata.messageType() == MessageType::Request ||
         metadata.messageType() == MessageType::Oneway);
  ASSERT(metadata.bodySize() <= buffer.length());

  Hessian2::Decoder decoder(std::make_unique<BufferReader>(buffer));

  // TODO(zyfjeff): Add format checker
  auto dubbo_version = decoder.decode<std::string>();
  auto service_name = decoder.decode<std::string>();
  auto service_version = decoder.decode<std::string>();
  auto method_name = decoder.decode<std::string>();

  const auto decoded_size = decoder.offset();

  if (metadata.bodySize() < decoded_size) {
    return absl::InvalidArgumentError(fmt::format("RpcRequest size({}) larger than body size({})",
                                                  decoded_size, metadata.bodySize()));
  }

  if (dubbo_version == nullptr || service_name == nullptr || service_version == nullptr ||
      method_name == nullptr) {
    return absl::InvalidArgumentError(fmt::format("RpcRequest has no request metadata"));
  }

  buffer.drain(decoded_size);

  // Set the output request.
  auto request = std::make_unique<RpcRequest>(std::move(*dubbo_version), std::move(*service_name),
                                              std::move(*service_version), std::move(*method_name));
  request->content().initialize(buffer, metadata.bodySize() - decoded_size);

  return request;
}

absl::StatusOr<RpcResponsePtr>
Hessian2SerializerImpl::deserializeRpcResponse(Buffer::Instance& buffer, Metadata& metadata) const {
  // The caller should be able to handle the case where the buffer is not large enough to
  // deserialize the entire message.
  ASSERT(metadata.bodySize() <= buffer.length());

  // Handle heartbeat.
  if (metadata.heartbeat()) {
    buffer.drain(metadata.bodySize());
    return RpcResponsePtr{nullptr};
  }

  ASSERT(metadata.response());

  // Set the output response.
  auto response = std::make_unique<RpcResponse>();

  // Non `Ok` response body has no response type info and skip deserialization.
  if (metadata.messageType() == MessageType::Exception) {
    ASSERT(metadata.responseStatus() != ResponseStatus::Ok);
    response->content().initialize(buffer, metadata.bodySize());
    return absl::OkStatus();
  }

  Hessian2::Decoder decoder(std::make_unique<BufferReader>(buffer));
  auto type_value = decoder.decode<int32_t>();
  if (type_value == nullptr) {
    return absl::InvalidArgumentError(fmt::format("Cannot parse RpcResponse type from buffer"));
  }

  const RpcResponseType type = static_cast<RpcResponseType>(*type_value);

  switch (type) {
  case RpcResponseType::ResponseWithException:
  case RpcResponseType::ResponseWithExceptionWithAttachments:
    metadata.setMessageType(MessageType::Exception);
    break;
  case RpcResponseType::ResponseWithNullValue:
  case RpcResponseType::ResponseNullValueWithAttachments:
  case RpcResponseType::ResponseWithValue:
  case RpcResponseType::ResponseValueWithAttachments:
    break;
  default:
    return absl::InvalidArgumentError(
        fmt::format("not supported return type {}", static_cast<uint8_t>(type)));
  }

  const auto decoded_size = decoder.offset();

  if (metadata.bodySize() < decoded_size) {
    return absl::InvalidArgumentError(fmt::format("RpcResponse size({}) large than body size({})",
                                                  decoded_size, metadata.bodySize()));
  }

  buffer.drain(decoded_size);

  response->setResponseType(type);
  response->content().initialize(buffer, metadata.bodySize() - decoded_size);

  return response;
}

void Hessian2SerializerImpl::serializeRpcResponse(Buffer::Instance& buffer,
                                                  const Metadata& metadata,
                                                  OptRef<const RpcResponse> response) const {
  if (metadata.heartbeat()) {
    buffer.writeByte('N');
    return;
  }
  if (!response.has_value()) {
    ENVOY_LOG(error, "Dubbo hessian2 serializer: non heartbeat but response is null");
    return;
  }

  if (auto type = response->responseType(); type.has_value()) {
    ASSERT(metadata.responseStatus() == ResponseStatus::Ok);
    buffer.writeByte(0x90 + static_cast<uint8_t>(type.value()));
  }

  buffer.add(response->content().buffer());
}

void Hessian2SerializerImpl::serializeRpcRequest(Buffer::Instance& buffer, const Metadata& metadata,
                                                 OptRef<const RpcRequest> request) const {
  if (metadata.heartbeat()) {
    buffer.writeByte('N');
    return;
  }
  if (!request.has_value()) {
    ENVOY_LOG(error, "Dubbo hessian2 serializer: non heartbeat but request is null");
    return;
  }

  Hessian2::Encoder encoder(std::make_unique<BufferWriter>(buffer));

  encoder.encode<absl::string_view>(request->version());
  encoder.encode<absl::string_view>(request->service());
  encoder.encode<absl::string_view>(request->serviceVersion());
  encoder.encode<absl::string_view>(request->method());

  buffer.add(request->content().buffer());
}

} // namespace Dubbo
} // namespace Common
} // namespace Extensions
} // namespace Envoy
