#pragma once

#include <string>

#include "envoy/config/route/v3/route.pb.h"
#include "envoy/config/route/v3/route_components.pb.h"
#include "envoy/rds/config_traits.h"
#include "envoy/router/rds.h"
#include "envoy/router/route_config_update_receiver.h"
#include "envoy/server/factory_context.h"
#include "envoy/service/discovery/v3/discovery.pb.h"

#include "source/common/common/logger.h"
#include "source/common/protobuf/utility.h"
#include "source/common/rds/route_config_update_receiver_impl.h"
#include "source/common/router/config_impl.h"
#include "source/common/router/vhds.h"

namespace Envoy {
namespace Router {

class ConfigTraitsImpl : public Rds::ConfigTraits {
public:
  ConfigTraitsImpl(ProtobufMessage::ValidationVisitor& validator) : validator_(validator) {}

  Rds::ConfigConstSharedPtr createNullConfig() const override;
  Rds::ConfigConstSharedPtr createConfig(const Protobuf::Message& rc,
                                         Server::Configuration::ServerFactoryContext& context,
                                         Init::Manager&,
                                         bool validate_clusters_default) const override;

private:
  ProtobufMessage::ValidationVisitor& validator_;
};

class RouteConfigUpdateReceiverImpl : public RouteConfigUpdateReceiver,
                                      Logger::Loggable<Logger::Id::router> {
public:
  RouteConfigUpdateReceiverImpl(Rds::ProtoTraits& proto_traits,
                                Server::Configuration::ServerFactoryContext& factory_context,
                                const std::string& stat_prefix = "")
      : config_traits_(factory_context.messageValidationContext().dynamicValidationVisitor()),
        base_(config_traits_, proto_traits, factory_context),
        factory_context_(factory_context), stat_prefix_(stat_prefix) {}

  using VirtualHostMap = std::map<std::string, envoy::config::route::v3::VirtualHost>;

  bool removeVhosts(VirtualHostMap& vhosts,
                    const Protobuf::RepeatedPtrField<std::string>& removed_vhost_names);
  bool updateVhosts(VirtualHostMap& vhosts, const VirtualHostRefVector& added_vhosts);

  // Router::RouteConfigUpdateReceiver
  absl::StatusOr<bool> onRdsUpdate(const Protobuf::Message& rc, Init::Manager& init_manager,
                                   const std::string& version_info) override;
  bool onVhdsUpdate(const VirtualHostRefVector& added_vhosts,
                    std::set<std::string>&& added_resource_ids,
                    const Protobuf::RepeatedPtrField<std::string>& removed_resources,
                    Init::Manager& init_manager, const std::string& version_info) override;
  uint64_t configHash() const override { return base_.configHash(); }
  const std::optional<Rds::RouteConfigProvider::ConfigInfo>& configInfo() const override {
    return base_.configInfo();
  }
  Rds::RouteConfigProvider*& routeConfigProvider() override { return route_config_provider_; }
  void updateOnDemand(const std::string& aliases) override {
    if (vhds_subscription_ != nullptr) {
      vhds_subscription_->updateOnDemand(aliases);
    }
  }
  const Protobuf::Message& protobufConfiguration() const override {
    return base_.protobufConfiguration();
  }
  Rds::ConfigConstSharedPtr parsedConfiguration() const override {
    return base_.parsedConfiguration();
  }
  SystemTime lastUpdated() const override { return base_.lastUpdated(); }
  const std::set<std::string>& resourceIdsInLastVhdsUpdate() const override {
    return resource_ids_in_last_update_;
  }
  const envoy::config::route::v3::RouteConfiguration& protobufConfigurationCast() const override {
    ASSERT(Envoy::Protobuf::DynamicCastMessage<envoy::config::route::v3::RouteConfiguration>(
        &RouteConfigUpdateReceiverImpl::protobufConfiguration()));
    return static_cast<const envoy::config::route::v3::RouteConfiguration&>(
        RouteConfigUpdateReceiverImpl::protobufConfiguration());
  }

private:
  ConfigTraitsImpl config_traits_;

  Rds::RouteConfigUpdateReceiverImpl base_;

  Server::Configuration::ServerFactoryContext& factory_context_;
  const std::string stat_prefix_;

  uint64_t last_vhds_config_hash_{0ul};
  // vhosts supplied by RDS, to be merged with VHDS vhosts in onVhdsUpdate.
  std::unique_ptr<VirtualHostMap> rds_virtual_hosts_;
  // vhosts supplied by VHDS, to be merged with RDS vhosts in onRdsUpdate.
  std::unique_ptr<VirtualHostMap> vhds_virtual_hosts_;
  std::set<std::string> resource_ids_in_last_update_;
  // The provider that the VHDS subscription publishes to. Bound after construction, see
  // routeConfigProvider().
  Rds::RouteConfigProvider* route_config_provider_{nullptr};
  // The VHDS subscription of the current route configuration, null if it doesn't use VHDS. It is
  // (re)created by onRdsUpdate() whenever the VHDS configuration changes, and it holds a reference
  // to this receiver, so it must be destroyed before the state it reads above.
  VhdsSubscriptionPtr vhds_subscription_;
};

} // namespace Router
} // namespace Envoy
