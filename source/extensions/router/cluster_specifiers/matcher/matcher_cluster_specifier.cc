#include "source/extensions/router/cluster_specifiers/matcher/matcher_cluster_specifier.h"

#include "source/common/router/config_impl.h"

namespace Envoy {
namespace Extensions {
namespace Router {
namespace Matcher {

Envoy::Matcher::ActionFactoryCb ClusterActionFactory::createActionFactoryCb(
    const Protobuf::Message& config, ClusterActionContext&,
    ProtobufMessage::ValidationVisitor& validation_visitor) {
  const auto& proto_config =
      MessageUtil::downcastAndValidate<const ClusterActionProto&>(config, validation_visitor);
  auto cluster = std::make_shared<std::string>(proto_config.cluster());

  return [cluster]() { return std::make_unique<ClusterAction>(cluster); };
}

class MatcherRouteEntry : public Envoy::Router::RouteEntryImplBase::DynamicRouteEntry {
public:
  MatcherRouteEntry(Envoy::Router::RouteConstSharedPtr parent,
                    Envoy::Matcher::MatchTreeSharedPtr<Http::HttpMatchingData> match_tree)
      : DynamicRouteEntry(dynamic_cast<const Envoy::Router::RouteEntryImplBase*>(parent.get()),
                          parent, ""),
        match_tree_(std::move(match_tree)) {}

  void refreshRouteCluster(const Http::RequestHeaderMap& headers,
                           const StreamInfo::StreamInfo& stream_info) const {
    Http::Matching::HttpMatchingDataImpl data(stream_info);
    data.onRequestHeaders(headers);

    Envoy::Matcher::MatchResult match_result =
        Envoy::Matcher::evaluateMatch<Http::HttpMatchingData>(*match_tree_, data);

    if (!match_result.isMatch()) {
      return;
    }

    const Envoy::Matcher::ActionPtr result = match_result.action();
    const ClusterAction& cluster_action = result->getTyped<ClusterAction>();
    cluster_name_ = std::string(cluster_action.cluster());
  }

private:
  Envoy::Matcher::MatchTreeSharedPtr<Http::HttpMatchingData> match_tree_;
};

Envoy::Router::RouteConstSharedPtr
MatcherClusterSpecifierPlugin::route(Envoy::Router::RouteConstSharedPtr parent,
                                     const Http::RequestHeaderMap& headers) const {}

} // namespace Matcher
} // namespace Router
} // namespace Extensions
} // namespace Envoy
