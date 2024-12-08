#pragma once

#include <memory>

#include "envoy/extensions/load_balancing_policies/cluster_provided/v3/cluster_provided.pb.h"
#include "envoy/upstream/load_balancer.h"

#include "source/common/upstream/load_balancer_factory_base.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolices {
namespace ClusterProvided {

class Factory
    : public Upstream::TypedLoadBalancerFactoryBase<
          envoy::extensions::load_balancing_policies::cluster_provided::v3::ClusterProvided> {
public:
  Factory() : TypedLoadBalancerFactoryBase("envoy.load_balancing_policies.cluster_provided") {}

  absl::StatusOr<Upstream::ThreadAwareLoadBalancerPtr>
  create(const Envoy::Upstream::ClusterProto& cluster_proto, ProtobufTypes::MessagePtr config,
         Upstream::Cluster& cluster, Server::Configuration::ServerFactoryContext& context) override;
};

DECLARE_FACTORY(Factory);

} // namespace ClusterProvided
} // namespace LoadBalancingPolices
} // namespace Extensions
} // namespace Envoy
