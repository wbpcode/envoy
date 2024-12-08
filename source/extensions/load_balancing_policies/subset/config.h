#pragma once

#include <memory>

#include "envoy/upstream/load_balancer.h"

#include "source/common/upstream/load_balancer_factory_base.h"
#include "source/extensions/load_balancing_policies/subset/subset_lb.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolices {
namespace Subset {

class SubsetLbFactory
    : public Upstream::TypedLoadBalancerFactoryBase<Upstream::SubsetLbConfigProto> {
public:
  SubsetLbFactory() : TypedLoadBalancerFactoryBase("envoy.load_balancing_policies.subset") {}

  absl::StatusOr<Upstream::ThreadAwareLoadBalancerPtr>
  create(const Envoy::Upstream::ClusterProto& cluster_proto, ProtobufTypes::MessagePtr config,
         Upstream::Cluster& cluster, Server::Configuration::ServerFactoryContext& context) override;
};

} // namespace Subset
} // namespace LoadBalancingPolices
} // namespace Extensions
} // namespace Envoy
