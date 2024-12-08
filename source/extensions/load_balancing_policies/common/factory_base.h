#pragma once

#include <memory>

#include "envoy/server/factory_context.h"
#include "envoy/upstream/load_balancer.h"

#include "source/common/upstream/load_balancer_factory_base.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolices {
namespace Common {

class LbFactoryBase : public Upstream::LoadBalancerFactory {
public:
  LbFactoryBase(Upstream::Cluster& cluster, Server::Configuration::ServerFactoryContext& context)
      : runtime_(context.runtime()), random_(context.api().randomGenerator()),
        time_source_(context.timeSource()), info_(cluster.info()) {}

  bool recreateOnHostChange() const override { return false; }

protected:
  Runtime::Loader& runtime_;
  Random::RandomGenerator& random_;
  TimeSource& time_source_;
  Upstream::ClusterInfoConstSharedPtr info_;
};

template <class ProtoType, class LbFactoryImpl>
class FactoryBase : public Upstream::TypedLoadBalancerFactoryBase<ProtoType> {
public:
  FactoryBase(const std::string& name) : Upstream::TypedLoadBalancerFactoryBase<ProtoType>(name) {}

  absl::StatusOr<Upstream::ThreadAwareLoadBalancerPtr>
  create(const Envoy::Upstream::ClusterProto& cluster_proto, ProtobufTypes::MessagePtr config,
         Upstream::Cluster& cluster,
         Server::Configuration::ServerFactoryContext& context) override {
    return std::make_unique<ThreadAwareLb>(
        std::make_shared<LbFactoryImpl>(cluster_proto, std::move(config), cluster, context));
  }

private:
  class ThreadAwareLb : public Upstream::ThreadAwareLoadBalancer {
  public:
    ThreadAwareLb(Upstream::LoadBalancerFactorySharedPtr factory) : factory_(std::move(factory)) {}

    Upstream::LoadBalancerFactorySharedPtr factory() override { return factory_; }
    absl::Status initialize() override { return absl::OkStatus(); }

  private:
    Upstream::LoadBalancerFactorySharedPtr factory_;
  };

  const std::string name_;
};

/**
 * Helper class to hold either a legacy or production config.
 */
template <class ActiveType, class LegacyType> class ActiveOrLegacy {
public:
  template <class BaseType> static ActiveOrLegacy get(const BaseType* base) {
    auto* active_type = dynamic_cast<const ActiveType*>(base);
    if (active_type != nullptr) {
      return {active_type};
    }
    auto* legacy_type = dynamic_cast<const LegacyType*>(base);
    if (legacy_type != nullptr) {
      return {legacy_type};
    }

    return {};
  }

  bool hasActive() const { return active_ != nullptr; }
  bool hasLegacy() const { return legacy_ != nullptr; }

  const ActiveType* active() const {
    ASSERT(hasActive());
    return active_;
  }
  const LegacyType* legacy() const {
    ASSERT(hasLegacy());
    return legacy_;
  }

private:
  ActiveOrLegacy() = default;
  ActiveOrLegacy(const ActiveType* active) : active_(active) {}
  ActiveOrLegacy(const LegacyType* legacy) : legacy_(legacy) {}

  const ActiveType* active_{};
  const LegacyType* legacy_{};
};

} // namespace Common
} // namespace LoadBalancingPolices
} // namespace Extensions
} // namespace Envoy
