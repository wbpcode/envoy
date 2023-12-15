#pragma once

#include "envoy/extensions/filters/http/multiple_runtime/v3/multiple_runtime.pb.h"
#include "envoy/extensions/filters/http/multiple_runtime/v3/multiple_runtime.pb.validate.h"

#include "source/extensions/filters/http/common/pass_through_filter.h"
#include "envoy/server/factory_context.h"
#include "source/extensions/filters/http/multiple_runtime/state_store.h"

#include "absl/container/flat_hash_map.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace MultipleRuntime {

using ProtoConfig = envoy::extensions::filters::http::multiple_runtime::v3::MultipleRuntime;
using ComponentType = envoy::extensions::filters::http::multiple_runtime::v3::ComponentType;

enum class OperationType {
  None,   // Unknown operation.
  Invoke, // Invoke remote service.
  State,  // Get/Set/Del state.
};

class MultipleRuntimeConfig {
public:
  MultipleRuntimeConfig(const ProtoConfig& config, Server::Configuration::FactoryContext& context);

  absl::string_view defaultNamespace() const { return default_namespace_; }

  OptRef<StateStoreInstance> getStateStoreInstance(absl::string_view name);

private:
  const std::string default_namespace_;
  absl::flat_hash_map<std::string, StateStoreInstanceSharedPtr> state_store_instances_;
};

using MultipleRuntimeConfigSharedPtr = std::shared_ptr<MultipleRuntimeConfig>;

class MultipleRuntimeFilter;

class StateStoreHandler : public StateStoreGetCallbacks,
                          public StateStoreSetCallbacks,
                          public StateStoreDelCallbacks {
public:
  StateStoreHandler(MultipleRuntimeFilter& parent) : parent_(parent) {}

  // StateStoreGetCallbacks
  void onStateStoreGetFailure() override;
  void onStateStoreGetSuccess(StateStoreGetResponse&& response) override;

  // StateStoreSetCallbacks
  void onStateStoreSetFailure() override;
  void onStateStoreSetSuccess(StateStoreSetResponse&& response) override;

  // StateStoreDelCallbacks
  void onStateStoreDelFailure() override;
  void onStateStoreDelSuccess(StateStoreDelResponse&& response) override;

  void handleStateStoreGetRequest(absl::string_view store_name, StateStoreGetRequest&& request);
  void handleStateStoreSetREquest(absl::string_view store_name, StateStoreSetRequest&& request);
  void handleStateStoreDelRequest(absl::string_view store_name, StateStoreDelRequest&& request);

  MultipleRuntimeFilter& parent_;

  absl::InlinedVector<CancancellablePtr, 8> pending_requests_;
};

class MultipleRuntimeFilter : public Http::PassThroughDecoderFilter {

public:
  MultipleRuntimeFilter(MultipleRuntimeConfigSharedPtr config) : config_(config) {}

  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& request_headers,
                                          bool end_stream) override;
  Http::FilterDataStatus decodeData(Buffer::Instance& data, bool end_stream) override;
  Http::FilterTrailersStatus decodeTrailers(Http::RequestTrailerMap& trailers) override;

private:
  friend class StateStoreHandler;

  void handleMultipleRuntimeRequest();

  void handleGrpcMultipleRuntimeRequest();
  void handleHttpMultipleRuntimeRequest();

  OperationType operation_type_{OperationType::None};
  std::unique_ptr<StateStoreHandler> state_store_handler_;

  MultipleRuntimeConfigSharedPtr config_;
};

} // namespace MultipleRuntime
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
