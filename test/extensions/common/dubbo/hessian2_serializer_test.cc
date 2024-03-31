#include <memory>

#include "source/extensions/common/dubbo/hessian2_serializer.h"

#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"
#include "hessian2/object.hpp"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Dubbo {
namespace {

TEST(Hessian2ProtocolTest, Type) {
  Hessian2SerializerImpl serializer;
  EXPECT_EQ(SerializeType::Hessian2, serializer.type());
}

TEST(Hessian2ProtocolTest, deserializeRpcRequest) {
  Hessian2SerializerImpl serializer;

  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({
        0x05, '2', '.', '0', '.', '2', // Dubbo version
        0x04, 't', 'e', 's', 't',      // Service name
        0x05, '0', '.', '0', '.', '0', // Service version
        0x04, 't', 'e', 's', 't',      // method name
    }));
    Metadata metadata;
    metadata.setBodySize(buffer.length());
    auto result_or = serializer.deserializeRpcRequest(buffer, metadata);
    EXPECT_TRUE(result_or.ok());
    auto result = std::move(result_or).value();

    EXPECT_EQ("test", result->method());
    EXPECT_EQ("test", result->service());
    EXPECT_EQ("0.0.0", result->serviceVersion());
  }

  // incorrect body size
  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({
        0x05, '2', '.', '0', '.', '2', // Dubbo version
        0x04, 't', 'e', 's', 't',      // Service name
        0x05, '0', '.', '0', '.', '0', // Service version
        0x04, 't', 'e', 's', 't',      // method name
    }));
    std::string error_string = fmt::format("RpcRequest size({}) larger than body size({})",
                                           buffer.length(), buffer.length() - 1);
    Metadata metadata;
    metadata.setBodySize(buffer.length() - 1);
    auto result_or = serializer.deserializeRpcRequest(buffer, metadata);
    EXPECT_FALSE(result_or.ok());
    EXPECT_EQ(error_string, result_or.status().message());
  }

  // Missing key metadata.
  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({
        0x05, '2', '.', '0', '.', '2', // Dubbo version
        0x04, 't', 'e', 's', 't',      // Service name
        0x05, '0', '.', '0', '.', '0', // Service version
    }));
    Metadata metadata;
    metadata.setBodySize(buffer.length());

    auto result_or = serializer.deserializeRpcRequest(buffer, metadata);
    EXPECT_FALSE(result_or.ok());
    EXPECT_EQ("RpcRequest has no request metadata", result_or.status().message());
  }
}

TEST(Hessian2ProtocolTest, deserializeRpcRequestWithParametersOrAttachment) {
  Hessian2SerializerImpl serializer;

  Hessian2::Object::UntypedMap untyped_map;
  untyped_map.emplace(Hessian2::ObjectPtr{new Hessian2::StringObject("test1")},
                      Hessian2::ObjectPtr{new Hessian2::StringObject("test_value1")});
  untyped_map.emplace(Hessian2::ObjectPtr{new Hessian2::StringObject("test2")},
                      Hessian2::ObjectPtr{new Hessian2::StringObject("test_value2")});
  untyped_map.emplace(Hessian2::ObjectPtr{new Hessian2::StringObject("test3")},
                      Hessian2::ObjectPtr{new Hessian2::StringObject("test_value3")});

  auto map_object = std::make_unique<Hessian2::UntypedMapObject>(std::move(untyped_map));

  ArgumentVec params;

  params.push_back(std::make_unique<Hessian2::StringObject>("test_string"));

  std::vector<uint8_t> test_binary{0, 1, 2, 3, 4};
  params.push_back(std::make_unique<Hessian2::BinaryObject>(test_binary));

  params.push_back(std::make_unique<Hessian2::LongObject>(233333));

  // 4 parameters. Some times we will encode attachment as a map type parameter for test.
  std::string parameters_type = "Ljava.lang.String;[BJLjava.util.Map;";

  // Test for heartbeat request.
  {
    Buffer::OwnedImpl buffer;
    buffer.add("anything_here_for_heartbeat");

    Metadata metadata;
    metadata.setBodySize(buffer.length());
    metadata.setMessageType(MessageType::HeartbeatRequest);

    auto result_or = serializer.deserializeRpcRequest(buffer, metadata);
    EXPECT_TRUE(result_or.ok());
    auto result = std::move(result_or).value();

    EXPECT_EQ(nullptr, result);

    EXPECT_EQ(0, buffer.length());
  }

  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({
        0x05, '2', '.', '0', '.', '2', // Dubbo version
        0x04, 't', 'e', 's', 't',      // Service name
        0x05, '0', '.', '0', '.', '0', // Service version
        0x04, 't', 'e', 's', 't',      // method name
    }));

    Hessian2::Encoder encoder(std::make_unique<BufferWriter>(buffer));

    encoder.encode<std::string>(parameters_type);

    for (const auto& param : params) {
      encoder.encode<Hessian2::Object>(*param);
    }
    // Encode an untyped map object as fourth parameter.
    encoder.encode<Hessian2::Object>(*map_object);

    // Encode attachment
    encoder.encode<Hessian2::Object>(*map_object);

    Metadata metadata;
    metadata.setBodySize(buffer.length());

    auto result_or = serializer.deserializeRpcRequest(buffer, metadata);
    EXPECT_TRUE(result_or.ok());
    auto result = std::move(result_or).value();

    EXPECT_NE(nullptr, result);

    // All data be moved to buffer in the request.
    EXPECT_EQ(0, buffer.length());

    auto& result_params = result->content().arguments();

    EXPECT_EQ(4, result_params.size());

    EXPECT_EQ("test_string", result_params.at(0)->toString().value().get());
    EXPECT_EQ(4, result_params.at(1)->toBinary().value().get().at(4));
    EXPECT_EQ(233333, *result_params.at(2)->toLong());
    EXPECT_EQ(3, result_params.at(3)->toUntypedMap().value().get().size());
    EXPECT_EQ("test_value2", result_params.at(3)
                                 ->toUntypedMap()
                                 .value()
                                 .get()
                                 .find("test2")
                                 ->second->toString()
                                 .value()
                                 .get());

    EXPECT_EQ("test_value2", result->content().attachments().at("test2"));
  }

  // Test case that request only have parameters.
  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({
        0x05, '2', '.', '0', '.', '2', // Dubbo version
        0x04, 't', 'e', 's', 't',      // Service name
        0x05, '0', '.', '0', '.', '0', // Service version
        0x04, 't', 'e', 's', 't',      // method name
    }));

    Hessian2::Encoder encoder(std::make_unique<BufferWriter>(buffer));

    encoder.encode<std::string>(parameters_type);

    for (const auto& param : params) {
      encoder.encode<Hessian2::Object>(*param);
    }
    // Encode an untyped map object as fourth parameter.
    encoder.encode<Hessian2::Object>(*map_object);

    Metadata metadata;
    metadata.setBodySize(buffer.length());

    auto result_or = serializer.deserializeRpcRequest(buffer, metadata);
    EXPECT_TRUE(result_or.ok());
    auto result = std::move(result_or).value();

    EXPECT_NE(nullptr, result);

    EXPECT_EQ(4, result->content().arguments().size());
    EXPECT_EQ(true, result->content().attachments().empty());
  }
  // Test the case where there are not enough parameters in the request buffer.
  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({
        0x05, '2', '.', '0', '.', '2', // Dubbo version
        0x04, 't', 'e', 's', 't',      // Service name
        0x05, '0', '.', '0', '.', '0', // Service version
        0x04, 't', 'e', 's', 't',      // method name
    }));

    Hessian2::Encoder encoder(std::make_unique<BufferWriter>(buffer));

    encoder.encode<std::string>(parameters_type);

    // There are actually only three parameters in the request.
    for (const auto& param : params) {
      encoder.encode<Hessian2::Object>(*param);
    }

    Metadata metadata;
    metadata.setBodySize(buffer.length());

    auto result_or = serializer.deserializeRpcRequest(buffer, metadata);
    EXPECT_TRUE(result_or.ok());
    auto result = std::move(result_or).value();
    EXPECT_NE(nullptr, result);

    // The request will be reset to an empty state.
    EXPECT_EQ(true, result->content().arguments().empty());
    EXPECT_EQ(true, result->content().attachments().empty());
  }
}

TEST(Hessian2ProtocolTest, deserializeRpcResponse) {
  Hessian2SerializerImpl serializer;

  // Test for heartbeat response.
  {
    Buffer::OwnedImpl buffer;
    buffer.add("anything_here_for_heartbeat");

    Metadata metadata;
    metadata.setBodySize(buffer.length());
    metadata.setMessageType(MessageType::HeartbeatResponse);
    metadata.setResponseStatus(ResponseStatus::Ok);

    auto result_or = serializer.deserializeRpcRequest(buffer, metadata);
    EXPECT_TRUE(result_or.ok());
    auto result = std::move(result_or).value();
    EXPECT_EQ(nullptr, result);

    EXPECT_EQ(0, buffer.length());
  }

  // The first element by of normal response should response type.
  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({
        0x04,
        't',
        'e',
        's',
        't',
    }));

    Metadata metadata;
    metadata.setMessageType(MessageType::Response);
    metadata.setResponseStatus(ResponseStatus::Ok);
    metadata.setBodySize(buffer.length());

    auto result_or = serializer.deserializeRpcRequest(buffer, metadata);
    EXPECT_FALSE(result_or.ok());

    EXPECT_EQ("Cannot parse RpcResponse type from buffer", result_or.status().message());
  }

  // If a response is set to type `Exception` before calling `deserializeRpcRequest`, then
  // it must be a non-Ok request and the response type would absent.
  {

    Buffer::OwnedImpl buffer;
    buffer.add(std::string({
        0x04,
        't',
        'e',
        's',
        't',
    }));

    Metadata metadata;
    metadata.setMessageType(MessageType::Exception);
    metadata.setResponseStatus(ResponseStatus::BadResponse);
    metadata.setBodySize(buffer.length());

    auto result_or = serializer.deserializeRpcResponse(buffer, metadata);
    EXPECT_TRUE(result_or.ok());
    auto result = std::move(result_or).value();

    EXPECT_NE(nullptr, result);
    EXPECT_EQ(0, buffer.length());
  }

  // Normal response.
  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({
        '\x94',                   // return type
        0x04, 't', 'e', 's', 't', // return body
    }));

    Metadata metadata;
    metadata.setMessageType(MessageType::Response);
    metadata.setResponseStatus(ResponseStatus::Ok);
    metadata.setBodySize(buffer.length());

    auto result_or = serializer.deserializeRpcResponse(buffer, metadata);
    EXPECT_TRUE(result_or.ok());
    auto result = std::move(result_or).value();

    EXPECT_NE(nullptr, result);

    EXPECT_EQ(RpcResponseType::ResponseValueWithAttachments, result->responseType().value());
    EXPECT_EQ(MessageType::Response, metadata.messageType());
  }

  // Exception response.
  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({
        '\x93',                   // return type
        0x04, 't', 'e', 's', 't', // return body
    }));

    Metadata metadata;
    metadata.setMessageType(MessageType::Response);
    metadata.setResponseStatus(ResponseStatus::Ok);
    metadata.setBodySize(buffer.length());

    auto result_or = serializer.deserializeRpcResponse(buffer, metadata);
    EXPECT_TRUE(result_or.ok());
    auto result = std::move(result_or).value();

    EXPECT_NE(nullptr, result);

    EXPECT_EQ(RpcResponseType::ResponseWithExceptionWithAttachments,
              result->responseType().value());
    // The message type will be set to exception if there is response with exception.
    EXPECT_EQ(MessageType::Exception, metadata.messageType());
  }

  // Exception response.
  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({
        '\x90',                   // return type
        0x04, 't', 'e', 's', 't', // return body
    }));

    Metadata metadata;
    metadata.setMessageType(MessageType::Response);
    metadata.setResponseStatus(ResponseStatus::Ok);
    metadata.setBodySize(buffer.length());

    auto result_or = serializer.deserializeRpcResponse(buffer, metadata);
    EXPECT_TRUE(result_or.ok());
    auto result = std::move(result_or).value();

    EXPECT_NE(nullptr, result);

    EXPECT_EQ(RpcResponseType::ResponseWithException, result->responseType().value());
    EXPECT_EQ(MessageType::Exception, metadata.messageType());
  }

  // Normal response.
  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({
        '\x91',                   // return type
        0x04, 't', 'e', 's', 't', // return body
    }));

    Metadata metadata;
    metadata.setMessageType(MessageType::Response);
    metadata.setResponseStatus(ResponseStatus::Ok);
    metadata.setBodySize(buffer.length());

    auto result_or = serializer.deserializeRpcResponse(buffer, metadata);
    EXPECT_TRUE(result_or.ok());
    auto result = std::move(result_or).value();

    EXPECT_NE(nullptr, result);

    EXPECT_EQ(RpcResponseType::ResponseWithValue, result->responseType().value());
    EXPECT_EQ(MessageType::Response, metadata.messageType());
  }

  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({
        '\x95', // return type
        'H',    // return attachment
        0x03,
        'k',
        'e',
        'y',
        0x05,
        'v',
        'a',
        'l',
        'u',
        'e',
        'Z',
    }));

    Metadata metadata;
    metadata.setMessageType(MessageType::Response);
    metadata.setResponseStatus(ResponseStatus::Ok);
    metadata.setBodySize(buffer.length());

    auto result_or = serializer.deserializeRpcResponse(buffer, metadata);
    EXPECT_TRUE(result_or.ok());
    auto result = std::move(result_or).value();

    EXPECT_NE(nullptr, result);

    EXPECT_EQ(RpcResponseType::ResponseNullValueWithAttachments, result->responseType().value());
    EXPECT_EQ(MessageType::Response, metadata.messageType());
  }

  // Incorrect body size
  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({
        '\x94',                   // return type
        0x04, 't', 'e', 's', 't', // return body
    }));

    Metadata metadata;
    metadata.setMessageType(MessageType::Response);
    metadata.setResponseStatus(ResponseStatus::Ok);
    metadata.setBodySize(0);

    auto result_or = serializer.deserializeRpcResponse(buffer, metadata);
    EXPECT_FALSE(result_or.ok());
    EXPECT_EQ("RpcResponse size(1) large than body size(0)", result_or.status().message());
  }

  // Incorrect return type
  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({
        '\x96',                   // incorrect return type
        0x04, 't', 'e', 's', 't', // return body
    }));

    Metadata metadata;
    metadata.setMessageType(MessageType::Response);
    metadata.setResponseStatus(ResponseStatus::Ok);
    metadata.setBodySize(buffer.length());

    auto result_or = serializer.deserializeRpcResponse(buffer, metadata);
    EXPECT_FALSE(result_or.ok());
    EXPECT_EQ("not supported return type 6", result_or.status().message());
  }
}

TEST(Hessian2ProtocolTest, SerializeRpcRequest) {
  Hessian2SerializerImpl serializer;

  // Heartbeat request.
  {
    Metadata metadata;
    metadata.setMessageType(MessageType::HeartbeatRequest);

    Buffer::OwnedImpl buffer;
    serializer.serializeRpcRequest(buffer, metadata, {});

    EXPECT_EQ(1, buffer.length());
    EXPECT_EQ("N", buffer.toString());
  }

  // Normal request.
  {
    Metadata metadata;
    metadata.setMessageType(MessageType::Request);

    auto request = std::make_unique<RpcRequest>("v", "v", "v", "v");

    ArgumentVec args;
    args.push_back(std::make_unique<Hessian2::BooleanObject>(true));

    request->content().initialize("Z", std::move(args), {});

    Buffer::OwnedImpl buffer;
    serializer.serializeRpcRequest(buffer, metadata, *request);

    EXPECT_EQ(
        std::string({'\x1', 'v', '\x1', 'v', '\x1', 'v', '\x1', 'v', '\x1', 'Z', 'T', 'H', 'Z'}),
        buffer.toString());
  }

  // Normal request with attachment update.
  {

    Metadata metadata;
    metadata.setMessageType(MessageType::Request);

    auto request = std::make_unique<RpcRequest>("v", "v", "v", "v");

    ArgumentVec args;
    args.push_back(std::make_unique<Hessian2::BooleanObject>(true));

    request->content().initialize("Z", std::move(args), {});
    request->content().setAttachment("key", "value");

    Buffer::OwnedImpl buffer;
    serializer.serializeRpcRequest(buffer, metadata, *request);

    EXPECT_EQ(true, absl::StrContains(buffer.toString(), "value"));
  }
}

TEST(Hessian2ProtocolTest, serializeRpcResponse) {
  Hessian2SerializerImpl serializer;

  // Heartbeat response.
  {
    Metadata metadata;
    metadata.setMessageType(MessageType::HeartbeatResponse);
    metadata.setResponseStatus(ResponseStatus::Ok);

    Buffer::OwnedImpl buffer;
    serializer.serializeRpcResponse(buffer, metadata, {});

    EXPECT_EQ(1, buffer.length());
    EXPECT_EQ("N", buffer.toString());
  }

  // Normal response.
  {
    Metadata metadata;
    metadata.setMessageType(MessageType::Response);
    metadata.setResponseStatus(ResponseStatus::Ok);

    auto response = std::make_unique<RpcResponse>();
    response->setResponseType(RpcResponseType::ResponseWithValue);

    Buffer::OwnedImpl response_content;
    response_content.writeByte('\x08');
    response_content.add("anything");
    response->content().initialize(response_content, 9);

    Buffer::OwnedImpl buffer;
    serializer.serializeRpcResponse(buffer, metadata, *response);

    // The data in message buffer will be used directly for normal response.
    EXPECT_EQ("anything", buffer.toString().substr(2));
  }
}

} // namespace
} // namespace Dubbo
} // namespace Common
} // namespace Extensions
} // namespace Envoy
