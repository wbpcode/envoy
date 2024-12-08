#pragma once

#include <memory>

#include "envoy/extensions/load_balancing_policies/maglev/v3/maglev.pb.h"
#include "envoy/extensions/load_balancing_policies/maglev/v3/maglev.pb.validate.h"
#include "envoy/upstream/load_balancer.h"

#include "source/common/upstream/load_balancer_factory_base.h"
#include "source/extensions/load_balancing_policies/common/factory_base.h"
#include "source/extensions/load_balancing_policies/maglev/maglev_lb.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolices {
namespace Maglev {

using MaglevLbProto = envoy::extensions::load_balancing_policies::maglev::v3::Maglev;

class Factory : public Upstream::TypedLoadBalancerFactoryBase<MaglevLbProto> {
public:
  Factory() : TypedLoadBalancerFactoryBase("envoy.load_balancing_policies.maglev") {}

  absl::StatusOr<Upstream::ThreadAwareLoadBalancerPtr>
  create(const Envoy::Upstream::ClusterProto& cluster_proto, ProtobufTypes::MessagePtr config,
         Upstream::Cluster& cluster, Server::Configuration::ServerFactoryContext& context) override;
};

} // namespace Maglev
} // namespace LoadBalancingPolices
} // namespace Extensions
} // namespace Envoy
