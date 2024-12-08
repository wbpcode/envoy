#include "source/extensions/load_balancing_policies/round_robin/config.h"

#include "envoy/extensions/load_balancing_policies/round_robin/v3/round_robin.pb.h"

#include "source/extensions/load_balancing_policies/round_robin/round_robin_lb.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolices {
namespace RoundRobin {

Upstream::LoadBalancerPtr LbFactory::create(Upstream::LoadBalancerParams params) {
  // TODO(wbpcode): merge the two constructors.
  if (lb_config_ != nullptr) {
    return std::make_unique<Upstream::RoundRobinLoadBalancer>(
        params.priority_set, params.local_priority_set, info_->lbStats(), runtime_, random_,
        PROTOBUF_PERCENT_TO_ROUNDED_INTEGER_OR_DEFAULT(info_->lbConfig(), healthy_panic_threshold,
                                                       100, 50),
        *lb_config_, time_source_);
  } else {
    return std::make_unique<Upstream::RoundRobinLoadBalancer>(
        params.priority_set, params.local_priority_set, info_->lbStats(), runtime_, random_,
        info_->lbConfig(),
        legacy_lb_config_ != nullptr ? OptRef<const LegacyRoundRobinLbProto>(*legacy_lb_config_)
                                     : absl::nullopt,
        time_source_);
  }
}

LbFactory::LbFactory(const ClusterProto& cluster_proto, ProtobufTypes::MessagePtr config,
                     Upstream::Cluster& cluster,
                     Server::Configuration::ServerFactoryContext& context)
    : Common::LbFactoryBase(cluster, context) {
  if (config != nullptr) {
    if (dynamic_cast<RoundRobinLbProto*>(config.get()) != nullptr) {
      lb_config_.reset(dynamic_cast<RoundRobinLbProto*>(config.release()));
    }
  } else if (cluster_proto.has_round_robin_lb_config()) {
    legacy_lb_config_ =
        std::make_unique<LegacyRoundRobinLbProto>(cluster_proto.round_robin_lb_config());
  }
}

/**
 * Static registration for the Factory. @see RegisterFactory.
 */
REGISTER_FACTORY(Factory, Upstream::TypedLoadBalancerFactory);

} // namespace RoundRobin
} // namespace LoadBalancingPolices
} // namespace Extensions
} // namespace Envoy
