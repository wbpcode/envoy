#include "source/extensions/load_balancing_policies/least_request/config.h"

#include "envoy/extensions/load_balancing_policies/least_request/v3/least_request.pb.h"

#include "source/extensions/load_balancing_policies/least_request/least_request_lb.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolices {
namespace LeastRequest {

TypedLeastRequestLbConfig::TypedLeastRequestLbConfig(const LeastRequestLbProto& lb_config)
    : lb_config_(lb_config) {}

TypedLeastRequestLbConfig::TypedLeastRequestLbConfig(const ClusterProto& cluster_config) {
  auto locality_lb_config = Upstream::LoadBalancerConfigHelper::localityLbConfigFromCommonLbConfig(
      cluster_config.common_lb_config());
  if (locality_lb_config.has_value()) {
    *lb_config_.mutable_locality_lb_config() = std::move(*locality_lb_config);
  }

  if (!cluster_config.has_least_request_lb_config()) {
    return;
  }

  if (cluster_config.least_request_lb_config().has_active_request_bias()) {
    lb_config_.mutable_active_request_bias()->CopyFrom(
        cluster_config.least_request_lb_config().active_request_bias());
  }

  if (cluster_config.least_request_lb_config().has_choice_count()) {
    lb_config_.mutable_choice_count()->set_value(
        cluster_config.least_request_lb_config().choice_count().value());
  }

  auto slow_start_config = Upstream::LoadBalancerConfigHelper::slowStartConfigFromLegacyProto(
      cluster_config.least_request_lb_config());
  if (slow_start_config.has_value()) {
    *lb_config_.mutable_slow_start_config() = std::move(*slow_start_config);
  }
}

Upstream::LoadBalancerPtr LeastRequestCreator::operator()(
    Upstream::LoadBalancerParams params, OptRef<const Upstream::LoadBalancerConfig> lb_config,
    const Upstream::ClusterInfo& cluster_info, const Upstream::PrioritySet&,
    Runtime::Loader& runtime, Random::RandomGenerator& random, TimeSource& time_source) {

  ASSERT(lb_config.has_value());

  const TypedLeastRequestLbConfig* typed_lb_config =
      dynamic_cast<const TypedLeastRequestLbConfig*>(lb_config.ptr());
  ASSERT(typed_lb_config != nullptr);

  return std::make_unique<Upstream::LeastRequestLoadBalancer>(
      params.priority_set, params.local_priority_set, cluster_info.lbStats(), runtime, random,
      PROTOBUF_PERCENT_TO_ROUNDED_INTEGER_OR_DEFAULT(cluster_info.lbConfig(),
                                                     healthy_panic_threshold, 100, 50),
      typed_lb_config->lbConfig(), time_source);
}

/**
 * Static registration for the Factory. @see RegisterFactory.
 */
REGISTER_FACTORY(Factory, Upstream::TypedLoadBalancerFactory);

} // namespace LeastRequest
} // namespace LoadBalancingPolices
} // namespace Extensions
} // namespace Envoy
