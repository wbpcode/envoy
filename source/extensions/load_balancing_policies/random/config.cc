#include "source/extensions/load_balancing_policies/random/config.h"

#include "envoy/extensions/load_balancing_policies/random/v3/random.pb.h"

#include "source/extensions/load_balancing_policies/random/random_lb.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolices {
namespace Random {

Upstream::LoadBalancerPtr LbFactory::create(Upstream::LoadBalancerParams params) {
  // TODO(wbpcode): merge the two constructors.
  if (lb_config_ != nullptr) {
    return std::make_unique<Upstream::RandomLoadBalancer>(
        params.priority_set, params.local_priority_set, info_->lbStats(), runtime_, random_,
        PROTOBUF_PERCENT_TO_ROUNDED_INTEGER_OR_DEFAULT(info_->lbConfig(), healthy_panic_threshold,
                                                       100, 50),
        *lb_config_);
  } else {
    return std::make_unique<Upstream::RandomLoadBalancer>(
        params.priority_set, params.local_priority_set, info_->lbStats(), runtime_, random_,
        info_->lbConfig());
  }
}

LbFactory::LbFactory(const Envoy::Upstream::ClusterProto&, ProtobufTypes::MessagePtr config,
                     Upstream::Cluster& cluster,
                     Server::Configuration::ServerFactoryContext& context)
    : LbFactoryBase(cluster, context) {
  if (config != nullptr) {
    if (dynamic_cast<RandomLbProto*>(config.get()) != nullptr) {
      lb_config_.reset(dynamic_cast<RandomLbProto*>(config.release()));
    }
  }
}

/**
 * Static registration for the Factory. @see RegisterFactory.
 */
REGISTER_FACTORY(Factory, Upstream::TypedLoadBalancerFactory);

} // namespace Random
} // namespace LoadBalancingPolices
} // namespace Extensions
} // namespace Envoy
