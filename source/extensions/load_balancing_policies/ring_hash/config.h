#pragma once

#include <memory>

#include "envoy/extensions/load_balancing_policies/ring_hash/v3/ring_hash.pb.h"
#include "envoy/extensions/load_balancing_policies/ring_hash/v3/ring_hash.pb.validate.h"
#include "envoy/upstream/load_balancer.h"

#include "source/common/upstream/load_balancer_factory_base.h"
#include "source/extensions/load_balancing_policies/common/factory_base.h"
#include "source/extensions/load_balancing_policies/ring_hash/ring_hash_lb.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolices {
namespace RingHash {

using RingHashLbProto = envoy::extensions::load_balancing_policies::ring_hash::v3::RingHash;

class Factory : public Upstream::TypedLoadBalancerFactoryBase<RingHashLbProto> {
public:
  Factory() : TypedLoadBalancerFactoryBase("envoy.load_balancing_policies.ring_hash") {}

  absl::StatusOr<Upstream::ThreadAwareLoadBalancerPtr>
  create(const Envoy::Upstream::ClusterProto& cluster_proto, ProtobufTypes::MessagePtr config,
         Upstream::Cluster& cluster, Server::Configuration::ServerFactoryContext& context) override;
};

} // namespace RingHash
} // namespace LoadBalancingPolices
} // namespace Extensions
} // namespace Envoy
