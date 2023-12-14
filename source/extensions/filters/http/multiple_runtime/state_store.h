#pragma once

#include <string>
#include "envoy/common/pure.h"
#include "envoy/config/typed_config.h"
#include <memory>
#include "envoy/server/factory_context.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace MultipleRuntime {

// TODO(wbpcode): metadata support.
// TODO(wbpcode): operation options support.
// TODO(wbpcode): etag support.
// TODO(wbpcode): transaction support.

struct StateStoreGetRequest {
  std::string key_;
};

struct StateStoreGetResponse {
  std::string value_;
};

struct StateStoreSetRequest {
  std::string key_;
  std::string value_;
};

struct StateStoreSetResponse {
  bool success_;
};

struct StateStoreDelRequest {
  std::string key_;
};

struct StateStoreDelResponse {
  bool success_;
};

class Cancellable {
public:
  virtual ~Cancellable() = default;
  virtual void cancel() PURE;
};
using CancancellablePtr = std::unique_ptr<Cancellable>;

class StateStoreGetCallbacks {
public:
  virtual ~StateStoreGetCallbacks() = default;
  virtual void onFailure() PURE;
  virtual void onSuccess(StateStoreGetResponse&& response) PURE;
};

class StateStoreSetCallbacks {
public:
  virtual ~StateStoreSetCallbacks() = default;
  virtual void onFailure() PURE;
  virtual void onSuccess(StateStoreSetResponse&& response) PURE;
};

class StateStoreDelCallbacks {
public:
  virtual ~StateStoreDelCallbacks() = default;
  virtual void onFailure() PURE;
  virtual void onSuccess(StateStoreDelResponse&& response) PURE;
};

class StateStoreInstance {
public:
  virtual ~StateStoreInstance() = default;

  virtual CancancellablePtr get(StateStoreGetRequest&& request,
                                StateStoreGetCallbacks& callbacks) PURE;
  virtual CancancellablePtr set(StateStoreSetRequest&& request,
                                StateStoreSetCallbacks& callbacks) PURE;
  virtual CancancellablePtr del(StateStoreDelRequest&& request,
                                StateStoreDelCallbacks& callbacks) PURE;
};
using StateStoreInstanceSharedPtr = std::shared_ptr<StateStoreInstance>;

class StateStoreInstanceFactory : public Envoy::Config::TypedFactory {
public:
  ~StateStoreInstanceFactory() override = default;

  virtual StateStoreInstanceSharedPtr create(const Protobuf::Message& config,
                                             Server::Configuration::FactoryContext& context) PURE;

  std::string category() const override { return "envoy.multiple_runtime.state_store"; }
};

} // namespace MultipleRuntime
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
