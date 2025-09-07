#pragma once

#include <memory>

#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/event/dispatcher.h"
#include "envoy/extensions/access_loggers/grpc/v3/als.pb.h"
#include "envoy/grpc/async_client_manager.h"
#include "envoy/singleton/instance.h"
#include "envoy/stats/scope.h"
#include "envoy/thread_local/thread_local.h"

#include "source/common/common/assert.h"
#include "source/common/grpc/typed_async_client.h"
#include "source/common/http/utility.h"
#include "source/common/protobuf/utility.h"
#include "source/common/tracing/null_span_impl.h"

#include "absl/container/flat_hash_map.h"
#include "absl/types/optional.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace Common {

template <typename LogRequest, typename LogResponse> class GrpcAccessLogClient {
public:
  virtual ~GrpcAccessLogClient() = default;
  virtual bool isConnected() PURE;
  virtual bool log(const LogRequest& request) PURE;

protected:
  GrpcAccessLogClient(const Grpc::RawAsyncClientSharedPtr& client,
                      const Protobuf::MethodDescriptor& service_method)
      : client_(client), service_method_(service_method) {}

  Grpc::AsyncClient<LogRequest, LogResponse> client_;
  const Protobuf::MethodDescriptor& service_method_;
  const Http::AsyncClient::RequestOptions opts_;
};

template <typename LogRequest, typename LogResponse>
class UnaryGrpcAccessLogClient : public GrpcAccessLogClient<LogRequest, LogResponse> {
public:
  using AsyncRequestCallbacksFactory = std::function<Grpc::AsyncRequestCallbacks<LogResponse>&()>;

  UnaryGrpcAccessLogClient(const Grpc::RawAsyncClientSharedPtr& client,
                           const Protobuf::MethodDescriptor& service_method,
                           AsyncRequestCallbacksFactory callback_factory)
      : GrpcAccessLogClient<LogRequest, LogResponse>(client, service_method),
        callbacks_factory_(callback_factory) {}

  bool isConnected() override { return false; }

  bool log(const LogRequest& request) override {
    GrpcAccessLogClient<LogRequest, LogResponse>::client_->send(
        GrpcAccessLogClient<LogRequest, LogResponse>::service_method_, request,
        callbacks_factory_(), Tracing::NullSpan::instance(),
        GrpcAccessLogClient<LogRequest, LogResponse>::opts_);
    return true;
  }

private:
  AsyncRequestCallbacksFactory callbacks_factory_;
};

template <typename LogRequest, typename LogResponse>
class StreamingGrpcAccessLogClient : public GrpcAccessLogClient<LogRequest, LogResponse> {
public:
  StreamingGrpcAccessLogClient(const Grpc::RawAsyncClientSharedPtr& client,
                               const Protobuf::MethodDescriptor& service_method)
      : GrpcAccessLogClient<LogRequest, LogResponse>(client, service_method) {}

public:
  struct LocalStream : public Grpc::AsyncStreamCallbacks<LogResponse> {
    LocalStream(StreamingGrpcAccessLogClient& parent) : parent_(parent) {}

    // Grpc::AsyncStreamCallbacks
    void onCreateInitialMetadata(Http::RequestHeaderMap&) override {}
    void onReceiveInitialMetadata(Http::ResponseHeaderMapPtr&&) override {}
    void onReceiveMessage(std::unique_ptr<LogResponse>&&) override {}
    void onReceiveTrailingMetadata(Http::ResponseTrailerMapPtr&&) override {}
    void onRemoteClose(Grpc::Status::GrpcStatus, const std::string&) override {
      ASSERT(parent_.stream_ != nullptr);
      if (parent_.stream_->stream_ != nullptr) {
        // Only reset if we have a stream. Otherwise we had an inline failure and we will clear the
        // stream data in send().
        parent_.stream_.reset();
      }
    }

    StreamingGrpcAccessLogClient& parent_;
    Grpc::AsyncStream<LogRequest> stream_{};
  };

  bool isConnected() override { return stream_ != nullptr && stream_->stream_ != nullptr; }

  bool log(const LogRequest& request) override {
    if (!stream_) {
      stream_ = std::make_unique<LocalStream>(*this);
    }

    if (stream_->stream_ == nullptr) {
      stream_->stream_ = GrpcAccessLogClient<LogRequest, LogResponse>::client_->start(
          GrpcAccessLogClient<LogRequest, LogResponse>::service_method_, *stream_,
          GrpcAccessLogClient<LogRequest, LogResponse>::opts_);
    }

    if (stream_->stream_ != nullptr) {
      if (stream_->stream_->isAboveWriteBufferHighWatermark()) {
        return false;
      }
      stream_->stream_->sendMessage(request, false);
    } else {
      // Clear out the stream data due to stream creation failure.
      stream_.reset();
    }
    return true;
  }

  std::unique_ptr<LocalStream> stream_;
};

inline absl::StatusOr<Grpc::RawAsyncClientSharedPtr> createRawAsyncClient(
    const envoy::extensions::access_loggers::grpc::v3::CommonGrpcAccessLogConfig& config,
    Grpc::AsyncClientManager& async_client_manager, Stats::Scope& scope) {
  // We pass skip_cluster_check=true to factoryForGrpcService in order to avoid throwing
  // exceptions in worker threads. Call sites of this getOrCreateLogger must check the cluster
  // availability via ClusterManager::checkActiveStaticCluster beforehand, and throw exceptions in
  // the main thread if necessary.

  // If a retry policy is specified in the access log config, we copy it to the grpc_service
  // so that the async client factory can pick it up. If the a retry policy is also be specified
  // in the grpc_service, the one in the access log config takes precedence.
  auto grpc_service_config = config.grpc_service();
  if (config.has_grpc_stream_retry_policy()) {
    *grpc_service_config.mutable_retry_policy() = config.grpc_stream_retry_policy();
  }
  auto factory_or_error =
      async_client_manager.factoryForGrpcService(grpc_service_config, scope, true);
  RETURN_IF_ERROR(factory_or_error.status());

  return factory_or_error.value()->createUncachedRawAsyncClient();
}

} // namespace Common
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
