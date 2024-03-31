#pragma once

#include "source/extensions/common/dubbo/serializer.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Dubbo {

class MockSerializer : public Serializer {
public:
  MockSerializer();
  ~MockSerializer() override;

  // DubboProxy::Serializer
  MOCK_METHOD(SerializeType, type, (), (const));
  MOCK_METHOD(absl::StatusOr<RpcRequestPtr>, deserializeRpcRequest, (Buffer::Instance&, Metadata&),
              (const));
  MOCK_METHOD(absl::StatusOr<RpcResponsePtr>, deserializeRpcResponse,
              (Buffer::Instance&, Metadata&), (const));
  MOCK_METHOD(void, serializeRpcRequest,
              (Buffer::Instance&, const Metadata&, OptRef<const RpcRequest>), (const));
  MOCK_METHOD(void, serializeRpcResponse,
              (Buffer::Instance&, const Metadata&, OptRef<const RpcResponse>), (const));

  SerializeType type_{SerializeType::Hessian2};
};

} // namespace Dubbo
} // namespace Common
} // namespace Extensions
} // namespace Envoy
