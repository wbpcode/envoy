#include <memory>

#include "source/extensions/common/dubbo/codec.h"
#include "source/extensions/common/dubbo/message.h"

#include "test/extensions/common/dubbo/mocks.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"
#include "hessian2/object.hpp"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Dubbo {
namespace {

constexpr uint16_t MagicNumber = 0xdabb;
constexpr uint64_t FlagOffset = 2;
constexpr uint64_t StatusOffset = 3;
constexpr uint64_t RequestIDOffset = 4;
constexpr uint64_t BodySizeOffset = 12;

inline void addInt32(Buffer::Instance& buffer, uint32_t value) {
  value = htobe32(value);
  buffer.add(&value, 4);
}

inline void addInt64(Buffer::Instance& buffer, uint64_t value) {
  value = htobe64(value);
  buffer.add(&value, 8);
}

TEST(DubboCodecTest, GetSerializer) {
  DubboCodec codec;

  auto serializer = std::make_unique<MockSerializer>();
  // Keep this for mock.
  auto raw_serializer = serializer.get();
  codec.initilize(std::move(serializer));

  EXPECT_EQ(raw_serializer, &codec.serializer());
}

TEST(DubboCodecTest, CodecWithSerializer) {
  auto codec = DubboCodec::codecFromSerializeType(SerializeType::Hessian2);
  EXPECT_EQ(SerializeType::Hessian2, codec.serializer().type());
}

TEST(DubboCodecTest, NotEnoughData) {
  Buffer::OwnedImpl buffer;
  DubboCodec codec;
  auto result = codec.decodeHeader(buffer);
  EXPECT_EQ(DecodeStatus::Waiting, result.first);

  buffer.add(std::string(15, 0x00));
  result = codec.decodeHeader(buffer);
  EXPECT_EQ(DecodeStatus::Waiting, result.first);
}

TEST(DubboCodecTest, DecodeHeaderTest) {
  DubboCodec codec;
  auto serializer = std::make_unique<MockSerializer>();
  codec.initilize(std::move(serializer));

  // Invalid dubbo magic number
  {
    Buffer::OwnedImpl buffer;
    addInt64(buffer, 0);
    addInt64(buffer, 0);

    EXPECT_LOG_CONTAINS("info", "invalid dubbo message magic number 0", codec.decodeHeader(buffer));
  }

  // Invalid message body size
  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({'\xda', '\xbb', '\xc2', 0x00}));
    addInt64(buffer, 1);
    addInt32(buffer, DubboCodec::MaxBodySize + 1);
    std::string exception_string =
        fmt::format("invalid dubbo message size {}", DubboCodec::MaxBodySize + 1);

    EXPECT_LOG_CONTAINS("info", exception_string, codec.decodeHeader(buffer));
  }

  // Invalid serialization type
  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({'\xda', '\xbb', '\xc3', 0x00}));
    addInt64(buffer, 1);
    addInt32(buffer, 0xff);

    EXPECT_LOG_CONTAINS("info", "invalid dubbo message serialization type 3",
                        codec.decodeHeader(buffer));
  }

  // Invalid response status
  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({'\xda', '\xbb', 0x02, 0x00}));
    addInt64(buffer, 1);
    addInt32(buffer, 0xff);

    EXPECT_LOG_CONTAINS("info", "invalid dubbo message response status 0",
                        codec.decodeHeader(buffer));
  }

  // Normal dubbo request message
  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({'\xda', '\xbb', '\xc2', 0x00}));
    addInt64(buffer, 1);
    addInt32(buffer, 1);

    auto result = codec.decodeHeader(buffer);
    EXPECT_EQ(DecodeStatus::Success, result.first);

    auto metadata = result.second.value();

    EXPECT_EQ(1, metadata.requestId());
    EXPECT_EQ(1, metadata.bodySize());
    EXPECT_EQ(MessageType::Request, metadata.messageType());
  }

  // Oneway dubbo request message
  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({'\xda', '\xbb', '\x82', 0x00}));
    addInt64(buffer, 1);
    addInt32(buffer, 1);
    auto result = codec.decodeHeader(buffer);
    EXPECT_EQ(DecodeStatus::Success, result.first);

    auto metadata = result.second.value();

    EXPECT_EQ(1, metadata.requestId());
    EXPECT_EQ(1, metadata.bodySize());
    EXPECT_EQ(MessageType::Oneway, metadata.messageType());
  }

  // Heartbeat request message
  {
    Buffer::OwnedImpl buffer;

    buffer.add(std::string({'\xda', '\xbb', '\xe2', 0x00}));
    addInt64(buffer, 1);
    addInt32(buffer, 1);
    auto result = codec.decodeHeader(buffer);
    EXPECT_EQ(DecodeStatus::Success, result.first);
    auto metadata = result.second.value();

    EXPECT_EQ(1, metadata.requestId());
    EXPECT_EQ(1, metadata.bodySize());
    EXPECT_EQ(MessageType::HeartbeatRequest, metadata.messageType());
  }

  // Normal dubbo response message
  {
    Buffer::OwnedImpl buffer;

    buffer.add(std::string({'\xda', '\xbb', 0x02, 20}));
    addInt64(buffer, 1);
    addInt32(buffer, 1);

    auto result = codec.decodeHeader(buffer);
    EXPECT_EQ(DecodeStatus::Success, result.first);
    auto metadata = result.second.value();
    ;

    EXPECT_EQ(1, metadata.requestId());
    EXPECT_EQ(1, metadata.bodySize());
    EXPECT_EQ(MessageType::Response, metadata.messageType());
    EXPECT_EQ(true, metadata.response());
    EXPECT_EQ(ResponseStatus::Ok, metadata.responseStatus());
  }

  // Normal dubbo response with error.
  {
    Buffer::OwnedImpl buffer;

    buffer.add(std::string({'\xda', '\xbb', 0x02, 40}));
    addInt64(buffer, 1);
    addInt32(buffer, 1);

    auto result = codec.decodeHeader(buffer);
    EXPECT_EQ(DecodeStatus::Success, result.first);
    auto metadata = result.second.value();
    ;

    EXPECT_EQ(1, metadata.requestId());
    EXPECT_EQ(1, metadata.bodySize());
    EXPECT_EQ(MessageType::Exception, metadata.messageType());
    EXPECT_EQ(true, metadata.response());
    EXPECT_EQ(ResponseStatus::BadRequest, metadata.responseStatus());
  }

  // Heartbeat response message
  {
    Buffer::OwnedImpl buffer;

    buffer.add(std::string({'\xda', '\xbb', '\x22', 20}));
    addInt64(buffer, 1);
    addInt32(buffer, 1);
    auto result = codec.decodeHeader(buffer);
    EXPECT_EQ(DecodeStatus::Success, result.first);
    auto metadata = result.second.value();
    ;

    EXPECT_EQ(1, metadata.requestId());
    EXPECT_EQ(1, metadata.bodySize());
    EXPECT_EQ(MessageType::HeartbeatResponse, metadata.messageType());
  }
}

TEST(DubboCodecTest, DecodeDataTest) {
  DubboCodec codec;

  auto serializer = std::make_unique<MockSerializer>();
  // Keep this for mock.
  auto raw_serializer = serializer.get();
  codec.initilize(std::move(serializer));

  // No enough data.
  {
    Buffer::OwnedImpl buffer;
    buffer.add("anything");

    Metadata metadata;

    metadata.setMessageType(MessageType::Request);
    metadata.setRequestId(1);
    metadata.setBodySize(buffer.length() + 1);

    EXPECT_EQ(DecodeStatus::Waiting, codec.decodeRequest(buffer, metadata).first);
  }

  // Decode request body.
  {
    Buffer::OwnedImpl buffer;
    buffer.add("anything");

    Metadata metadata;

    metadata.setMessageType(MessageType::Request);
    metadata.setRequestId(1);
    metadata.setBodySize(buffer.length());

    EXPECT_CALL(*raw_serializer, deserializeRpcRequest(_, _))
        .WillOnce(
            testing::Return(testing::ByMove(std::make_unique<RpcRequest>("a", "b", "c", "d"))));

    auto result = codec.decodeRequest(buffer, metadata);
    EXPECT_EQ(DecodeStatus::Success, result.first);
    EXPECT_NE(nullptr, result.second);
  }

  // Decode request body with null request.
  {
    Buffer::OwnedImpl buffer;
    buffer.add("anything");

    Metadata metadata;

    metadata.setMessageType(MessageType::HeartbeatRequest);
    metadata.setRequestId(1);
    metadata.setBodySize(buffer.length());

    EXPECT_CALL(*raw_serializer, deserializeRpcRequest(_, _))
        .WillOnce(testing::Return(testing::ByMove(nullptr)));

    auto result = codec.decodeRequest(buffer, metadata);
    EXPECT_EQ(DecodeStatus::Success, result.first);
    EXPECT_EQ(nullptr, result.second);
  }

  // Decode response body.
  {
    Buffer::OwnedImpl buffer;
    buffer.add("anything");

    Metadata metadata;

    metadata.setMessageType(MessageType::Response);
    metadata.setRequestId(1);
    metadata.setBodySize(buffer.length());
    metadata.setResponseStatus(ResponseStatus::Ok);

    EXPECT_CALL(*raw_serializer, deserializeRpcResponse(_, _))
        .WillOnce(testing::Return(testing::ByMove(std::make_unique<RpcResponse>())));

    auto result = codec.decodeResponse(buffer, metadata);
    EXPECT_EQ(DecodeStatus::Success, result.first);
    EXPECT_NE(nullptr, result.second);
  }

  // Decode response body.
  {
    Buffer::OwnedImpl buffer;
    buffer.add("anything");

    Metadata metadata;

    metadata.setMessageType(MessageType::HeartbeatResponse);
    metadata.setRequestId(1);
    metadata.setBodySize(buffer.length());
    metadata.setResponseStatus(ResponseStatus::Ok);

    EXPECT_CALL(*raw_serializer, deserializeRpcResponse(_, _))
        .WillOnce(testing::Return(testing::ByMove(nullptr)));

    auto result = codec.decodeResponse(buffer, metadata);
    EXPECT_EQ(DecodeStatus::Success, result.first);
    EXPECT_NE(nullptr, result.second);
  }

  // Encode unexpected message type will cause exit.
  {
    Buffer::OwnedImpl buffer;
    buffer.add("anything");

    Metadata metadata;

    metadata.setMessageType(static_cast<MessageType>(6));
    metadata.setRequestId(1);
    metadata.setBodySize(buffer.length());
    metadata.setResponseStatus(ResponseStatus::Ok);

    EXPECT_DEATH(codec.decodeResponse(buffer, metadata), ".*panic: corrupted enum.*");
  }
}

TEST(DubboCodecTest, EncodeTest) {
  DubboCodec codec;

  auto serializer = std::make_unique<MockSerializer>();
  // Keep this for mock.
  auto raw_serializer = serializer.get();
  codec.initilize(std::move(serializer));

  // Encode normal request.
  {
    Buffer::OwnedImpl buffer;
    Metadata metadata;

    metadata.setMessageType(MessageType::Request);
    metadata.setRequestId(12345);

    EXPECT_CALL(*raw_serializer, serializeRpcRequest(_, _, _))
        .WillOnce(testing::Invoke([](Buffer::Instance& buffer, const Metadata&,
                                     OptRef<const RpcRequest>) { buffer.add("anything"); }));

    codec.encodeEntireMessage(buffer, metadata, OptRef<const RpcRequest>{});

    EXPECT_EQ(DubboCodec::HeadersSize + 8, buffer.length());

    // Check magic number.
    EXPECT_EQ(MagicNumber, buffer.peekBEInt<uint16_t>());

    // Check flag. 0x80 for request, 0x40 for two way, 0x20 for event/heartbeat, 0x02 for hessian2
    // serialize type. So, the final flag of normal two way request is 0xc2.
    EXPECT_EQ(0xc2, buffer.peekBEInt<uint8_t>(FlagOffset));

    // Check status. Only response has valid status byte.
    EXPECT_EQ(0, buffer.peekBEInt<uint8_t>(StatusOffset));

    // Check request id.
    EXPECT_EQ(12345, buffer.peekBEInt<int64_t>(RequestIDOffset));

    // Check body size.
    EXPECT_EQ(8, buffer.peekBEInt<int32_t>(BodySizeOffset));
  }

  // Encode oneway request.
  {
    Buffer::OwnedImpl buffer;
    Metadata metadata;

    metadata.setMessageType(MessageType::Oneway);
    metadata.setRequestId(12345);

    EXPECT_CALL(*raw_serializer, serializeRpcRequest(_, _, _))
        .WillOnce(testing::Invoke([](Buffer::Instance& buffer, const Metadata&,
                                     OptRef<const RpcRequest>) { buffer.add("anything"); }));

    codec.encodeEntireMessage(buffer, metadata, OptRef<const RpcRequest>{});

    EXPECT_EQ(DubboCodec::HeadersSize + 8, buffer.length());

    // Check magic number.
    EXPECT_EQ(MagicNumber, buffer.peekBEInt<uint16_t>());

    // Check flag.
    EXPECT_EQ(0x82, buffer.peekBEInt<uint8_t>(FlagOffset));

    // Check status. Only response has valid status byte.
    EXPECT_EQ(0, buffer.peekBEInt<uint8_t>(StatusOffset));

    // Check request id.
    EXPECT_EQ(12345, buffer.peekBEInt<int64_t>(RequestIDOffset));

    // Check body size.
    EXPECT_EQ(8, buffer.peekBEInt<int32_t>(BodySizeOffset));
  }

  // Encode heartbeat request.
  {
    Buffer::OwnedImpl buffer;
    Metadata metadata;

    metadata.setMessageType(MessageType::HeartbeatRequest);
    metadata.setRequestId(12345);

    EXPECT_CALL(*raw_serializer, serializeRpcRequest(_, _, _))
        .WillOnce(testing::Invoke([](Buffer::Instance& buffer, const Metadata&,
                                     OptRef<const RpcRequest>) { buffer.add("N"); }));

    codec.encodeEntireMessage(buffer, metadata, OptRef<const RpcRequest>{});

    EXPECT_EQ(DubboCodec::HeadersSize + 1, buffer.length());

    // Check magic number.
    EXPECT_EQ(MagicNumber, buffer.peekBEInt<uint16_t>());

    // Check flag.
    EXPECT_EQ(0xe2, buffer.peekBEInt<uint8_t>(FlagOffset));

    // Check status. Only response has valid status byte.
    EXPECT_EQ(0, buffer.peekBEInt<uint8_t>(StatusOffset));

    // Check request id.
    EXPECT_EQ(12345, buffer.peekBEInt<int64_t>(RequestIDOffset));

    // Check body size.
    EXPECT_EQ(1, buffer.peekBEInt<int32_t>(BodySizeOffset));
  }

  // Encode normal response.
  {
    Buffer::OwnedImpl buffer;
    Metadata metadata;

    metadata.setMessageType(MessageType::Response);
    metadata.setResponseStatus(ResponseStatus::Ok);
    metadata.setRequestId(12345);

    EXPECT_CALL(*raw_serializer, serializeRpcResponse(_, _, _))
        .WillOnce(testing::Invoke([](Buffer::Instance& buffer, const Metadata&,
                                     OptRef<const RpcResponse>) { buffer.add("anything"); }));

    codec.encodeEntireMessage(buffer, metadata, OptRef<const RpcResponse>{});

    EXPECT_EQ(DubboCodec::HeadersSize + 8, buffer.length());

    // Check magic number.
    EXPECT_EQ(MagicNumber, buffer.peekBEInt<uint16_t>());

    // Check flag.
    EXPECT_EQ(0x02, buffer.peekBEInt<uint8_t>(FlagOffset));

    // Check status. Only response has valid status byte.
    EXPECT_EQ(20, buffer.peekBEInt<uint8_t>(StatusOffset));

    // Check request id.
    EXPECT_EQ(12345, buffer.peekBEInt<int64_t>(RequestIDOffset));

    // Check body size.
    EXPECT_EQ(8, buffer.peekBEInt<int32_t>(BodySizeOffset));
  }

  // Encode exception response
  {
    Buffer::OwnedImpl buffer;
    Metadata metadata;

    metadata.setMessageType(MessageType::Exception);
    metadata.setResponseStatus(ResponseStatus::BadRequest);
    metadata.setRequestId(12345);

    EXPECT_CALL(*raw_serializer, serializeRpcResponse(_, _, _))
        .WillOnce(testing::Invoke([](Buffer::Instance& buffer, const Metadata&,
                                     OptRef<const RpcResponse>) { buffer.add("anything"); }));

    codec.encodeEntireMessage(buffer, metadata, OptRef<const RpcResponse>{});

    EXPECT_EQ(DubboCodec::HeadersSize + 8, buffer.length());

    // Check magic number.
    EXPECT_EQ(MagicNumber, buffer.peekBEInt<uint16_t>());

    // Check flag.
    EXPECT_EQ(0x02, buffer.peekBEInt<uint8_t>(FlagOffset));

    // Check status. Only response has valid status byte.
    EXPECT_EQ(40, buffer.peekBEInt<uint8_t>(StatusOffset));

    // Check request id.
    EXPECT_EQ(12345, buffer.peekBEInt<int64_t>(RequestIDOffset));

    // Check body size.
    EXPECT_EQ(8, buffer.peekBEInt<int32_t>(BodySizeOffset));
  }

  // Encode exception response with ServerError
  {
    Buffer::OwnedImpl buffer;
    Metadata metadata;

    metadata.setMessageType(MessageType::Exception);
    metadata.setResponseStatus(ResponseStatus::ServerError);
    metadata.setRequestId(12345);

    EXPECT_CALL(*raw_serializer, serializeRpcResponse(_, _, _))
        .WillOnce(testing::Invoke([](Buffer::Instance& buffer, const Metadata&,
                                     OptRef<const RpcResponse>) { buffer.add("anything"); }));

    codec.encodeEntireMessage(buffer, metadata, OptRef<const RpcResponse>{});

    EXPECT_EQ(DubboCodec::HeadersSize + 8, buffer.length());

    // Check magic number.
    EXPECT_EQ(MagicNumber, buffer.peekBEInt<uint16_t>());

    // Check flag.
    EXPECT_EQ(0x02, buffer.peekBEInt<uint8_t>(FlagOffset));

    // Check status. Only response has valid status byte.
    EXPECT_EQ(80, buffer.peekBEInt<uint8_t>(StatusOffset));

    // Check request id.
    EXPECT_EQ(12345, buffer.peekBEInt<int64_t>(RequestIDOffset));

    // Check body size.
    EXPECT_EQ(8, buffer.peekBEInt<int32_t>(BodySizeOffset));
  }

  // Encode heartbeat response
  {
    Buffer::OwnedImpl buffer;
    Metadata metadata;

    metadata.setMessageType(MessageType::HeartbeatResponse);
    metadata.setResponseStatus(ResponseStatus::Ok);
    metadata.setRequestId(12345);

    EXPECT_CALL(*raw_serializer, serializeRpcResponse(_, _, _))
        .WillOnce(testing::Invoke([](Buffer::Instance& buffer, const Metadata&,
                                     OptRef<const RpcResponse>) { buffer.add("N"); }));

    codec.encodeEntireMessage(buffer, metadata, OptRef<const RpcResponse>{});

    EXPECT_EQ(DubboCodec::HeadersSize + 1, buffer.length());

    // Check magic number.
    EXPECT_EQ(MagicNumber, buffer.peekBEInt<uint16_t>());

    // Check flag.
    EXPECT_EQ(0x22, buffer.peekBEInt<uint8_t>(FlagOffset));

    // Check status. Only response has valid status byte.
    EXPECT_EQ(20, buffer.peekBEInt<uint8_t>(StatusOffset));

    // Check request id.
    EXPECT_EQ(12345, buffer.peekBEInt<int64_t>(RequestIDOffset));

    // Check body size.
    EXPECT_EQ(1, buffer.peekBEInt<int32_t>(BodySizeOffset));
  }

  // Encode unexpected message type will cause exit.
  {
    Buffer::OwnedImpl buffer;
    Metadata metadata;

    metadata.setMessageType(static_cast<MessageType>(6));
    metadata.setResponseStatus(ResponseStatus::Ok);
    metadata.setRequestId(12345);

    EXPECT_DEATH(codec.encodeEntireMessage(buffer, metadata, OptRef<const RpcResponse>{}),
                 ".*panic: corrupted enum.*");
  }
}

TEST(DubboCodecTest, EncodeHeaderForTestTest) {
  DubboCodec codec;

  // Encode unexpected message type will cause exit.
  {
    Buffer::OwnedImpl buffer;

    Metadata metadata;
    metadata.setMessageType(static_cast<MessageType>(6));
    metadata.setResponseStatus(ResponseStatus::Ok);
    metadata.setRequestId(12345);

    EXPECT_DEATH(codec.encodeHeaderForTest(buffer, metadata), ".*panic: corrupted enum.*");
  }
}

TEST(DirectResponseUtilTest, DirectResponseUtilTest) {
  // Local normal response.
  {

    auto response = DirectResponseUtil::localResponse(absl::OkStatus(), "anything");

    EXPECT_EQ(RpcResponseType::ResponseValueWithAttachments, response->responseType().value());
    EXPECT_EQ("anything", response->content().result()->toString().value().get());
    EXPECT_EQ("ok", response->content().attachments().at("envoy-local-response-status"));
  }

  // Local normal response without response type.
  {
    auto response = DirectResponseUtil::localResponse(absl::InvalidArgumentError("error-status"),
                                                      "error-message");

    EXPECT_EQ(RpcResponseType::ResponseWithExceptionWithAttachments,
              response->responseType().value());
    EXPECT_EQ("error-message", response->content().result()->toString().value().get());
    EXPECT_EQ("error-status", response->content().attachments().at("envoy-local-response-status"));
  }
}

} // namespace
} // namespace Dubbo
} // namespace Common
} // namespace Extensions
} // namespace Envoy
