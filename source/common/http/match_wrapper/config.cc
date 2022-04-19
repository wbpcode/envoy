#include "source/common/http/match_wrapper/config.h"

#include "envoy/extensions/filters/common/matcher/action/v3/skip_action.pb.h"
#include "envoy/http/filter.h"
#include "envoy/matcher/matcher.h"
#include "envoy/registry/registry.h"

#include "source/common/config/utility.h"
#include "source/common/http/matching/data_impl.h"
#include "source/common/matcher/matcher.h"

#include "absl/status/status.h"

namespace Envoy {
namespace Common {
namespace Http {
namespace MatchWrapper {

namespace {

class SkipAction : public Matcher::ActionBase<
                       envoy::extensions::filters::common::matcher::action::v3::SkipFilter> {};

class MatchTreeValidationVisitor
    : public Matcher::MatchTreeValidationVisitor<Envoy::Http::HttpMatchingData> {
public:
  explicit MatchTreeValidationVisitor(
      const envoy::extensions::filters::common::dependency::v3::MatchingRequirements&
          requirements) {
    if (requirements.has_data_input_allow_list()) {
      data_input_allowlist_ = requirements.data_input_allow_list().type_url();
    }
  }
  absl::Status
  performDataInputValidation(const Matcher::DataInputFactory<Envoy::Http::HttpMatchingData>&,
                             absl::string_view type_url) override {
    if (!data_input_allowlist_) {
      return absl::OkStatus();
    }

    if (std::find(data_input_allowlist_->begin(), data_input_allowlist_->end(), type_url) !=
        data_input_allowlist_->end()) {
      return absl::OkStatus();
    }

    return absl::InvalidArgumentError(
        fmt::format("data input typeUrl {} not permitted according to allowlist", type_url));
  }

private:
  absl::optional<Protobuf::RepeatedPtrField<std::string>> data_input_allowlist_;
};

struct DelegatingFactoryCallbacks : public Envoy::Http::FilterChainFactoryCallbacks {
  DelegatingFactoryCallbacks(Envoy::Http::FilterChainFactoryCallbacks& delegated_callbacks,
                             Matcher::MatchTreePtr<Envoy::Http::HttpMatchingData>& match_tree)
      : delegated_callbacks_(delegated_callbacks), match_tree_(match_tree) {

    Envoy::Http::Matching::HttpMatchingDataImpl match_data(
        delegated_callbacks.streamInfo().downstreamAddressProvider());
    ASSERT(delegated_callbacks.streamInfo().getRequestHeaders() != nullptr);
    match_data.onRequestHeaders(*delegated_callbacks.streamInfo().getRequestHeaders());

    auto result = Matcher::evaluateMatch<Envoy::Http::HttpMatchingData>(*match_tree, match_data);

    ASSERT(result.match_state_ == Matcher::MatchState::MatchComplete);
    ASSERT(result.result_ != nullptr);
    if (result.result_) {
      if (match_result_ = result.result_(); SkipAction().typeUrl() == match_result_->typeUrl()) {
        skip_filter_ = true;
      }
    }
  }

  Event::Dispatcher& dispatcher() override { return delegated_callbacks_.dispatcher(); }
  void addStreamDecoderFilter(Envoy::Http::StreamDecoderFilterSharedPtr filter) override {
    if (skip_filter_) {
      return;
    }
    if (match_result_) {
      filter->onMatchCallback(*match_result_);
    }
    delegated_callbacks_.addStreamDecoderFilter(std::move(filter));
  }
  void addStreamDecoderFilter(
      Envoy::Http::StreamDecoderFilterSharedPtr filter,
      Matcher::MatchTreeSharedPtr<Envoy::Http::HttpMatchingData> match_tree) override {
    delegated_callbacks_.addStreamDecoderFilter(std::move(filter), std::move(match_tree));
  }
  void addStreamEncoderFilter(Envoy::Http::StreamEncoderFilterSharedPtr filter) override {
    if (skip_filter_) {
      return;
    }
    if (match_result_) {
      filter->onMatchCallback(*match_result_);
    }
    delegated_callbacks_.addStreamEncoderFilter(std::move(filter));
  }
  void addStreamEncoderFilter(
      Envoy::Http::StreamEncoderFilterSharedPtr filter,
      Matcher::MatchTreeSharedPtr<Envoy::Http::HttpMatchingData> match_tree) override {
    delegated_callbacks_.addStreamEncoderFilter(std::move(filter), std::move(match_tree));
  }
  void addStreamFilter(Envoy::Http::StreamFilterSharedPtr filter) override {
    if (skip_filter_) {
      return;
    }
    if (match_result_) {
      Envoy::Http::StreamEncoderFilter* base_filter = filter.get();
      base_filter->onMatchCallback(*match_result_);
    }
    delegated_callbacks_.addStreamFilter(std::move(filter));
  }
  // This method now is unnecessary because we we needn't inject match tree to the filter chain manager.
  void
  addStreamFilter(Envoy::Http::StreamFilterSharedPtr filter,
                  Matcher::MatchTreeSharedPtr<Envoy::Http::HttpMatchingData> match_tree) override {
    delegated_callbacks_.addStreamFilter(std::move(filter), std::move(match_tree));
  }
  void addAccessLogHandler(AccessLog::InstanceSharedPtr handler) override {
    delegated_callbacks_.addAccessLogHandler(std::move(handler));
  }
  const StreamInfo::StreamInfo& streamInfo() const override {
    return delegated_callbacks_.streamInfo();
  }

  Envoy::Http::FilterChainFactoryCallbacks& delegated_callbacks_;
  Matcher::MatchTreePtr<Envoy::Http::HttpMatchingData>& match_tree_;

  Matcher::ActionPtr match_result_;
  bool skip_filter_{};
};
} // namespace

Envoy::Http::FilterFactoryCb MatchWrapperConfig::createFilterFactoryFromProtoTyped(
    const envoy::extensions::common::matching::v3::ExtensionWithMatcher& proto_config,
    const std::string& prefix, Server::Configuration::FactoryContext& context) {

  ASSERT(proto_config.has_extension_config());
  auto& factory =
      Config::Utility::getAndCheckFactory<Server::Configuration::NamedHttpFilterConfigFactory>(
          proto_config.extension_config());

  auto message = Config::Utility::translateAnyToFactoryConfig(
      proto_config.extension_config().typed_config(), context.messageValidationVisitor(), factory);
  auto filter_factory = factory.createFilterFactoryFromProto(*message, prefix, context);

  MatchTreeValidationVisitor validation_visitor(*factory.matchingRequirements());

  Envoy::Http::Matching::HttpFilterActionContext action_context{prefix, context};
  Matcher::MatchTreeFactory<Envoy::Http::HttpMatchingData,
                            Envoy::Http::Matching::HttpFilterActionContext>
      matcher_factory(action_context, context.getServerFactoryContext(), validation_visitor);
  Matcher::MatchTreeFactoryCb<Envoy::Http::HttpMatchingData> factory_cb;
  if (proto_config.has_xds_matcher()) {
    factory_cb = matcher_factory.create(proto_config.xds_matcher());
  } else if (proto_config.has_matcher()) {
    factory_cb = matcher_factory.create(proto_config.matcher());
  } else {
    throw EnvoyException("one of `matcher` and `matcher_tree` must be set.");
  }

  if (!validation_visitor.errors().empty()) {
    // TODO(snowp): Output all violations.
    throw EnvoyException(fmt::format("requirement violation while creating match tree: {}",
                                     validation_visitor.errors()[0]));
  }

  return [filter_factory,
          match_tree = std::make_shared<Matcher::MatchTreePtr<Envoy::Http::HttpMatchingData>>(
              factory_cb())](Envoy::Http::FilterChainFactoryCallbacks& callbacks) -> void {
    DelegatingFactoryCallbacks delegated_callbacks(callbacks, *match_tree);
    return filter_factory(delegated_callbacks);
  };
}

/**
 * Static registration for the match wrapper filter. @see RegisterFactory.
 * Note that we register this as a filter in order to serve as a drop in wrapper for other HTTP
 * filters. While not a real filter, by being registered as one all the code paths that look up HTTP
 * filters will look up this filter factory instead, which does the work to create and associate a
 * match tree with the underlying filter.
 */
REGISTER_FACTORY(MatchWrapperConfig, Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace MatchWrapper
} // namespace Http
} // namespace Common
} // namespace Envoy
