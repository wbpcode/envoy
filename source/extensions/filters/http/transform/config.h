#pragma once

#include "envoy/extensions/filters/http/transform/v3/transform.pb.h"
#include "envoy/extensions/filters/http/transform/v3/transform.pb.validate.h"

#include "source/extensions/filters/http/common/factory_base.h"
#include "source/extensions/filters/http/transform/transform.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Transform {

/**
 * Config registration for the stateful session filter. @see NamedHttpFilterConfigFactory.
 */
class TransformFactoryConfig
    : public Common::DualFactoryBase<ProtoConfig, PerRouteProtoConfig> {
public:
  TransformFactoryConfig() : DualFactoryBase("envoy.filters.http.header_mutation") {}

private:
  absl::StatusOr<Http::FilterFactoryCb>
  createFilterFactoryFromProtoTyped(const ProtoConfig& proto_config,
                                    const std::string& stats_prefix, DualInfo info,
                                    Server::Configuration::ServerFactoryContext& context) override;
  absl::StatusOr<Router::RouteSpecificFilterConfigConstSharedPtr>
  createRouteSpecificFilterConfigTyped(const PerRouteProtoConfig& proto_config,
                                       Server::Configuration::ServerFactoryContext&,
                                       ProtobufMessage::ValidationVisitor&) override;
};

} // namespace Transform
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
