#pragma once

#include <string>

#include "envoy/buffer/buffer.h"
#include "envoy/config/typed_config.h"

#include "source/common/common/assert.h"
#include "source/common/config/utility.h"
#include "source/common/singleton/const_singleton.h"
#include "source/extensions/common/dubbo/message.h"
#include "source/extensions/common/dubbo/metadata.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Dubbo {

class Serializer {
public:
  virtual ~Serializer() = default;

  virtual SerializeType type() const PURE;

  /**
   * Deserialize rpc request.
   * @param buffer the currently buffered dubbo binary.
   * @param metadata metadata of the rpc request.
   * @return RpcRequestPtr the deserialized rpc request if deserialization is successful.
   */
  virtual absl::StatusOr<RpcRequestPtr> deserializeRpcRequest(Buffer::Instance& buffer,
                                                              Metadata& metadata) const PURE;

  /**
   * Deserialize rpc response.
   * @param buffer the currently buffered dubbo binary.
   * @param metadata metadata of the rpc response.
   * @return RpcResponsePtr the deserialized rpc response if deserialization is successful.
   */
  virtual absl::StatusOr<RpcResponsePtr> deserializeRpcResponse(Buffer::Instance& buffer,
                                                                Metadata& metadata) const PURE;

  /**
   * Serialize rpc request.
   * @param buffer the buffer to store the serialized binary.
   * @param metadata metadata of the rpc request.
   * @param request the rpc request to be serialized. May be empty for heartbeat.
   */
  virtual void serializeRpcRequest(Buffer::Instance& buffer, const Metadata& metadata,
                                   OptRef<const RpcRequest> request) const PURE;

  /**
   * Serialize rpc response.
   * @param buffer the buffer to store the serialized binary.
   * @param metadata metadata of the rpc response.
   * @param response the rpc response to be serialized. May be empty for heartbeat.
   */
  virtual void serializeRpcResponse(Buffer::Instance& buffer, const Metadata& metadata,
                                    OptRef<const RpcResponse> response) const PURE;
};

using SerializerPtr = std::unique_ptr<Serializer>;

} // namespace Dubbo
} // namespace Common
} // namespace Extensions
} // namespace Envoy
