#include "source/extensions/load_balancing_policies/cluster_provided/config.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolices {
namespace ClusterProvided {

// TODO(wbpcode): remove this cluster provided load balancer because it make no sense
// except as a placeholder for clusters that have their own load balancer.
absl::StatusOr<Upstream::ThreadAwareLoadBalancerPtr>
Factory::create(const Envoy::Upstream::ClusterProto&, ProtobufTypes::MessagePtr, Upstream::Cluster&,
                Server::Configuration::ServerFactoryContext&) {
  return nullptr;
}

/**
 * Static registration for the Factory. @see RegisterFactory.
 */
REGISTER_FACTORY(Factory, Upstream::TypedLoadBalancerFactory);

} // namespace ClusterProvided
} // namespace LoadBalancingPolices
} // namespace Extensions
} // namespace Envoy
