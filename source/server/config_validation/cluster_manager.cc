#include "source/server/config_validation/cluster_manager.h"

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/core/v3/config_source.pb.h"

#include "source/common/common/utility.h"

namespace Envoy {
namespace Upstream {

ClusterManagerPtr ValidationClusterManagerFactory::clusterManagerFromProto(
    const envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
  auto cluster_manager = std::unique_ptr<ValidationClusterManager>{new ValidationClusterManager(
      bootstrap, *this, context_.stats(), context_.threadLocal(), context_.runtime(),
      context_.localInfo(), context_.accessLogManager(), context_.dispatcher(), context_.admin(),
      context_.messageValidationContext(), context_.api(), context_.httpContext(),
      context_.grpcContext(), context_.routerContext(), context_)};
  cluster_manager->init(bootstrap);
  return cluster_manager;
}

CdsApiPtr ValidationClusterManagerFactory::createCds(
    const envoy::config::core::v3::ConfigSource& cds_config,
    const xds::core::v3::ResourceLocator* cds_resources_locator, ClusterManager& cm) {
  // Create the CdsApiImpl...
  ProdClusterManagerFactory::createCds(cds_config, cds_resources_locator, cm);
  // ... and then throw it away, so that we don't actually connect to it.
  return nullptr;
}

} // namespace Upstream
} // namespace Envoy
