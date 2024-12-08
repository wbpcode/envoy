#include "source/extensions/load_balancing_policies/ring_hash/config.h"

#include "source/extensions/load_balancing_policies/ring_hash/ring_hash_lb.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolices {
namespace RingHash {

absl::StatusOr<Upstream::ThreadAwareLoadBalancerPtr>
Factory::create(const Envoy::Upstream::ClusterProto& cluster_proto,
                ProtobufTypes::MessagePtr config, Upstream::Cluster& cluster,
                Server::Configuration::ServerFactoryContext& context) {
  std::unique_ptr<Upstream::RingHashLbProto> lb_config;
  if (config != nullptr) {
    if (dynamic_cast<Upstream::RingHashLbProto*>(config.get()) != nullptr) {
      lb_config.reset(dynamic_cast<Upstream::RingHashLbProto*>(config.release()));
    }
  }

  auto cluster_info = cluster.info();

  // Assume legacy config.
  if (lb_config == nullptr) {
    return std::make_unique<Upstream::RingHashLoadBalancer>(
        cluster.prioritySet(), cluster_info->lbStats(), cluster_info->statsScope(),
        context.runtime(), context.api().randomGenerator(),
        cluster_proto.has_maglev_lb_config()
            ? OptRef<const Upstream::LegacyRingHashLbProto>(cluster_proto.ring_hash_lb_config())
            : absl::nullopt,
        cluster_info->lbConfig());
  }

  return std::make_unique<Upstream::RingHashLoadBalancer>(
      cluster.prioritySet(), cluster_info->lbStats(), cluster_info->statsScope(), context.runtime(),
      context.api().randomGenerator(),
      static_cast<uint32_t>(PROTOBUF_PERCENT_TO_ROUNDED_INTEGER_OR_DEFAULT(
          cluster_info->lbConfig(), healthy_panic_threshold, 100, 50)),
      *lb_config);
}

/**
 * Static registration for the Factory. @see RegisterFactory.
 */
REGISTER_FACTORY(Factory, Upstream::TypedLoadBalancerFactory);

} // namespace RingHash
} // namespace LoadBalancingPolices
} // namespace Extensions
} // namespace Envoy
