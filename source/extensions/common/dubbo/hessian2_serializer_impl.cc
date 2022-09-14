#include "source/extensions/filters/network/dubbo_proxy/dubbo_hessian2_serializer_impl.h"

#include "envoy/common/exception.h"

#include "source/common/common/assert.h"
#include "source/common/common/macros.h"
#include "source/extensions/common/dubbo/hessian2_utils.h"
#include "source/extensions/common/dubbo/message_impl.h"

#include "hessian2/object.hpp"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {

std::pair<RpcRequestSharedPtr, bool>
DubboHessian2SerializerImpl::deserializeRpcRequest(Buffer::Instance& buffer,
                                                      ContextSharedPtr context) {
  Hessian2::Decoder decoder(std::make_unique<BufferReader>(buffer));

  // TODO(zyfjeff): Add format checker
  auto dubbo_version = decoder.decode<std::string>();
  auto service_name = decoder.decode<std::string>();
  auto service_version = decoder.decode<std::string>();
  auto method_name = decoder.decode<std::string>();

  if (context->bodySize() < decoder.offset()) {
    throw EnvoyException(fmt::format("RpcRequest size({}) larger than body size({})",
                                     decoder.offset(), context->bodySize()));
  }

  if (dubbo_version == nullptr || service_name == nullptr || service_version == nullptr ||
      method_name == nullptr) {
    throw EnvoyException(fmt::format("RpcRequest has no request metadata"));
  }

  auto invo = std::make_shared<RpcRequestImpl>();
  invo->setServiceName(*service_name);
  invo->setServiceVersion(*service_version);
  invo->setMethodName(*method_name);

  size_t parsed_size = context->headerSize() + decoder.offset();

  auto delayed_decoder = std::make_shared<Hessian2::Decoder>(
      std::make_unique<BufferReader>(context->originMessage(), parsed_size));

  invo->setParametersLazyCallback([delayed_decoder]() -> RpcRequestImpl::ParametersPtr {
    auto params = std::make_unique<RpcRequestImpl::Parameters>();

    if (auto types = delayed_decoder->decode<std::string>(); types != nullptr && !types->empty()) {
      uint32_t number = HessianUtils::getParametersNumber(*types);
      for (uint32_t i = 0; i < number; i++) {
        if (auto result = delayed_decoder->decode<Hessian2::Object>(); result != nullptr) {
          params->push_back(std::move(result));
        } else {
          throw EnvoyException("Cannot parse RpcRequest parameter from buffer");
        }
      }
    }
    return params;
  });

  invo->setAttachmentLazyCallback([delayed_decoder]() -> RpcRequestImpl::AttachmentPtr {
    size_t offset = delayed_decoder->offset();

    auto result = delayed_decoder->decode<Hessian2::Object>();
    if (result != nullptr && result->type() == Hessian2::Object::Type::UntypedMap) {
      return std::make_unique<RpcRequestImpl::Attachment>(
          RpcRequestImpl::Attachment::MapPtr{
              dynamic_cast<RpcRequestImpl::Attachment::Map*>(result.release())},
          offset);
    } else {
      return std::make_unique<RpcRequestImpl::Attachment>(
          std::make_unique<RpcRequestImpl::Attachment::Map>(), offset);
    }
  });

  return std::pair<RpcRequestSharedPtr, bool>(invo, true);
}

std::pair<RpcResponseSharedPtr, bool>
DubboHessian2SerializerImpl::deserializeRpcResponse(Buffer::Instance& buffer,
                                                  ContextSharedPtr context) {
  ASSERT(buffer.length() >= context->bodySize());
  bool has_value = true;

  auto result = std::make_shared<RpcResponseImpl>();

  Hessian2::Decoder decoder(std::make_unique<BufferReader>(buffer));
  auto type_value = decoder.decode<int32_t>();
  if (type_value == nullptr) {
    throw EnvoyException(fmt::format("Cannot parse RpcResponse type from buffer"));
  }

  RpcResponseType type = static_cast<RpcResponseType>(*type_value);

  switch (type) {
  case RpcResponseType::ResponseWithException:
  case RpcResponseType::ResponseWithExceptionWithAttachments:
    result->setException(true);
    break;
  case RpcResponseType::ResponseWithNullValue:
    has_value = false;
    FALLTHRU;
  case RpcResponseType::ResponseNullValueWithAttachments:
  case RpcResponseType::ResponseWithValue:
  case RpcResponseType::ResponseValueWithAttachments:
    result->setException(false);
    break;
  default:
    throw EnvoyException(fmt::format("not supported return type {}", static_cast<uint8_t>(type)));
  }

  size_t total_size = decoder.offset();

  if (context->bodySize() < total_size) {
    throw EnvoyException(fmt::format("RpcResponse size({}) large than body size({})", total_size,
                                     context->bodySize()));
  }

  if (!has_value && context->bodySize() != total_size) {
    throw EnvoyException(
        fmt::format("RpcResponse is no value, but the rest of the body size({}) not equal 0",
                    (context->bodySize() - total_size)));
  }

  return std::pair<RpcResponseSharedPtr, bool>(result, true);
}

size_t DubboHessian2SerializerImpl::serializeRpcResponse(Buffer::Instance& output_buffer,
                                                       const std::string& content,
                                                       RpcResponseType type) {
  size_t origin_length = output_buffer.length();
  Hessian2::Encoder encoder(std::make_unique<BufferWriter>(output_buffer));

  // The serialized response type is compact int.
  bool result = encoder.encode(static_cast<std::underlying_type<RpcResponseType>::type>(type));
  result |= encoder.encode(content);

  ASSERT(result);

  return output_buffer.length() - origin_length;
}

class DubboHessian2SerializerConfigFactory
    : public SerializerFactoryBase<DubboHessian2SerializerImpl> {
public:
  DubboHessian2SerializerConfigFactory()
      : SerializerFactoryBase(ProtocolType::Dubbo, SerializationType::Hessian2) {}
};

/**
 * Static registration for the Hessian protocol. @see RegisterFactory.
 */
REGISTER_FACTORY(DubboHessian2SerializerConfigFactory, NamedSerializerConfigFactory);

} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
