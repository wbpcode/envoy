#pragma once

#include <memory>
#include <string>

#include "source/common/common/assert.h"
#include "source/common/common/empty_string.h"
#include "source/common/http/header_map_impl.h"
#include "source/extensions/common/dubbo/message.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Dubbo {

/**
 * Context of dubbo request/response.
 */
class MessageContext {
public:
  void setSerializeType(SerializeType type) { serialize_type_ = type; }
  SerializeType serializeType() const { return serialize_type_; }

  void setMessageType(MessageType type) { message_type_ = type; }
  MessageType messageType() const { return message_type_; }

  void setResponseStatus(ResponseStatus status) { response_status_ = status; }
  bool hasResponseStatus() const { return response_status_.has_value(); }
  ResponseStatus responseStatus() const {
    ASSERT(hasResponseStatus());
    return response_status_.value();
  }

  // Body size of dubbo request or dubbo response. Only make sense for
  // decoding.
  void setBodySize(size_t size) { body_size_ = size; }
  size_t bodySize() const { return body_size_; }

  void setRequestId(int64_t id) { request_id_ = id; }
  int64_t requestId() const { return request_id_; }

  bool isTwoWay() const { return message_type_ == MessageType::Request; }

  bool heartbeat() const {
    return message_type_ == MessageType::HeartbeatRequest ||
           message_type_ == MessageType::HeartbeatResponse;
  }

  ProtocolType protocolType() const { return ProtocolType::Dubbo; }

private:
  SerializeType serialize_type_{SerializeType::Hessian2};
  MessageType message_type_{MessageType::Request};
  absl::optional<ResponseStatus> response_status_{};

  int64_t request_id_{};

  size_t body_size_{};
};

using MessageContextSharedPtr = std::shared_ptr<MessageContext>;

class MessageMetadata {
public:
  // Common message context.
  void setMessageContextInfo(MessageContextSharedPtr context) {
    message_context_info_ = std::move(context);
  }
  bool hasMessageContextInfo() const { return message_context_info_ != nullptr; }
  const MessageContext& messageContextInfo() const { return *message_context_info_; }
  MessageContext& mutableMessageContextInfo() { return *message_context_info_; }

  // Helper method to access attributes of common context.
  MessageType messageType() const {
    ASSERT(hasMessageContextInfo());
    return message_context_info_->messageType();
  }
  bool hasResponseStatus() const {
    ASSERT(hasMessageContextInfo());
    return message_context_info_->hasResponseStatus();
  }
  ResponseStatus responseStatus() const {
    ASSERT(hasMessageContextInfo());
    return message_context_info_->responseStatus();
  }
  bool heartbeat() const {
    ASSERT(hasMessageContextInfo());
    return message_context_info_->heartbeat();
  }
  int64_t requestId() const {
    ASSERT(hasMessageContextInfo());
    return message_context_info_->requestId();
  }

  // Request info.
  void setRequestInfo(RpcRequestSharedPtr request_info) {
    rpc_request_info_ = std::move(request_info);
  }
  bool hasRequestInfo() const { return rpc_request_info_ != nullptr; }
  const RpcRequest& requestInfo() const { return *rpc_request_info_; }
  RpcRequest& mutableRequestInfo() { return *rpc_request_info_; }

  // Response info.
  void setResponseInfo(RpcResponseSharedPtr response_info) {
    rpc_response_info_ = std::move(response_info);
  }
  bool hasResponseInfo() const { return rpc_response_info_ != nullptr; }
  const RpcResponse& responseInfo() const { return *rpc_response_info_; }
  RpcResponse& mutableResponseInfo() { return *rpc_response_info_; }

private:
  // Common message context for dubbo request and dubbo response.
  MessageContextSharedPtr message_context_info_;

  RpcRequestSharedPtr rpc_request_info_;

  RpcResponseSharedPtr rpc_response_info_;
};

using MessageMetadataSharedPtr = std::shared_ptr<MessageMetadata>;

} // namespace Dubbo
} // namespace Common
} // namespace Extensions
} // namespace Envoy
