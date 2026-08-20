#include "envoy/extensions/filters/http/router/v3/router.pb.h"
#include "envoy/extensions/filters/http/router/v3/router.pb.validate.h"
#include "envoy/http/filter.h"

#include "source/extensions/filters/http/common/factory_base.h"

#include "test/mocks/server/factory_context.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Common {
namespace {

using ::Envoy::StatusHelpers::HasStatus;
using ::Envoy::StatusHelpers::IsOkAndHolds;
using RouterProto = envoy::extensions::filters::http::router::v3::Router;

// A minimal concrete filter factory used to test the default (non-overridden) behavior of
// FactoryBase. It only implements the pure `createFilterFactoryFromProtoTyped` method and relies on
// the base class defaults for everything else.
class TestFactoryBase : public FactoryBase<RouterProto> {
public:
  TestFactoryBase() : FactoryBase("test.factory_base") {}

  Envoy::Http::FilterFactoryCb
  createFilterFactoryFromProtoTyped(const RouterProto&, const std::string&,
                                    Server::Configuration::FactoryContext&) override {
    return [](Envoy::Http::FilterChainFactoryCallbacks&) {};
  }
};

// A concrete filter factory used to test the default behavior of ExceptionFreeFactoryBase.
class TestExceptionFreeFactoryBase : public ExceptionFreeFactoryBase<RouterProto> {
public:
  TestExceptionFreeFactoryBase() : ExceptionFreeFactoryBase("test.exception_free_factory_base") {}

  absl::StatusOr<Envoy::Http::FilterFactoryCb>
  createFilterFactoryFromProtoTyped(const RouterProto&, const std::string&,
                                    Server::Configuration::FactoryContext&) override {
    return [](Envoy::Http::FilterChainFactoryCallbacks&) {};
  }
};

// A concrete filter factory that only implements createHttpFilterFactoryFromProtoTyped and relies
// on the ExceptionFreeFactoryBase default to bridge the FactoryContext based creation path to it.
class TestHttpOnlyExceptionFreeFactoryBase : public ExceptionFreeFactoryBase<RouterProto> {
public:
  TestHttpOnlyExceptionFreeFactoryBase()
      : ExceptionFreeFactoryBase("test.http_only_exception_free_factory_base") {}

  absl::StatusOr<Envoy::Http::FilterFactoryCb> createHttpFilterFactoryFromProtoTyped(
      const RouterProto&, Server::Configuration::ServerFactoryContext& context,
      Server::Configuration::ExtraFactoryContext& extra_context) override {
    seen_stats_prefix_ = extra_context.stats_prefix;
    seen_init_manager_ = extra_context.init_manager.ptr();
    seen_scope_ = extra_context.scope.ptr();
    seen_scope_or_ = &extra_context.scopeOr(context);
    seen_is_upstream_ = extra_context.is_upstream;
    return [](Envoy::Http::FilterChainFactoryCallbacks&) {};
  }

  std::string seen_stats_prefix_;
  Init::Manager* seen_init_manager_{nullptr};
  Stats::Scope* seen_scope_{nullptr};
  Stats::Scope* seen_scope_or_{nullptr};
  bool seen_is_upstream_{false};
};

// A concrete filter factory used to test the default behavior of DualFactoryBase.
class TestDualFactoryBase : public DualFactoryBase<RouterProto> {
public:
  TestDualFactoryBase() : DualFactoryBase("test.dual_factory_base") {}

  absl::StatusOr<Envoy::Http::FilterFactoryCb>
  createFilterFactoryFromProtoTyped(const RouterProto&, const std::string&, DualInfo,
                                    Server::Configuration::ServerFactoryContext&) override {
    return [](Envoy::Http::FilterChainFactoryCallbacks&) {};
  }
};

// A concrete dual filter factory that only implements createHttpFilterFactoryFromProtoTyped and
// relies on the DualFactoryBase default to bridge the DualInfo based creation paths to it.
class TestHttpOnlyDualFactoryBase : public DualFactoryBase<RouterProto> {
public:
  TestHttpOnlyDualFactoryBase() : DualFactoryBase("test.http_only_dual_factory_base") {}

  absl::StatusOr<Envoy::Http::FilterFactoryCb> createHttpFilterFactoryFromProtoTyped(
      const RouterProto&, Server::Configuration::ServerFactoryContext&,
      Server::Configuration::ExtraFactoryContext& extra_context) override {
    seen_stats_prefix_ = extra_context.stats_prefix;
    seen_init_manager_ = extra_context.init_manager.ptr();
    seen_scope_ = extra_context.scope.ptr();
    seen_is_upstream_ = extra_context.is_upstream;
    return [](Envoy::Http::FilterChainFactoryCallbacks&) {};
  }

  std::string seen_stats_prefix_;
  Init::Manager* seen_init_manager_{nullptr};
  Stats::Scope* seen_scope_{nullptr};
  bool seen_is_upstream_{false};
};

// Exercises the shared CommonFactoryBase helpers: proto factory methods, name and the default
// terminal-filter and route-config behaviors.
TEST(FactoryBaseTest, CommonBehavior) {
  TestFactoryBase factory;
  testing::NiceMock<Server::Configuration::MockServerFactoryContext> server_context;

  EXPECT_EQ("test.factory_base", factory.name());

  // Empty config/route-config protos are created with the templated proto types.
  EXPECT_NE(nullptr, factory.createEmptyConfigProto());
  EXPECT_NE(nullptr, factory.createEmptyRouteConfigProto());

  // The default terminal-filter implementation returns false.
  RouterProto proto_config;
  EXPECT_FALSE(factory.isTerminalFilterByProto(proto_config, server_context));

  // The default route-specific config implementation returns a nullptr config.
  auto route_config = factory.createRouteSpecificFilterConfig(
      proto_config, server_context, server_context.messageValidationVisitor());
  ASSERT_THAT(route_config, IsOkAndHolds(nullptr));
}

// FactoryBase falls back to createFilterFactoryFromProtoWithServerContextTyped for the
// server-context based creation, which by default throws.
TEST(FactoryBaseTest, ServerContextNotSupported) {
  TestFactoryBase factory;
  testing::NiceMock<Server::Configuration::MockServerFactoryContext> server_context;
  RouterProto proto_config;

  EXPECT_THROW_WITH_MESSAGE(
      factory.createFilterFactoryFromProtoWithServerContext(proto_config, "stats", server_context),
      EnvoyException, "Creating filter factory from server factory context is not supported");
  Server::Configuration::ExtraFactoryContext extra_context{
      server_context.messageValidationVisitor(), "stats"};

  // createHttpFilterFactoryFromProto delegates to the typed variant, which in turn delegates to the
  // (throwing) server-context implementation.
  EXPECT_THROW_WITH_MESSAGE(
      factory.createHttpFilterFactoryFromProto(proto_config, server_context, extra_context)
          .IgnoreError(),
      EnvoyException, "Creating filter factory from server factory context is not supported");
}

// FactoryBase's downstream FactoryContext path works and returns a valid factory callback.
TEST(FactoryBaseTest, FactoryContextCreation) {
  TestFactoryBase factory;
  testing::NiceMock<Server::Configuration::MockFactoryContext> context;
  RouterProto proto_config;

  auto cb = factory.createFilterFactoryFromProto(proto_config, "stats", context);
  ASSERT_THAT(cb, IsOkAndHolds(::testing::NotNull()));
}

// ExceptionFreeFactoryBase returns an error status (rather than throwing) when server-context based
// creation is not supported.
TEST(FactoryBaseTest, ExceptionFreeServerContextNotSupported) {
  TestExceptionFreeFactoryBase factory;
  testing::NiceMock<Server::Configuration::MockServerFactoryContext> server_context;
  RouterProto proto_config;

  EXPECT_EQ("test.exception_free_factory_base", factory.name());
  Server::Configuration::ExtraFactoryContext extra_context{
      server_context.messageValidationVisitor(), "stats"};

  auto result =
      factory.createHttpFilterFactoryFromProto(proto_config, server_context, extra_context);
  EXPECT_THAT(
      result,
      HasStatus(absl::StatusCode::kInvalidArgument,
                "Creating HTTP filter factory from server factory context is not supported"));
}

// ExceptionFreeFactoryBase's downstream FactoryContext path works.
TEST(FactoryBaseTest, ExceptionFreeFactoryContextCreation) {
  TestExceptionFreeFactoryBase factory;
  testing::NiceMock<Server::Configuration::MockFactoryContext> context;
  RouterProto proto_config;

  auto cb = factory.createFilterFactoryFromProto(proto_config, "stats", context);
  ASSERT_THAT(cb, IsOkAndHolds(::testing::NotNull()));
}

// DualFactoryBase's downstream and upstream FactoryContext paths both work.
TEST(FactoryBaseTest, DualFactoryContextCreation) {
  TestDualFactoryBase factory;
  RouterProto proto_config;

  EXPECT_EQ("test.dual_factory_base", factory.name());

  testing::NiceMock<Server::Configuration::MockFactoryContext> downstream_context;
  auto downstream_cb =
      factory.createFilterFactoryFromProto(proto_config, "stats", downstream_context);
  ASSERT_THAT(downstream_cb, IsOkAndHolds(::testing::NotNull()));

  testing::NiceMock<Server::Configuration::MockUpstreamFactoryContext> upstream_context;
  auto upstream_cb = factory.createFilterFactoryFromProto(proto_config, "stats", upstream_context);
  ASSERT_THAT(upstream_cb, IsOkAndHolds(::testing::NotNull()));
}

// DualFactoryBase falls back to the (throwing) server-context typed implementation for both the
// server-context and HTTP filter factory creation paths.
TEST(FactoryBaseTest, DualServerContextNotSupported) {
  TestDualFactoryBase factory;
  testing::NiceMock<Server::Configuration::MockServerFactoryContext> server_context;
  RouterProto proto_config;

  EXPECT_THROW_WITH_MESSAGE(
      factory.createFilterFactoryFromProtoWithServerContext(proto_config, "stats", server_context),
      EnvoyException,
      "DualFactoryBase: creating filter factory from server factory context is not supported");
  Server::Configuration::ExtraFactoryContext extra_context{
      server_context.messageValidationVisitor(), "stats"};

  EXPECT_THROW_WITH_MESSAGE(
      factory.createHttpFilterFactoryFromProto(proto_config, server_context, extra_context)
          .IgnoreError(),
      EnvoyException,
      "DualFactoryBase: creating filter factory from server factory context is not supported");
}

// The DualFactoryBase default createFilterFactoryFromProtoTyped bridges both the downstream and
// upstream DualInfo paths to createHttpFilterFactoryFromProtoTyped, forwarding the stats prefix,
// init manager, scope and upstream-ness of the DualInfo via the ExtraFactoryContext.
TEST(FactoryBaseTest, DualFactoryDelegatesToHttpFilterFactory) {
  RouterProto proto_config;

  {
    TestHttpOnlyDualFactoryBase factory;
    testing::NiceMock<Server::Configuration::MockFactoryContext> downstream_context;
    auto cb =
        factory.createFilterFactoryFromProto(proto_config, "downstream_stats", downstream_context);
    ASSERT_THAT(cb, IsOkAndHolds(::testing::NotNull()));
    EXPECT_EQ("downstream_stats", factory.seen_stats_prefix_);
    EXPECT_EQ(&downstream_context.initManager(), factory.seen_init_manager_);
    EXPECT_EQ(&downstream_context.scope(), factory.seen_scope_);
    EXPECT_FALSE(factory.seen_is_upstream_);
  }

  {
    TestHttpOnlyDualFactoryBase factory;
    testing::NiceMock<Server::Configuration::MockUpstreamFactoryContext> upstream_context;
    auto cb =
        factory.createFilterFactoryFromProto(proto_config, "upstream_stats", upstream_context);
    ASSERT_THAT(cb, IsOkAndHolds(::testing::NotNull()));
    EXPECT_EQ("upstream_stats", factory.seen_stats_prefix_);
    EXPECT_EQ(&upstream_context.initManager(), factory.seen_init_manager_);
    EXPECT_EQ(&upstream_context.scope(), factory.seen_scope_);
    EXPECT_TRUE(factory.seen_is_upstream_);
  }
}

// The ExceptionFreeFactoryBase default createFilterFactoryFromProtoTyped bridges the
// FactoryContext based path to createHttpFilterFactoryFromProtoTyped, forwarding the stats prefix,
// init manager and scope of the FactoryContext via the ExtraFactoryContext. is_upstream is always
// false on this path, since ExceptionFreeFactoryBase is downstream only.
TEST(FactoryBaseTest, ExceptionFreeFactoryDelegatesToHttpFilterFactory) {
  TestHttpOnlyExceptionFreeFactoryBase factory;
  RouterProto proto_config;
  testing::NiceMock<Server::Configuration::MockFactoryContext> context;

  auto cb = factory.createFilterFactoryFromProto(proto_config, "listener_stats", context);
  ASSERT_THAT(cb, IsOkAndHolds(::testing::NotNull()));
  EXPECT_EQ("listener_stats", factory.seen_stats_prefix_);
  EXPECT_EQ(&context.initManager(), factory.seen_init_manager_);
  EXPECT_EQ(&context.scope(), factory.seen_scope_);
  // With a scope present, scopeOr() returns it rather than the server scope.
  EXPECT_EQ(&context.scope(), factory.seen_scope_or_);
  EXPECT_FALSE(factory.seen_is_upstream_);
}

// On the route/embedded path the caller does not provide a scope, so ExtraFactoryContext::scopeOr
// falls back to the scope of the ServerFactoryContext.
TEST(FactoryBaseTest, ExtraFactoryContextScopeFallsBackToServerScope) {
  TestHttpOnlyExceptionFreeFactoryBase factory;
  RouterProto proto_config;
  testing::NiceMock<Server::Configuration::MockServerFactoryContext> server_context;
  Server::Configuration::ExtraFactoryContext extra_context{
      server_context.messageValidationVisitor(), "route_stats"};

  auto cb = factory.createHttpFilterFactoryFromProto(proto_config, server_context, extra_context);
  ASSERT_THAT(cb, IsOkAndHolds(::testing::NotNull()));
  EXPECT_EQ("route_stats", factory.seen_stats_prefix_);
  EXPECT_EQ(nullptr, factory.seen_init_manager_);
  EXPECT_EQ(nullptr, factory.seen_scope_);
  EXPECT_EQ(&server_context.scope(), factory.seen_scope_or_);
  EXPECT_FALSE(factory.seen_is_upstream_);
}

} // namespace
} // namespace Common
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
