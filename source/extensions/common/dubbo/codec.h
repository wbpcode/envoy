#pragma once

#include "metadata.h"
#include "source/extensions/common/dubbo/serializer.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {

enum DecodeStatus {
  Success,
  Failure,
  Waiting,
};

class DubboCodec {
public:
  DecodeStatus decodeHeader(Buffer::Instance& buffer, MessageMetadata& metadata);

  DecodeStatus decodeData(Buffer::Instance& buffer, MessageMetadata& metadata);

  void encode(Buffer::Instance& buffer, MessageMetadata& metadata);

  void initilize(SerializerPtr serializer) { serializer_ = std::move(serializer); }

  static constexpr uint8_t HeadersSize = 16;
  static constexpr int32_t MaxBodySize = 16 * 1024 * 1024;

private:
  SerializerPtr serializer_{};
};

} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
