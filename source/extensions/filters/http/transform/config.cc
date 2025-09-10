#include "source/extensions/filters/http/transform/config.h"

#include <memory>

#include "envoy/registry/registry.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Transform {

absl::StatusOr<Http::FilterFactoryCb>
TransformFactoryConfig::createFilterFactoryFromProtoTyped(
    const ProtoConfig& config, const std::string&, DualInfo,
    Server::Configuration::ServerFactoryContext&) {
  absl::Status creation_status = absl::OkStatus();
  auto filter_config = std::make_shared<TransformConfig>(config, creation_status);
  RETURN_IF_NOT_OK_REF(creation_status);

  return [filter_config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(std::make_shared<Transform>(filter_config));
  };
}

absl::StatusOr<Router::RouteSpecificFilterConfigConstSharedPtr>
TransformFactoryConfig::createRouteSpecificFilterConfigTyped(
    const PerRouteProtoConfig& proto_config, Server::Configuration::ServerFactoryContext&,
    ProtobufMessage::ValidationVisitor&) {
  absl::Status creation_status = absl::OkStatus();
  auto route_config = std::make_shared<PerRouteTransform>(proto_config, creation_status);
  RETURN_IF_NOT_OK_REF(creation_status);
  return route_config;
}

using UpstreamTransformFactoryConfig = TransformFactoryConfig;

REGISTER_FACTORY(TransformFactoryConfig, Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace Transform
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
