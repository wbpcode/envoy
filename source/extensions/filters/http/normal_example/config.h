#pragma once

#include "extensions/filters/http/common/factory_base.h"
#include "extensions/filters/http/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace NormalExample {

/**
 * Config registration for the header-to-metadata filter. @see NamedHttpFilterConfigFactory.
 */
class Config : public Server::Configuration::NamedHttpFilterConfigFactory {
public:
  Config() = default;

  Http::FilterFactoryCb
  createFilterFactoryFromProto(const Protobuf::Message& config, const std::string& stat_prefix,
                               Server::Configuration::FactoryContext& context) override;

  virtual ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<Protobuf::Any>();
  }

  std::string name() const override { return "normal_example"; }
};

} // namespace NormalExample
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
