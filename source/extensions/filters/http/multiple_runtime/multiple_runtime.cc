#include "source/extensions/filters/http/multiple_runtime/multiple_runtime.h"

#include "source/common/config/utility.h"
#include "source/common/http/headers.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace MultipleRuntime {

MultipleRuntimeConfig::MultipleRuntimeConfig(const ProtoConfig& config,
                                             Server::Configuration::FactoryContext& context)
    : default_namespace_(config.default_namespace()) {

  for (const auto& component : config.components()) {
    switch (component.type()) {
    case ComponentType::STATE_STORE: {

      auto& factory =
          Config::Utility::getAndCheckFactory<StateStoreInstanceFactory>(component.config());
      ProtobufTypes::MessagePtr typed_config = factory.createEmptyConfigProto();
      Envoy::Config::Utility::translateOpaqueConfig(
          component.config().typed_config(), context.messageValidationVisitor(), *typed_config);
      auto state_store_instance = factory.create(*typed_config, context);
      state_store_instances_.emplace(component.config().name(), state_store_instance);
      break;
    }
    default:
      break;
    }
  }
}

OptRef<StateStoreInstance> MultipleRuntimeConfig::getStateStoreInstance(absl::string_view name) {
  auto it = state_store_instances_.find(name);
  if (it == state_store_instances_.end()) {
    return absl::nullopt;
  }
  return makeOptRefFromPtr<StateStoreInstance>(it->second.get());
}

void MultipleRuntimeFilter::handleMultipleRuntimeRequest() {
  auto request_headers = decoder_callbacks_->requestHeaders();
  ASSERT(request_headers.has_value());

  if (absl::StrContains(request_headers->getContentTypeValue(),
                        Envoy::Http::Headers::get().ContentTypeValues.Grpc)) {
  }
}

void StateStoreHandler::handleStateStoreGetRequest(absl::string_view store_name,
                                                   StateStoreGetRequest&& request) {
  auto state_store_instance = parent_.config_->getStateStoreInstance(store_name);
  if (!state_store_instance.has_value()) {
    parent_.decoder_callbacks_->sendLocalReply(Http::Code::BadRequest, {}, {}, {},
                                               "state_store_not_found");
    return;
  }

  auto cancellable = state_store_instance->get(std::move(request), *this);
  if (cancellable == nullptr) {
    parent_.decoder_callbacks_->sendLocalReply(Http::Code::BadRequest, {}, {}, {},
                                               "state_store_get_failed");
    return;
  }
  pending_requests_.push_back(std::move(cancellable));
}
void StateStoreHandler::handleStateStoreSetREquest(absl::string_view store_name,
                                                   StateStoreSetRequest&& request) {
  auto state_store_instance = parent_.config_->getStateStoreInstance(store_name);
  if (!state_store_instance.has_value()) {
    parent_.decoder_callbacks_->sendLocalReply(Http::Code::BadRequest, {}, {}, {},

                                               "state_store_not_found");
    return;
  }

  auto cancellable = state_store_instance->set(std::move(request), *this);
  if (cancellable == nullptr) {
    parent_.decoder_callbacks_->sendLocalReply(Http::Code::BadRequest, {}, {}, {},
                                               "state_store_set_failed");
  }
  pending_requests_.push_back(std::move(cancellable));
}

void StateStoreHandler::handleStateStoreDelRequest(absl::string_view store_name,
                                                   StateStoreDelRequest&& request) {
  auto state_store_instance = parent_.config_->getStateStoreInstance(store_name);
  if (!state_store_instance.has_value()) {
    parent_.decoder_callbacks_->sendLocalReply(Http::Code::BadRequest, {}, {}, {},
                                               "state_store_not_found");
    return;
  }
  auto cancellable = state_store_instance->del(std::move(request), *this);
  if (cancellable == nullptr) {
    parent_.decoder_callbacks_->sendLocalReply(Http::Code::BadRequest, {}, {}, {},
                                               "state_store_del_failed");
  }
  pending_requests_.push_back(std::move(cancellable));
}

Http::FilterHeadersStatus MultipleRuntimeFilter::decodeHeaders(Http::RequestHeaderMap&,
                                                               bool end_stream) {
  if (end_stream) {
    handleMultipleRuntimeRequest();
  }

  return Http::FilterHeadersStatus::StopIteration;
}
Http::FilterDataStatus MultipleRuntimeFilter::decodeData(Buffer::Instance&, bool end_stream) {
  if (end_stream) {
    handleMultipleRuntimeRequest();
  }

  return Http::FilterDataStatus::StopIterationAndBuffer;
}
Http::FilterTrailersStatus MultipleRuntimeFilter::decodeTrailers(Http::RequestTrailerMap&) {
  handleMultipleRuntimeRequest();

  return Http::FilterTrailersStatus::StopIteration;
}

} // namespace MultipleRuntime
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
