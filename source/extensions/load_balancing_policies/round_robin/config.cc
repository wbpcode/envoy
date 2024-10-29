#include "source/extensions/load_balancing_policies/round_robin/config.h"

#include "envoy/extensions/load_balancing_policies/round_robin/v3/round_robin.pb.h"

#include "source/extensions/load_balancing_policies/round_robin/round_robin_lb.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolices {
namespace RoundRobin {

TypedRoundRobinLbConfig::TypedRoundRobinLbConfig(const RoundRobinLbProto& lb_config)
    : lb_config_(lb_config) {}

TypedRoundRobinLbConfig::TypedRoundRobinLbConfig(const ClusterProto& cluster_config) {
  auto locality_lb_config = Upstream::LoadBalancerConfigHelper::localityLbConfigFromCommonLbConfig(
      cluster_config.common_lb_config());
  if (locality_lb_config.has_value()) {
    *lb_config_.mutable_locality_lb_config() = std::move(*locality_lb_config);
  }

  auto slow_start_config = Upstream::LoadBalancerConfigHelper::slowStartConfigFromLegacyProto(
      cluster_config.round_robin_lb_config());
  if (slow_start_config.has_value()) {
    *lb_config_.mutable_slow_start_config() = std::move(*slow_start_config);
  }
}

Upstream::LoadBalancerPtr RoundRobinCreator::operator()(
    Upstream::LoadBalancerParams params, OptRef<const Upstream::LoadBalancerConfig> lb_config,
    const Upstream::ClusterInfo& cluster_info, const Upstream::PrioritySet&,
    Runtime::Loader& runtime, Random::RandomGenerator& random, TimeSource& time_source) {

  ASSERT(lb_config.has_value());
  const TypedRoundRobinLbConfig* typed_lb_config =
      dynamic_cast<const TypedRoundRobinLbConfig*>(lb_config.ptr());
  ASSERT(typed_lb_config != nullptr);

  return std::make_unique<Upstream::RoundRobinLoadBalancer>(
      params.priority_set, params.local_priority_set, cluster_info.lbStats(), runtime, random,
      PROTOBUF_PERCENT_TO_ROUNDED_INTEGER_OR_DEFAULT(cluster_info.lbConfig(),
                                                     healthy_panic_threshold, 100, 50),
      typed_lb_config->lbConfig(), time_source);
}

/**
 * Static registration for the Factory. @see RegisterFactory.
 */
REGISTER_FACTORY(Factory, Upstream::TypedLoadBalancerFactory);

} // namespace RoundRobin
} // namespace LoadBalancingPolices
} // namespace Extensions
} // namespace Envoy
