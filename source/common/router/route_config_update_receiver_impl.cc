#include "source/common/router/route_config_update_receiver_impl.h"

#include <string>
#include <utility>

#include "envoy/config/route/v3/route.pb.h"
#include "envoy/service/discovery/v3/discovery.pb.h"

#include "source/common/common/assert.h"
#include "source/common/common/fmt.h"
#include "source/common/common/thread.h"
#include "source/common/config/resource_name.h"
#include "source/common/protobuf/utility.h"
#include "source/common/router/config_impl.h"

namespace Envoy {
namespace Router {

namespace {

// Resets 'route_config::virtual_hosts' by merging VirtualHost contained in
// 'rds_vhosts' and 'vhds_vhosts'.
void rebuildRouteConfigVirtualHosts(
    const RouteConfigUpdateReceiverImpl::VirtualHostMap& rds_vhosts,
    const RouteConfigUpdateReceiverImpl::VirtualHostMap& vhds_vhosts,
    envoy::config::route::v3::RouteConfiguration& route_config) {
  route_config.clear_virtual_hosts();
  for (const auto& vhost : rds_vhosts) {
    route_config.mutable_virtual_hosts()->Add()->CheckTypeAndMergeFrom(vhost.second);
  }
  for (const auto& vhost : vhds_vhosts) {
    route_config.mutable_virtual_hosts()->Add()->CheckTypeAndMergeFrom(vhost.second);
  }
}

} // namespace

Rds::ConfigConstSharedPtr ConfigTraitsImpl::createNullConfig() const {
  return std::make_shared<NullConfigImpl>();
}

// TODO(wbpcode): the route configuration doesn't own any resource that needs to be warmed up yet,
// so the init manager is ignored. Once the resources of a route configuration, such as the
// route-level filter configurations, can be warmed up, the init manager should be passed down to
// them.
Rds::ConfigConstSharedPtr
ConfigTraitsImpl::createConfig(const Protobuf::Message& rc,
                               Server::Configuration::ServerFactoryContext& factory_context,
                               Init::Manager&, bool validate_clusters_default) const {
  ASSERT(Envoy::Protobuf::DynamicCastMessage<envoy::config::route::v3::RouteConfiguration>(&rc));
  return THROW_OR_RETURN_VALUE(
      ConfigImpl::create(static_cast<const envoy::config::route::v3::RouteConfiguration&>(rc),
                         factory_context, validator_, validate_clusters_default),
      std::shared_ptr<ConfigImpl>);
}

absl::StatusOr<bool> RouteConfigUpdateReceiverImpl::onRdsUpdate(const Protobuf::Message& rc,
                                                                Init::Manager& init_manager,
                                                                const std::string& version_info) {
  uint64_t new_hash = base_.getHash(rc);
  if (!base_.checkHash(new_hash)) {
    return false;
  }
  auto new_route_config = std::make_unique<envoy::config::route::v3::RouteConfiguration>();
  new_route_config->CheckTypeAndMergeFrom(rc);
  const uint64_t new_vhds_config_hash =
      new_route_config->has_vhds() ? MessageUtil::hash(new_route_config->vhds()) : 0ul;
  if (new_route_config->has_vhds()) {
    // When using VHDS, stash away RDS vhosts, so that they can be merged with VHDS vhosts in
    // onVhdsUpdate.
    if (rds_virtual_hosts_ == nullptr) {
      rds_virtual_hosts_ = std::make_unique<VirtualHostMap>();
    } else {
      rds_virtual_hosts_->clear();
    }
    for (const auto& vhost : new_route_config->virtual_hosts()) {
      rds_virtual_hosts_->emplace(vhost.name(), vhost);
    }
    if (vhds_virtual_hosts_ != nullptr && !vhds_virtual_hosts_->empty()) {
      // If there are vhosts supplied by VHDS, merge them with RDS vhosts.
      rebuildRouteConfigVirtualHosts(*rds_virtual_hosts_, *vhds_virtual_hosts_, *new_route_config);
    }
  }
  base_.updateConfig(std::move(new_route_config), init_manager);
  base_.updateHash(new_hash);
  const bool vhds_configuration_changed = new_vhds_config_hash != last_vhds_config_hash_;
  last_vhds_config_hash_ = new_vhds_config_hash;

  base_.onUpdateCommon(version_info);

  if (!protobufConfigurationCast().has_vhds()) {
    // The new route configuration doesn't use VHDS. Drop the subscription of the previous one, if
    // there was one, so that no more virtual hosts are pushed into a route configuration that
    // didn't ask for them.
    vhds_subscription_.reset();
    return true;
  }
  if (vhds_configuration_changed) {
    ENVOY_LOG(debug,
              "rds: vhds configuration present/changed, (re)starting vhds: config_name={} hash={}",
              protobufConfigurationCast().name(), configHash());
    // (Re)start the VHDS subscription and register it with the init manager of this update, so that
    // the first batch of virtual hosts is fetched before the new route configuration is published.
    // The init manager is dedicated to this update and hasn't been initialized yet, so registering
    // with it is always legal, whether or not this subscription has served an update before.
    auto subscription_or_error = VhdsSubscription::createVhdsSubscription(
        *this, factory_context_, stat_prefix_, route_config_provider_);
    if (!subscription_or_error.ok()) {
      // This route configuration can't have a VHDS subscription. The caller rejects the update, so
      // also drop the subscription of the previous route configuration: the state it would push
      // virtual hosts into has already been replaced by the rejected route configuration.
      vhds_subscription_.reset();
      return subscription_or_error.status();
    }
    // Assigning drops the subscription of the previous route configuration, which signals its init
    // target ready, so it can't leave an abandoned update warming up forever.
    vhds_subscription_ = std::move(subscription_or_error.value());
    vhds_subscription_->registerInitTargetWithInitManager(init_manager);
  }
  return true;
}

bool RouteConfigUpdateReceiverImpl::onVhdsUpdate(
    const VirtualHostRefVector& added_vhosts, std::set<std::string>&& added_resource_ids,
    const Protobuf::RepeatedPtrField<std::string>& removed_resources, Init::Manager& init_manager,
    const std::string& version_info) {
  std::unique_ptr<VirtualHostMap> vhosts_after_this_update;
  if (vhds_virtual_hosts_ != nullptr) {
    vhosts_after_this_update = std::make_unique<VirtualHostMap>(*vhds_virtual_hosts_);
  } else {
    vhosts_after_this_update = std::make_unique<VirtualHostMap>();
  }
  if (rds_virtual_hosts_ == nullptr) {
    rds_virtual_hosts_ = std::make_unique<VirtualHostMap>();
  }
  const bool removed = removeVhosts(*vhosts_after_this_update, removed_resources);
  const bool updated = updateVhosts(*vhosts_after_this_update, added_vhosts);

  const bool vhosts_changed = removed || updated || !added_resource_ids.empty();
  if (vhosts_changed) {
    auto route_config_after_this_update =
        std::make_unique<envoy::config::route::v3::RouteConfiguration>();
    route_config_after_this_update->CheckTypeAndMergeFrom(base_.protobufConfiguration());
    rebuildRouteConfigVirtualHosts(*rds_virtual_hosts_, *vhosts_after_this_update,
                                   *route_config_after_this_update);
    base_.updateConfig(std::move(route_config_after_this_update), init_manager);
  }

  // The new route configuration, if any, was built without throwing, so the state can be updated.
  vhds_virtual_hosts_ = std::move(vhosts_after_this_update);
  resource_ids_in_last_update_ = std::move(added_resource_ids);
  base_.onUpdateCommon(version_info);
  return vhosts_changed;
}

bool RouteConfigUpdateReceiverImpl::removeVhosts(
    VirtualHostMap& vhosts, const Protobuf::RepeatedPtrField<std::string>& removed_vhost_names) {
  bool vhosts_removed = false;
  for (const auto& vhost_name : removed_vhost_names) {
    auto found = vhosts.find(vhost_name);
    if (found != vhosts.end()) {
      vhosts_removed = true;
      vhosts.erase(vhost_name);
    }
  }
  return vhosts_removed;
}

bool RouteConfigUpdateReceiverImpl::updateVhosts(VirtualHostMap& vhosts,
                                                 const VirtualHostRefVector& added_vhosts) {
  bool vhosts_added = false;
  for (const auto& vhost : added_vhosts) {
    auto found = vhosts.find(vhost.get().name());
    if (found != vhosts.end()) {
      vhosts.erase(found);
    }
    vhosts.emplace(vhost.get().name(), vhost.get());
    vhosts_added = true;
  }
  return vhosts_added;
}

} // namespace Router
} // namespace Envoy
