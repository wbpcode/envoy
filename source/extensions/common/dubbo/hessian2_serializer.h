#pragma once

#include "source/extensions/common/dubbo/message.h"
#include "source/extensions/common/dubbo/metadata.h"
#include "source/extensions/common/dubbo/serializer.h"

#include "envoy/common/optref.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Dubbo {

class Hessian2SerializerImpl : public Serializer, Logger::Loggable<Logger::Id::dubbo> {
public:
  SerializeType type() const override { return SerializeType::Hessian2; }

  absl::StatusOr<RpcRequestPtr> deserializeRpcRequest(Buffer::Instance& buffer,
                                                      Metadata& metadata) const override;
  absl::StatusOr<RpcResponsePtr> deserializeRpcResponse(Buffer::Instance& buffer,
                                                        Metadata& metadata) const override;

  void serializeRpcRequest(Buffer::Instance& buffer, const Metadata& metadata,
                           OptRef<const RpcRequest> request) const override;
  void serializeRpcResponse(Buffer::Instance& buffer, const Metadata& metadata,
                            OptRef<const RpcResponse> response) const override;
};

} // namespace Dubbo
} // namespace Common
} // namespace Extensions
} // namespace Envoy
