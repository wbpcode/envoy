#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/listener/v3/listener_components.pb.h"
#include "envoy/extensions/filters/network/rbac/v3/rbac.pb.h"
#include "envoy/extensions/filters/network/rbac/v3/rbac.pb.validate.h"

#include "source/extensions/filters/network/rbac/config.h"

#include "test/integration/integration.h"
#include "test/test_common/environment.h"

#include "fmt/printf.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RBAC {
namespace {

const std::string FILTER_STATE_SETTER_CONFIG = R"EOF(
name: test-filter-state-setter
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.network.set_filter_state.v3.Config
  on_new_connection:
    object_key: "test_key"
    factory_key: "envoy.string"
    format_string:
      text_format_source:
        inline_string: "deny_value"
    read_only: false
)EOF";

std::string rbac_config;
} // namespace

class RoleBasedAccessControlNetworkFilterIntegrationTest
    : public testing::TestWithParam<Network::Address::IpVersion>,
      public BaseIntegrationTest {
public:
  RoleBasedAccessControlNetworkFilterIntegrationTest()
      : BaseIntegrationTest(GetParam(), rbac_config) {}

  static void SetUpTestSuite() { // NOLINT(readability-identifier-naming)
    // Enable debug logging for all loggers to ensure coverage of debug log statements
    Envoy::Logger::Registry::setLogLevel(spdlog::level::debug);

    rbac_config = absl::StrCat(ConfigHelper::baseConfig(), R"EOF(
    filter_chains:
      filters:
       -  name: rbac
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.filters.network.rbac.v3.RBAC
            stat_prefix: tcp.
            rules:
              policies:
                "foo":
                  permissions:
                    - any: true
                  principals:
                    - not_id:
                        any: true
       -  name: envoy.filters.network.echo
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.filters.network.echo.v3.Echo
)EOF");
  }

  void initializeFilter(const std::string& config) {
    config_helper_.addConfigModifier([config](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      envoy::config::listener::v3::Filter filter;
      TestUtility::loadFromYaml(config, filter);
      ASSERT_GT(bootstrap.mutable_static_resources()->listeners_size(), 0);
      auto l = bootstrap.mutable_static_resources()->mutable_listeners(0);
      ASSERT_GT(l->filter_chains_size(), 0);
      ASSERT_GT(l->filter_chains(0).filters_size(), 0);
      l->mutable_filter_chains(0)->mutable_filters(0)->Swap(&filter);
    });

    BaseIntegrationTest::initialize();
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, RoleBasedAccessControlNetworkFilterIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(RoleBasedAccessControlNetworkFilterIntegrationTest, Allowed) {
  initializeFilter(R"EOF(
name: rbac
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.network.rbac.v3.RBAC
  stat_prefix: tcp.
  rules:
    policies:
      "allow_all":
        permissions:
          - any: true
        principals:
          - any: true
  shadow_rules:
    policies:
      "deny_all":
        permissions:
          - any: true
        principals:
          - not_id:
              any: true
)EOF");
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  ASSERT_TRUE(tcp_client->write("hello"));
  ASSERT_TRUE(tcp_client->connected());
  tcp_client->close();

  test_server_->waitForCounterGe("tcp.rbac.allowed", 1);
  EXPECT_EQ(0U, test_server_->counter("tcp.rbac.denied")->value());
  EXPECT_EQ(0U, test_server_->counter("tcp.rbac.shadow_allowed")->value());
  test_server_->waitForCounterGe("tcp.rbac.shadow_denied", 1);
}

TEST_P(RoleBasedAccessControlNetworkFilterIntegrationTest, Denied) {
  initializeFilter(R"EOF(
name: rbac
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.network.rbac.v3.RBAC
  stat_prefix: tcp.
  rules:
    policies:
      "deny_all":
        permissions:
          - any: true
        principals:
          - not_id:
              any: true
  shadow_rules:
    policies:
      "allow_all":
        permissions:
          - any: true
        principals:
          - any: true
)EOF");
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  ASSERT_TRUE(tcp_client->write("hello", false, false));
  tcp_client->waitForDisconnect();

  EXPECT_EQ(0U, test_server_->counter("tcp.rbac.allowed")->value());
  EXPECT_EQ(1U, test_server_->counter("tcp.rbac.denied")->value());
  EXPECT_EQ(1U, test_server_->counter("tcp.rbac.shadow_allowed")->value());
  EXPECT_EQ(0U, test_server_->counter("tcp.rbac.shadow_denied")->value());
}

TEST_P(RoleBasedAccessControlNetworkFilterIntegrationTest, DelayDenied) {
  initializeFilter(R"EOF(
name: rbac
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.network.rbac.v3.RBAC
  stat_prefix: tcp.
  rules:
    policies:
      "deny_all":
        permissions:
          - any: true
        principals:
          - not_id:
              any: true
  delay_deny: 5s
)EOF");
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  ASSERT_TRUE(tcp_client->write("hello", false, false));
  ASSERT_TRUE(tcp_client->connected());

  timeSystem().advanceTimeWait(std::chrono::seconds(3));
  ASSERT_TRUE(tcp_client->connected());

  timeSystem().advanceTimeWait(std::chrono::seconds(6));
  tcp_client->waitForDisconnect();

  EXPECT_EQ(0U, test_server_->counter("tcp.rbac.allowed")->value());
  EXPECT_EQ(1U, test_server_->counter("tcp.rbac.denied")->value());
}

TEST_P(RoleBasedAccessControlNetworkFilterIntegrationTest, DeniedWithDenyAction) {
  useListenerAccessLog("%CONNECTION_TERMINATION_DETAILS%");
  initializeFilter(R"EOF(
name: rbac
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.network.rbac.v3.RBAC
  stat_prefix: tcp.
  rules:
    action: DENY
    policies:
      "deny all":
        permissions:
          - any: true
        principals:
          - any: true
)EOF");
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  ASSERT_TRUE(tcp_client->write("hello", false, false));
  tcp_client->waitForDisconnect();

  EXPECT_EQ(0U, test_server_->counter("tcp.rbac.allowed")->value());
  EXPECT_EQ(1U, test_server_->counter("tcp.rbac.denied")->value());
  // Note the whitespace in the policy id is replaced by '_'.
  EXPECT_THAT(waitForAccessLog(listener_access_log_name_),
              testing::HasSubstr("rbac_access_denied_matched_policy[deny_all]"));
}

TEST_P(RoleBasedAccessControlNetworkFilterIntegrationTest, MatcherAllowed) {
  initializeFilter(R"EOF(
name: rbac
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.network.rbac.v3.RBAC
  stat_prefix: tcp.
  matcher:
    on_no_match:
      action:
        name: action
        typed_config:
          "@type": type.googleapis.com/envoy.config.rbac.v3.Action
          name: allow_all
          action: ALLOW
  shadow_matcher:
    on_no_match:
      action:
        name: action
        typed_config:
          "@type": type.googleapis.com/envoy.config.rbac.v3.Action
          name: deny_all
          action: DENY
)EOF");
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  ASSERT_TRUE(tcp_client->write("hello"));
  ASSERT_TRUE(tcp_client->connected());
  tcp_client->close();

  test_server_->waitForCounterGe("tcp.rbac.allowed", 1);
  EXPECT_EQ(0U, test_server_->counter("tcp.rbac.denied")->value());
  EXPECT_EQ(0U, test_server_->counter("tcp.rbac.shadow_allowed")->value());
  test_server_->waitForCounterGe("tcp.rbac.shadow_denied", 1);
}

TEST_P(RoleBasedAccessControlNetworkFilterIntegrationTest, MatcherDenied) {
  initializeFilter(R"EOF(
name: rbac
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.network.rbac.v3.RBAC
  stat_prefix: tcp.
  matcher:
    on_no_match:
      action:
        name: action
        typed_config:
          "@type": type.googleapis.com/envoy.config.rbac.v3.Action
          name: deny_all
          action: DENY
  shadow_matcher:
    on_no_match:
      action:
        name: action
        typed_config:
          "@type": type.googleapis.com/envoy.config.rbac.v3.Action
          name: allow_all
          action: ALLOW
)EOF");
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  ASSERT_TRUE(tcp_client->write("hello", false, false));
  tcp_client->waitForDisconnect();

  EXPECT_EQ(0U, test_server_->counter("tcp.rbac.allowed")->value());
  EXPECT_EQ(1U, test_server_->counter("tcp.rbac.denied")->value());
  EXPECT_EQ(1U, test_server_->counter("tcp.rbac.shadow_allowed")->value());
  EXPECT_EQ(0U, test_server_->counter("tcp.rbac.shadow_denied")->value());
}

TEST_P(RoleBasedAccessControlNetworkFilterIntegrationTest, MatcherDeniedWithDenyAction) {
  useListenerAccessLog("%CONNECTION_TERMINATION_DETAILS%");
  initializeFilter(R"EOF(
name: rbac
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.network.rbac.v3.RBAC
  stat_prefix: tcp.
  matcher:
    on_no_match:
      action:
        name: action
        typed_config:
          "@type": type.googleapis.com/envoy.config.rbac.v3.Action
          name: "deny all"
          action: DENY
)EOF");
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  ASSERT_TRUE(tcp_client->write("hello", false, false));
  tcp_client->waitForDisconnect();

  EXPECT_EQ(0U, test_server_->counter("tcp.rbac.allowed")->value());
  EXPECT_EQ(1U, test_server_->counter("tcp.rbac.denied")->value());
  // Note the whitespace in the policy id is replaced by '_'.
  EXPECT_THAT(waitForAccessLog(listener_access_log_name_),
              testing::HasSubstr("rbac_access_denied_matched_policy[deny_all]"));
}

TEST_P(RoleBasedAccessControlNetworkFilterIntegrationTest, FilterStateMatcherDenied) {
  auto config = R"EOF(
name: rbac
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.network.rbac.v3.RBAC
  stat_prefix: tcp.
  matcher:
    matcher_tree:
      input:
        name: filter_state
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.matching.common_inputs.network.v3.FilterStateInput
          key: test_key
      exact_match_map:
        map:
          "deny_value":
            action:
              name: envoy.filters.rbac.action
              typed_config:
                "@type": type.googleapis.com/envoy.config.rbac.v3.Action
                name: deny-request
                action: DENY
    on_no_match:
      action:
        name: action
        typed_config:
          "@type": type.googleapis.com/envoy.config.rbac.v3.Action
          name: allow-request
          action: ALLOW
)EOF";
  config_helper_.addConfigModifier([config](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    envoy::config::listener::v3::Filter filter;
    TestUtility::loadFromYaml(config, filter);
    ASSERT_GT(bootstrap.mutable_static_resources()->listeners_size(), 0);
    auto l = bootstrap.mutable_static_resources()->mutable_listeners(0);
    ASSERT_GT(l->filter_chains_size(), 0);
    ASSERT_GT(l->filter_chains(0).filters_size(), 0);

    // Save all original filters
    std::vector<envoy::config::listener::v3::Filter> original_filters;
    for (int i = 0; i < l->filter_chains(0).filters_size(); i++) {
      original_filters.push_back(*l->mutable_filter_chains(0)->mutable_filters(i));
    }

    // Clear the existing filters
    l->mutable_filter_chains(0)->clear_filters();

    // Add the Set State filter at position 0
    envoy::config::listener::v3::Filter set_state_filter;
    TestUtility::loadFromYaml(FILTER_STATE_SETTER_CONFIG, set_state_filter);
    l->mutable_filter_chains(0)->add_filters()->Swap(&set_state_filter);

    // Re-add all original filters after our new ones
    for (auto& original_filter : original_filters) {
      *l->mutable_filter_chains(0)->add_filters() = original_filter;
    }

    // Swap the RBAC filter config at position 1
    l->mutable_filter_chains(0)->mutable_filters(1)->Swap(&filter);
  });
  BaseIntegrationTest::initialize();
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  ASSERT_TRUE(tcp_client->write("hello", false, false));
  tcp_client->waitForDisconnect();

  EXPECT_EQ(0U, test_server_->counter("tcp.rbac.allowed")->value());
  EXPECT_EQ(1U, test_server_->counter("tcp.rbac.denied")->value());
}

} // namespace RBAC
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
