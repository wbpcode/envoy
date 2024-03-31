#include "contrib/generic_proxy/filters/network/source/codecs/dubbo/config.h"

#include <memory>

#include "envoy/registry/registry.h"

#include "source/extensions/common/dubbo/message.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace GenericProxy {
namespace Codec {
namespace Dubbo {

namespace {

constexpr absl::string_view VERSION_KEY = "version";

} // namespace

void DubboRequest::forEach(IterateCallback callback) const {
  for (const auto& [key, val] : message_->content().attachments()) {
    if (!callback(key, val)) {
      break;
    }
  }
}

absl::optional<absl::string_view> DubboRequest::get(absl::string_view key) const {
  if (key == VERSION_KEY) {
    return message_->serviceVersion();
  }

  auto it = message_->content().attachments().find(key);
  if (it == message_->content().attachments().end()) {
    return absl::nullopt;
  }

  return absl::string_view{it->second};
}

void DubboRequest::set(absl::string_view key, absl::string_view val) {
  message_->content().setAttachment(key, val);
}

void DubboRequest::erase(absl::string_view key) { message_->content().delAttachment(key); }

void DubboResponse::refreshStatus() {

  using Common::Dubbo::RpcResponseType;

  const auto status = metadata_.responseStatus();
  const auto optional_type = message_->responseType();

  if (status != Common::Dubbo::ResponseStatus::Ok) {
    status_ = StreamStatus(static_cast<uint8_t>(status), false);
    return;
  }

  bool message_ok = true;

  // The final status is not ok if the response type is ResponseWithException or
  // ResponseWithExceptionWithAttachments even if the response status is Ok.
  ASSERT(optional_type.has_value());
  auto type = optional_type.value_or(RpcResponseType::ResponseWithException);
  if (type == RpcResponseType::ResponseWithException ||
      type == RpcResponseType::ResponseWithExceptionWithAttachments) {
    message_ok = false;
  }

  status_ = StreamStatus(static_cast<uint8_t>(status), message_ok);
}

DubboCodecBase::DubboCodecBase(Common::Dubbo::DubboCodec codec) : codec_(std::move(codec)) {}

ResponsePtr DubboServerCodec::respond(Status status, absl::string_view data,
                                      const Request& origin_request) {
  const auto* typed_request = dynamic_cast<const DubboRequest*>(&origin_request);
  ASSERT(typed_request != nullptr);

  auto response = Common::Dubbo::DirectResponseUtil::localResponse(status, data);
  ASSERT(response != nullptr);

  Common::Dubbo::Metadata metadata;
  metadata.setResponseStatus(Common::Dubbo::ResponseStatus::Ok);
  metadata.setRequestId(typed_request->metadata_.requestId());
  metadata.setMessageType(Common::Dubbo::MessageType::Response);

  return std::make_unique<DubboResponse>(metadata, std::move(response));
}

CodecFactoryPtr
DubboCodecFactoryConfig::createCodecFactory(const Protobuf::Message&,
                                            Envoy::Server::Configuration::ServerFactoryContext&) {
  return std::make_unique<DubboCodecFactory>();
}

REGISTER_FACTORY(DubboCodecFactoryConfig, CodecFactoryConfig);

} // namespace Dubbo
} // namespace Codec
} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
