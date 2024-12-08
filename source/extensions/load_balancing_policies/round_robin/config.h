#pragma once

#include "envoy/extensions/load_balancing_policies/round_robin/v3/round_robin.pb.h"
#include "envoy/extensions/load_balancing_policies/round_robin/v3/round_robin.pb.validate.h"
#include "envoy/upstream/load_balancer.h"

#include "source/common/common/logger.h"
#include "source/extensions/load_balancing_policies/common/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolices {
namespace RoundRobin {

using RoundRobinLbProto = envoy::extensions::load_balancing_policies::round_robin::v3::RoundRobin;
using ClusterProto = envoy::config::cluster::v3::Cluster;
using LegacyRoundRobinLbProto = ClusterProto::RoundRobinLbConfig;

class LbFactory : public Common::LbFactoryBase {
public:
  LbFactory(const ClusterProto& cluster_proto, ProtobufTypes::MessagePtr config,
            Upstream::Cluster& cluster, Server::Configuration::ServerFactoryContext& context);
  Upstream::LoadBalancerPtr create(Upstream::LoadBalancerParams params) override;

private:
  std::unique_ptr<LegacyRoundRobinLbProto> legacy_lb_config_;
  std::unique_ptr<RoundRobinLbProto> lb_config_;
};

class Factory : public Common::FactoryBase<RoundRobinLbProto, LbFactory> {
public:
  Factory() : FactoryBase("envoy.load_balancing_policies.round_robin") {}
};

DECLARE_FACTORY(Factory);

} // namespace RoundRobin
} // namespace LoadBalancingPolices
} // namespace Extensions
} // namespace Envoy
