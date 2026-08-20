#pragma once

#include <string>

#include "envoy/server/filter_config.h"

#include "source/common/protobuf/protobuf.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Common {

/**
 * Config registration for http filters that have empty configuration blocks.
 * The boiler plate instantiation functions (createFilterFactory, createFilterFactoryFromProto,
 * and createEmptyConfigProto) are implemented here. Users of this class have to implement
 * the createFilter function that instantiates the actual filter.
 */
class EmptyHttpFilterConfig : public Server::Configuration::NamedHttpFilterConfigFactory {
public:
  virtual absl::StatusOr<Http::FilterFactoryCb>
  createFilter(const std::string& stat_prefix, Server::Configuration::FactoryContext& context) PURE;

  absl::StatusOr<Http::FilterFactoryCb>
  createFilterFactoryFromProto(const Protobuf::Message&, const std::string& stat_prefix,
                               Server::Configuration::FactoryContext& context) override {
    return createFilter(stat_prefix, context);
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    // Using Struct instead of a custom filter config proto. This is only allowed in tests.
    return ProtobufTypes::MessagePtr{new Envoy::Protobuf::Struct()};
  }

  std::set<std::string> configTypes() override { return {}; }

  std::string name() const override { return name_; }

protected:
  EmptyHttpFilterConfig(const std::string& name) : name_(name) {}

private:
  const std::string name_;
};

class UpstreamFilterConfig : public Server::Configuration::UpstreamHttpFilterConfigFactory {
public:
  virtual absl::StatusOr<Http::FilterFactoryCb>
  createDualFilter(const std::string& stat_prefix,
                   Server::Configuration::ServerFactoryContext& context) PURE;

  absl::StatusOr<Http::FilterFactoryCb>
  createFilterFactoryFromProto(const Protobuf::Message&, const std::string& stat_prefix,
                               Server::Configuration::UpstreamFactoryContext& context) override {
    return createDualFilter(stat_prefix, context.serverFactoryContext());
  }
};

class EmptyHttpDualFilterConfig : public EmptyHttpFilterConfig, public UpstreamFilterConfig {
public:
  EmptyHttpDualFilterConfig(const std::string& name) : EmptyHttpFilterConfig(name) {}

  absl::StatusOr<Http::FilterFactoryCb>
  createFilter(const std::string& stat_prefix,
               Server::Configuration::FactoryContext& context) override {
    return createDualFilter(stat_prefix, context.serverFactoryContext());
  }

  absl::StatusOr<Http::FilterFactoryCb> createHttpFilterFactoryFromProto(
      const Protobuf::Message& config, Server::Configuration::ServerFactoryContext&,
      Server::Configuration::ExtraFactoryContext& extra_context) override {
    if (extra_context.compatible_downstream_context.has_value()) {
      return EmptyHttpFilterConfig::createFilterFactoryFromProto(
          config, extra_context.stats_prefix, extra_context.compatible_downstream_context.ref());
    }
    if (extra_context.compatible_upstream_context.has_value()) {
      return UpstreamFilterConfig::createFilterFactoryFromProto(
          config, extra_context.stats_prefix, extra_context.compatible_upstream_context.ref());
    }
    return absl::UnimplementedError(
        "createHttpFilterFactoryFromProto is not implemented for the server factory context only");
  }
};

template <class ProtoType>
class UniqueEmptyHttpFilterConfig : public Server::Configuration::NamedHttpFilterConfigFactory {
public:
  virtual absl::StatusOr<Http::FilterFactoryCb>
  createFilter(const std::string& stat_prefix, Server::Configuration::FactoryContext& context) PURE;

  absl::StatusOr<Http::FilterFactoryCb>
  createFilterFactoryFromProto(const Protobuf::Message&, const std::string& stat_prefix,
                               Server::Configuration::FactoryContext& context) override {
    return createFilter(stat_prefix, context);
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return ProtobufTypes::MessagePtr{new ProtoType()};
  }

  std::string name() const override { return name_; }

protected:
  UniqueEmptyHttpFilterConfig(const std::string& name) : name_(name) {}

private:
  const std::string name_;
};

template <class ProtoType>
class UniqueEmptyHttpDualFilterConfig : public UniqueEmptyHttpFilterConfig<ProtoType>,
                                        public UpstreamFilterConfig {
public:
  UniqueEmptyHttpDualFilterConfig(const std::string& name)
      : UniqueEmptyHttpFilterConfig<ProtoType>(name) {}

  absl::StatusOr<Http::FilterFactoryCb>
  createFilter(const std::string& stat_prefix,
               Server::Configuration::FactoryContext& context) override {
    return createDualFilter(stat_prefix, context.serverFactoryContext());
  }

  // Both NamedHttpFilterConfigFactory and UpstreamHttpFilterConfigFactory override
  // createHttpFilterFactoryFromProto, so the ambiguity must be resolved here.
  absl::StatusOr<Http::FilterFactoryCb> createHttpFilterFactoryFromProto(
      const Protobuf::Message& config, Server::Configuration::ServerFactoryContext&,
      Server::Configuration::ExtraFactoryContext& extra_context) override {
    if (extra_context.compatible_downstream_context.has_value()) {
      return UniqueEmptyHttpFilterConfig<ProtoType>::createFilterFactoryFromProto(
          config, extra_context.stats_prefix, extra_context.compatible_downstream_context.ref());
    }
    if (extra_context.compatible_upstream_context.has_value()) {
      return UpstreamFilterConfig::createFilterFactoryFromProto(
          config, extra_context.stats_prefix, extra_context.compatible_upstream_context.ref());
    }
    return absl::UnimplementedError(
        "createHttpFilterFactoryFromProto is not implemented for the server factory context only");
  }
};

} // namespace Common
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
