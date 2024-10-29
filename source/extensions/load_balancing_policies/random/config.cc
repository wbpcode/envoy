#include "source/extensions/load_balancing_policies/random/config.h"

#include "envoy/extensions/load_balancing_policies/random/v3/random.pb.h"

#include "source/extensions/load_balancing_policies/random/random_lb.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolices {
namespace Random {

TypedRandomLbConfig::TypedRandomLbConfig(const RandomLbProto& lb_config) : lb_config_(lb_config) {}

TypedRandomLbConfig::TypedRandomLbConfig(const ClusterProto& cluster_config) {
  auto locality_lb_config = Upstream::LoadBalancerConfigHelper::localityLbConfigFromCommonLbConfig(
      cluster_config.common_lb_config());
  if (locality_lb_config.has_value()) {
    *lb_config_.mutable_locality_lb_config() = std::move(*locality_lb_config);
  }
}

Upstream::LoadBalancerPtr RandomCreator::operator()(
    Upstream::LoadBalancerParams params, OptRef<const Upstream::LoadBalancerConfig> lb_config,
    const Upstream::ClusterInfo& cluster_info, const Upstream::PrioritySet&,
    Runtime::Loader& runtime, Envoy::Random::RandomGenerator& random, TimeSource&) {

  const auto typed_lb_config = dynamic_cast<const TypedRandomLbConfig*>(lb_config.ptr());
  ASSERT(typed_lb_config != nullptr);

  return std::make_unique<Upstream::RandomLoadBalancer>(
      params.priority_set, params.local_priority_set, cluster_info.lbStats(), runtime, random,
      PROTOBUF_PERCENT_TO_ROUNDED_INTEGER_OR_DEFAULT(cluster_info.lbConfig(),
                                                     healthy_panic_threshold, 100, 50),
      typed_lb_config->lbConfig());
}

/**
 * Static registration for the Factory. @see RegisterFactory.
 */
REGISTER_FACTORY(Factory, Upstream::TypedLoadBalancerFactory);

} // namespace Random
} // namespace LoadBalancingPolices
} // namespace Extensions
} // namespace Envoy
