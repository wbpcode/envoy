#pragma once

#include "envoy/extensions/load_balancing_policies/least_request/v3/least_request.pb.h"
#include "envoy/extensions/load_balancing_policies/least_request/v3/least_request.pb.validate.h"
#include "envoy/upstream/load_balancer.h"

#include "source/common/common/logger.h"
#include "source/extensions/load_balancing_policies/common/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolices {
namespace LeastRequest {

using LeastRequestLbProto =
    envoy::extensions::load_balancing_policies::least_request::v3::LeastRequest;
using ClusterProto = envoy::config::cluster::v3::Cluster;
using LegacyLeastRequestLbProto = ClusterProto::LeastRequestLbConfig;

class LbFactory : public Common::LbFactoryBase {
public:
  LbFactory(const ClusterProto& cluster_proto, ProtobufTypes::MessagePtr config,
            Upstream::Cluster& cluster, Server::Configuration::ServerFactoryContext& context);
  Upstream::LoadBalancerPtr create(Upstream::LoadBalancerParams params) override;

private:
  std::unique_ptr<LegacyLeastRequestLbProto> legacy_lb_config_;
  std::unique_ptr<LeastRequestLbProto> lb_config_;
};

class Factory : public Common::FactoryBase<LeastRequestLbProto, LbFactory> {
public:
  Factory() : FactoryBase("envoy.load_balancing_policies.least_request") {}
};

DECLARE_FACTORY(Factory);

} // namespace LeastRequest
} // namespace LoadBalancingPolices
} // namespace Extensions
} // namespace Envoy
