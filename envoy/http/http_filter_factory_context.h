#pragma once

#include "envoy/server/factory_context.h"

#include "source/common/common/assert.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Http {

/**
 * Factory context that is used to create the HTTP filters of the HTTP connection manager. Every
 * method is delegated to the factory context of the network filter chain, except prefixedScope(),
 * which returns the scope that is given at construction time.
 *
 * The HTTP connection manager uses this to hand its filters the 'http.<stat_prefix>.' scope, so
 * that they no longer need to prepend that prefix to their stat names themselves. The filters
 * never see this context directly: Server::Configuration::ExtraFactoryContext::create() recognizes
 * it and exposes the scope as the prefixed scope of the extra context. See
 * envoy/server/filter_config.h.
 *
 * The scope only stands in for one specific stats prefix, the one it was created from, which is
 * why that prefix is carried alongside it: a filter that is created with a different prefix, such
 * as an ECDS filter with its 'extension_config_discovery.http_filter.<name>.' prefix, must not be
 * given this scope, or its stats would both lose their own prefix and move under this one.
 */
class HttpFilterFactoryContext : public Server::Configuration::FactoryContext {
public:
  /**
   * @param context the factory context of the network filter chain that everything is delegated to.
   * @param prefixed_scope the scope named after stats_prefix.
   * @param stats_prefix the stats prefix that prefixed_scope stands in for. It is copied, so it
   *        need not outlive this context.
   */
  HttpFilterFactoryContext(Server::Configuration::FactoryContext& context,
                           Stats::ScopeSharedPtr prefixed_scope, absl::string_view stats_prefix)
      : context_(context), prefixed_scope_(std::move(prefixed_scope)), stats_prefix_(stats_prefix) {
    ASSERT(prefixed_scope_ != nullptr);
  }

  /**
   * @return the stats prefix that prefixedScope() stands in for. Only the filters that are created
   *         with this very prefix may be given that scope.
   */
  absl::string_view statsPrefix() const { return stats_prefix_; }

  // Server::Configuration::GenericFactoryContext
  Server::Configuration::ServerFactoryContext& serverFactoryContext() override {
    return context_.serverFactoryContext();
  }
  ProtobufMessage::ValidationVisitor& messageValidationVisitor() override {
    return context_.messageValidationVisitor();
  }
  Init::Manager& initManager() override { return context_.initManager(); }
  Stats::Scope& scope() override { return context_.scope(); }

  // Server::Configuration::FactoryContext
  const Network::DrainDecision& drainDecision() override { return context_.drainDecision(); }
  envoy::config::core::v3::TrafficDirection direction() const override {
    return context_.direction();
  }
  bool isQuic() const override { return context_.isQuic(); }
  bool shouldBypassOverloadManager() const override {
    return context_.shouldBypassOverloadManager();
  }
  Stats::Scope& prefixedScope() override { return *prefixed_scope_; }

private:
  Server::Configuration::FactoryContext& context_;
  const Stats::ScopeSharedPtr prefixed_scope_;
  const std::string stats_prefix_;
};

using HttpFilterFactoryContextPtr = std::unique_ptr<HttpFilterFactoryContext>;

} // namespace Http
} // namespace Envoy
