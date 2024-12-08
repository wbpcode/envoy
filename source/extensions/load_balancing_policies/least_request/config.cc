#include "source/extensions/load_balancing_policies/least_request/config.h"

#include "envoy/extensions/load_balancing_policies/least_request/v3/least_request.pb.h"

#include "source/extensions/load_balancing_policies/least_request/least_request_lb.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolices {
namespace LeastRequest {

Upstream::LoadBalancerPtr LbFactory::create(Upstream::LoadBalancerParams params) {
  // TODO(wbpcode): merge the two constructors.
  if (lb_config_ != nullptr) {
    return std::make_unique<Upstream::LeastRequestLoadBalancer>(
        params.priority_set, params.local_priority_set, info_->lbStats(), runtime_, random_,
        PROTOBUF_PERCENT_TO_ROUNDED_INTEGER_OR_DEFAULT(info_->lbConfig(), healthy_panic_threshold,
                                                       100, 50),
        *lb_config_, time_source_);
  } else {
    return std::make_unique<Upstream::LeastRequestLoadBalancer>(
        params.priority_set, params.local_priority_set, info_->lbStats(), runtime_, random_,
        info_->lbConfig(),
        legacy_lb_config_ != nullptr ? OptRef<LegacyLeastRequestLbProto>(*legacy_lb_config_)
                                     : absl::nullopt,
        time_source_);
  }
}

LbFactory::LbFactory(const ClusterProto& cluster_proto, ProtobufTypes::MessagePtr config,
                     Upstream::Cluster& cluster,
                     Server::Configuration::ServerFactoryContext& context)
    : Common::LbFactoryBase(cluster, context) {
  if (config != nullptr) {
    if (dynamic_cast<LeastRequestLbProto*>(config.get()) != nullptr) {
      lb_config_.reset(dynamic_cast<LeastRequestLbProto*>(config.release()));
    }
  } else if (cluster_proto.has_least_request_lb_config()) {
    legacy_lb_config_ =
        std::make_unique<LegacyLeastRequestLbProto>(cluster_proto.least_request_lb_config());
  }
}

/**
 * Static registration for the Factory. @see RegisterFactory.
 */
REGISTER_FACTORY(Factory, Upstream::TypedLoadBalancerFactory);

} // namespace LeastRequest
} // namespace LoadBalancingPolices
} // namespace Extensions
} // namespace Envoy
