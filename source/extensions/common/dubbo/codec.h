#pragma once

#include "source/extensions/common/dubbo/message.h"
#include "source/extensions/common/dubbo/metadata.h"
#include "source/extensions/common/dubbo/hessian2_serializer.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Dubbo {

enum DecodeStatus {
  Success,
  Failure,
  Waiting,
};

class DubboCodec;
using DubboCodecPtr = std::unique_ptr<DubboCodec>;
using OptMetadata = absl::optional<Metadata>;

class DubboCodec : public Logger::Loggable<Logger::Id::dubbo> {
public:
  static DubboCodec codecFromSerializeType(SerializeType type);

  void initilize(SerializerPtr serializer) { serializer_ = std::move(serializer); }

  const Serializer& serializer() const { return *serializer_; }

  // Decode header metadata only from the buffer.
  DecodeStatus decodeHeader(Buffer::Instance& buffer, OptMetadata& output_metadata);

  // Decode the request body from the buffer.
  DecodeStatus decodeData(Buffer::Instance& buffer, Metadata& metadata,
                          RpcRequestPtr& output_request);
  // Decode the response body from the buffer.
  DecodeStatus decodeData(Buffer::Instance& buffer, Metadata& metadata,
                          RpcResponsePtr& output_response);

  // Encode the entire request message (header and request body) to the buffer.
  void encodeEntireMessage(Buffer::Instance& buffer, const Metadata& metadata,
                           OptRef<const RpcRequest> request);

  // Encode the entire response message (header and response body) to the buffer.
  void encodeEntireMessage(Buffer::Instance& buffer, const Metadata& metadata,
                           OptRef<const RpcResponse> response);

  // Encode header only. In most cases, the 'encode' should be used and this method
  // just used for test.
  void encodeHeaderForTest(Buffer::Instance& buffer, Metadata& metadata);

  static constexpr uint8_t HeadersSize = 16;
  static constexpr int32_t MaxBodySize = 16 * 1024 * 1024;

private:
  SerializerPtr serializer_{};
};

class DirectResponseUtil {
public:
  static RpcResponsePtr localResponse(absl::Status status, absl::string_view content);
};

} // namespace Dubbo
} // namespace Common
} // namespace Extensions
} // namespace Envoy
