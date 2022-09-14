#pragma once

#include "source/extensions/common/dubbo/message_impl.h"
#include "source/extensions/filters/network/dubbo_proxy/serializer.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {

class DubboHessian2SerializerImpl : public Serializer {
public:
  const std::string& name() const override {
    return ProtocolSerializerNames::get().fromType(ProtocolType::Dubbo, type());
  }
  SerializationType type() const override { return SerializationType::Hessian2; }

  std::pair<RpcRequestSharedPtr, bool>
  deserializeRpcRequest(Buffer::Instance& buffer, ContextSharedPtr context) override;

  std::pair<RpcResponseSharedPtr, bool> deserializeRpcResponse(Buffer::Instance& buffer,
                                                           ContextSharedPtr context) override;

  size_t serializeRpcResponse(Buffer::Instance& output_buffer, const std::string& content,
                            RpcResponseType type) override;
};

} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
