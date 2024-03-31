#include <memory>

#include "source/extensions/common/dubbo/message.h"
#include "source/extensions/common/dubbo/metadata.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Dubbo {
namespace {

TEST(MetadataTest, MetadataTest) {
  Metadata metadata;

  // Simple set and get of message type.
  metadata.setMessageType(MessageType::HeartbeatResponse);
  EXPECT_EQ(MessageType::HeartbeatResponse, metadata.messageType());

  // Simple set and get of response status
  metadata.setResponseStatus(ResponseStatus::BadRequest);
  EXPECT_EQ(ResponseStatus::BadRequest, metadata.responseStatus());

  // Simple set and get of body size.
  metadata.setBodySize(12345);
  EXPECT_EQ(12345, metadata.bodySize());

  // Simple set and get of request id.
  metadata.setRequestId(12345);
  EXPECT_EQ(12345, metadata.requestId());

  // Two way request check.
  metadata.setMessageType(MessageType::Oneway);
  EXPECT_EQ(true, metadata.oneway());
  metadata.setMessageType(MessageType::Response);
  EXPECT_EQ(false, metadata.oneway());
  metadata.setMessageType(MessageType::HeartbeatRequest);
  EXPECT_EQ(false, metadata.oneway());
  metadata.setMessageType(MessageType::Request);
  EXPECT_EQ(false, metadata.oneway());

  // Heartbeat check.
  metadata.setMessageType(MessageType::Request);
  EXPECT_EQ(false, metadata.heartbeat());
  metadata.setMessageType(MessageType::Response);
  EXPECT_EQ(false, metadata.heartbeat());
  metadata.setMessageType(MessageType::HeartbeatResponse);
  EXPECT_EQ(true, metadata.heartbeat());
  metadata.setMessageType(MessageType::HeartbeatRequest);
  EXPECT_EQ(true, metadata.heartbeat());

  // Request check.
  metadata.setMessageType(MessageType::Request);
  EXPECT_EQ(true, metadata.request());
  metadata.setMessageType(MessageType::Response);
  EXPECT_EQ(false, metadata.request());
  // Oneway request will be treated as a request.
  metadata.setMessageType(MessageType::Oneway);
  EXPECT_EQ(true, metadata.request());
  // Heartbeat request will not be treated as a request.
  metadata.setMessageType(MessageType::HeartbeatRequest);
  EXPECT_EQ(false, metadata.request());

  // Response check.
  metadata.setMessageType(MessageType::Request);
  EXPECT_EQ(false, metadata.response());
  metadata.setMessageType(MessageType::Response);
  EXPECT_EQ(true, metadata.response());
  // Exception will be treated as a response.
  metadata.setMessageType(MessageType::Exception);
  EXPECT_EQ(true, metadata.response());
  // Heartbeat response will not be treated as a response.
  metadata.setMessageType(MessageType::HeartbeatResponse);
  EXPECT_EQ(false, metadata.response());
}

} // namespace
} // namespace Dubbo
} // namespace Common
} // namespace Extensions
} // namespace Envoy
