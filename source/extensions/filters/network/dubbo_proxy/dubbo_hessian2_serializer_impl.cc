#include "extensions/filters/network/dubbo_proxy/dubbo_hessian2_serializer_impl.h"

#include "envoy/common/exception.h"

#include "common/common/assert.h"
#include "common/common/macros.h"

#include "extensions/filters/network/dubbo_proxy/hessian_utils.h"
#include "extensions/filters/network/dubbo_proxy/message_impl.h"
#include "extensions/filters/network/dubbo_proxy/serializer.h"
#include "extensions/filters/network/dubbo_proxy/serializer_impl.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {

std::pair<RpcInvocationSharedPtr, bool>
DubboHessian2SerializerImpl::deserializeRpcInvocation(Buffer::Instance& buffer,
                                                      ContextSharedPtr context) {
  // TODO(zyfjeff): Add format checker
  Hessian2::Decoder decoder(std::make_unique<BufferReader>(buffer));

  auto dubbo_version = decoder.decode<std::string>();
  auto service_name = decoder.decode<std::string>();
  auto service_version = decoder.decode<std::string>();
  auto method_name = decoder.decode<std::string>();

  size_t total_size = decoder.offset();
  if (static_cast<uint64_t>(context->bodySize()) < decoder.offset()) {
    throw EnvoyException(fmt::format("RpcInvocation size({}) large than body size({})", total_size,
                                     context->bodySize()));
  }

  if (!dubbo_version || !service_name || !service_version || !method_name) {
    throw EnvoyException(fmt::format("RpcInvocation has no request metadata"));
  }

  auto invo = std::make_shared<RpcInvocationImpl>();
  invo->setServiceName(*service_name);
  invo->setServiceVersion(*service_version);
  invo->setMethodName(*method_name);

  // TODO(wbpcode): Lazy parsing of parameters and attachments will be supported later to improve
  // performance.
  if (auto types = decoder.decode<std::string>(); types && !types->empty()) {
    auto parameters_detail = invo->parametersDetail();
    auto parameters_number = HessianUtils::getParametersNumber(*types);

    parameters_detail.before_ = decoder.offset();
    parameters_detail.number_ = parameters_number;

    auto parameters = invo->mutableParameters();
    for (uint32_t i = 0; i < parameters_number; i++) {
      auto offset = decoder.offset();
      auto result = decoder.decode<Hessian2::Object>();

      if (!result) {
        throw EnvoyException("Cannot parse RpcInvocation parameter from buffer");
      }

      parameters_detail.detail_.push_back(decoder.offset() - offset);
      parameters.push_back(std::move(result));
    }
  }

  if (decoder.offset() < buffer.length()) {
    // Try to parse attachment of dubbo message.
    auto result = decoder.decode<Hessian2::Object>();
    if (result && result->type() == Hessian2::Object::Type::UntypedMap) {
      auto attachment = invo->mutableAttachment();
      for (const auto& pair : result->asType<Hessian2::UntypedMapObject>().data_) {
        if (!pair.first || !pair.second) {
          continue;
        }
        auto [key, value] = {pair.first->toString(), pair.second->toString()};
        if (!key.has_value() || !value.has_value()) {
          continue;
        }
        attachment.insert(key.value(), value.value());
      }
      auto group = attachment.lookup("group");
      if (group) {
        invo->setServiceGroup(*group);
      }
    }
  }

  return std::pair<RpcInvocationSharedPtr, bool>(invo, true);
}

std::pair<RpcResultSharedPtr, bool>
DubboHessian2SerializerImpl::deserializeRpcResult(Buffer::Instance& buffer,
                                                  ContextSharedPtr context) {
  ASSERT(buffer.length() >= context->bodySize());
  bool has_value = true;

  auto result = std::make_shared<RpcResultImpl>();

  Hessian2::Decoder decoder(std::make_unique<BufferReader>(buffer));
  auto type_value = decoder.decode<int32_t>();
  if (!type_value) {
    throw EnvoyException(fmt::format("Cannot parse RpcResult type from buffer"));
  }

  RpcResponseType type = static_cast<RpcResponseType>(*type_value);

  switch (type) {
  case RpcResponseType::ResponseWithException:
  case RpcResponseType::ResponseWithExceptionWithAttachments:
    result->setException(true);
    break;
  case RpcResponseType::ResponseWithNullValue:
  case RpcResponseType::ResponseNullValueWithAttachments:
    has_value = false;
    FALLTHRU;
  case RpcResponseType::ResponseWithValue:
  case RpcResponseType::ResponseValueWithAttachments:
    result->setException(false);
    break;
  default:
    throw EnvoyException(fmt::format("not supported return type {}", static_cast<uint8_t>(type)));
  }

  size_t total_size = decoder.offset();

  if (context->bodySize() < total_size) {
    throw EnvoyException(fmt::format("RpcResult size({}) large than body size({})", total_size,
                                     context->bodySize()));
  }

  if (!has_value && context->bodySize() != total_size) {
    throw EnvoyException(
        fmt::format("RpcResult is no value, but the rest of the body size({}) not equal 0",
                    (context->bodySize() - total_size)));
  }

  return std::pair<RpcResultSharedPtr, bool>(result, true);
}

size_t DubboHessian2SerializerImpl::serializeRpcResult(Buffer::Instance& output_buffer,
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
