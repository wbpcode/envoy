#pragma once

#include <memory>
#include <string>

#include "source/common/common/assert.h"
#include "source/extensions/common/dubbo/message.h"

#include "absl/types/optional.h"
#include "hessian2/object.hpp"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Dubbo {

class Metadata {
public:
  void setMessageType(MessageType type) { message_type_ = type; }
  MessageType messageType() const { return message_type_; }

  void setResponseStatus(ResponseStatus status) { response_status_ = status; }
  ResponseStatus responseStatus() const { return response_status_; }

  void setRequestId(uint64_t id) { request_id_ = id; }
  uint64_t requestId() const { return request_id_; }

  bool heartbeat() const {
    return message_type_ == MessageType::HeartbeatRequest ||
           message_type_ == MessageType::HeartbeatResponse;
  }
  bool oneway() const { return message_type_ == MessageType::Oneway; }
  bool request() const {
    // Heartbeat request will not be treat as real request.
    return message_type_ == MessageType::Request || message_type_ == MessageType::Oneway;
  }
  bool response() const {
    // Heartbeat response will not be treat as real response.
    return message_type_ == MessageType::Response || message_type_ == MessageType::Exception;
  }

  // Body size of dubbo request or dubbo response. Only make sense for
  // decoding. Never use this size except the request/response decoder.
  void setBodySize(uint64_t size) { body_size_ = size; }
  uint64_t bodySize() const { return body_size_; }

private:
  MessageType message_type_{MessageType::Request};
  ResponseStatus response_status_{ResponseStatus::Ok};
  uint64_t request_id_{};

  uint64_t body_size_{};
};

} // namespace Dubbo
} // namespace Common
} // namespace Extensions
} // namespace Envoy
