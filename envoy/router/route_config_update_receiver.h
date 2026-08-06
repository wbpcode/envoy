#pragma once

#include <memory>
#include <optional>

#include "envoy/common/pure.h"
#include "envoy/common/time.h"
#include "envoy/config/route/v3/route.pb.h"
#include "envoy/init/manager.h"
#include "envoy/rds/route_config_update_receiver.h"
#include "envoy/service/discovery/v3/discovery.pb.h"

#include "source/common/protobuf/protobuf.h"

namespace Envoy {
namespace Router {

/**
 * A primitive that keeps track of updates to a RouteConfiguration.
 */
class RouteConfigUpdateReceiver : public Rds::RouteConfigUpdateReceiver {
public:
  /**
   * Same purpose as Rds::RouteConfigUpdateReceiver::protobufConfiguration()
   * but the return is downcasted to proper type.
   * @return current RouteConfiguration downcasted from Protobuf::Message&
   */
  virtual const envoy::config::route::v3::RouteConfiguration&
  protobufConfigurationCast() const PURE;

  using VirtualHostRefVector =
      std::vector<std::reference_wrapper<const envoy::config::route::v3::VirtualHost>>;

  /**
   * Called on updates via VHDS.
   * @param added_vhosts supplies VirtualHosts that have been added.
   * @param added_resource_ids set of resources IDs (names + aliases) added.
   * @param removed_resources supplies names of VirtualHosts that have been removed.
   * @param init_manager supplies the init manager that is used to warm up the resources of the
   * new RouteConfiguration. Every update has its own independent init manager and the caller is
   * responsible for keeping it alive until the new RouteConfiguration is warmed up and published.
   * @param version_info supplies RouteConfiguration version.
   * @return bool whether RouteConfiguration has been updated.
   */
  virtual bool onVhdsUpdate(const VirtualHostRefVector& added_vhosts,
                            std::set<std::string>&& added_resource_ids,
                            const Protobuf::RepeatedPtrField<std::string>& removed_resources,
                            Init::Manager& init_manager, const std::string& version_info) PURE;

  /**
   * The route configuration provider that the VHDS subscription owned by this receiver publishes
   * to. The provider is created after the receiver, so it binds itself here once it exists. It is
   * null until then, and stays null for a receiver whose route configuration never uses VHDS.
   */
  virtual Rds::RouteConfigProvider*& routeConfigProvider() PURE;

  /**
   * Requests an on-demand VHDS update for the given aliases. A no-op if the current route
   * configuration doesn't use VHDS.
   * @param aliases supplies the aliases to request.
   */
  virtual void updateOnDemand(const std::string& aliases) PURE;

  /**
   * @return the union of all resource names and aliases (if any) received with the last VHDS
   * update.
   */
  virtual const std::set<std::string>& resourceIdsInLastVhdsUpdate() const PURE;
};

using RouteConfigUpdatePtr = std::unique_ptr<RouteConfigUpdateReceiver>;
} // namespace Router
} // namespace Envoy
