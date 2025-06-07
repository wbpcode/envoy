#pragma once

#include "envoy/extensions/router/cluster_specifiers/matcher/v3/matcher.pb.h"
#include "envoy/router/cluster_specifier_plugin.h"
#include "source/common/matcher/matcher.h"

namespace Envoy {
namespace Extensions {
namespace Router {
namespace Matcher {

using MatcherClusterSpecifierConfigProto =
    envoy::extensions::router::cluster_specifiers::matcher::v3::MatcherClusterSpecifier;
using ClusterActionProto =
    envoy::extensions::router::cluster_specifiers::matcher::v3::ClusterAction;

struct ClusterActionContext {};

class ClusterAction : public Envoy::Matcher::ActionBase<envoy::config::route::v3::Route> {
public:
  explicit ClusterAction(std::shared_ptr<std::string> cluster) : cluster_(cluster) {}

  absl::string_view cluster() const { return *cluster_; }

private:
  const std::shared_ptr<std::string> cluster_;
};

// Registered factory for ClusterAction.
class ClusterActionFactory : public Envoy::Matcher::ActionFactory<ClusterActionContext> {
public:
  Envoy::Matcher::ActionFactoryCb
  createActionFactoryCb(const Protobuf::Message& config, ClusterActionContext& context,
                        ProtobufMessage::ValidationVisitor& validation_visitor) override;
  std::string name() const override { return "cluster"; }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<ClusterActionProto>();
  }
};
DECLARE_FACTORY(ClusterActionFactory);

class MatcherClusterSpecifierPlugin : public Envoy::Router::ClusterSpecifierPlugin,
                                      Logger::Loggable<Logger::Id::router> {
public:
  MatcherClusterSpecifierPlugin(
      Envoy::Matcher::MatchTreeSharedPtr<Http::HttpMatchingData> match_tree)
      : match_tree_(match_tree) {}
  Envoy::Router::RouteConstSharedPtr route(Envoy::Router::RouteConstSharedPtr parent,
                                           const Http::RequestHeaderMap& header) const override;

private:
  Envoy::Matcher::MatchTreeSharedPtr<Http::HttpMatchingData> match_tree_;
};

} // namespace Matcher
} // namespace Router
} // namespace Extensions
} // namespace Envoy
