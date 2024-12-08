#pragma once

#include "envoy/extensions/load_balancing_policies/random/v3/random.pb.h"
#include "envoy/extensions/load_balancing_policies/random/v3/random.pb.validate.h"
#include "envoy/upstream/load_balancer.h"

#include "source/common/common/logger.h"
#include "source/extensions/load_balancing_policies/common/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolices {
namespace Random {

using RandomLbProto = envoy::extensions::load_balancing_policies::random::v3::Random;

class LbFactory : public Common::LbFactoryBase {
public:
  LbFactory(const Envoy::Upstream::ClusterProto& cluster_proto, ProtobufTypes::MessagePtr config,
            Upstream::Cluster& cluster, Server::Configuration::ServerFactoryContext& context);

  Upstream::LoadBalancerPtr create(Upstream::LoadBalancerParams params) override;

private:
  std::unique_ptr<RandomLbProto> lb_config_;
};

class Factory : public Common::FactoryBase<RandomLbProto, LbFactory> {
public:
  Factory() : FactoryBase("envoy.load_balancing_policies.random") {}
};

DECLARE_FACTORY(Factory);

} // namespace Random
} // namespace LoadBalancingPolices
} // namespace Extensions
} // namespace Envoy
