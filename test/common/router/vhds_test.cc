#include <chrono>
#include <memory>
#include <string>

#include "envoy/config/route/v3/route.pb.h"
#include "envoy/config/route/v3/route_components.pb.h"
#include "envoy/service/discovery/v3/discovery.pb.h"
#include "envoy/stats/scope.h"

#include "source/common/config/utility.h"
#include "source/common/init/manager_impl.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/router/rds_impl.h"
#include "source/common/router/route_config_update_receiver_impl.h"
#include "source/common/router/route_provider_manager.h"

#ifdef ENVOY_ADMIN_FUNCTIONALITY
#include "source/server/admin/admin.h"
#endif
#include "test/mocks/config/mocks.h"
#include "test/mocks/init/mocks.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/test_common/printers.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Router {
namespace {

using ::Envoy::StatusHelpers::HasStatusMessage;
using ::Envoy::StatusHelpers::IsOk;
using ::Envoy::StatusHelpers::IsOkAndHolds;
using ::testing::_;
using ::testing::Not;

class VhdsTest : public testing::Test {
public:
  void SetUp() override {
    default_vhds_config_ = R"EOF(
name: my_route
vhds:
  config_source:
    api_config_source:
      api_type: DELTA_GRPC
      grpc_services:
        envoy_grpc:
          cluster_name: xds_cluster
)EOF";
  }

  envoy::config::route::v3::VirtualHost buildVirtualHost(const std::string& name,
                                                         const std::string& domain) {
    return TestUtility::parseYaml<envoy::config::route::v3::VirtualHost>(fmt::format(R"EOF(
      name: {}
      domains: [{}]
      routes:
      - match: {{ prefix: "/" }}
        route: {{ cluster: "my_service" }}
    )EOF",
                                                                                     name, domain));
  }

  Protobuf::RepeatedPtrField<envoy::service::discovery::v3::Resource>
  buildAddedResources(const std::vector<envoy::config::route::v3::VirtualHost>& added_or_updated) {
    Protobuf::RepeatedPtrField<envoy::service::discovery::v3::Resource> to_ret;

    for (const auto& vhost : added_or_updated) {
      auto* resource = to_ret.Add();
      resource->set_name(vhost.name());
      resource->set_version("1");
      std::ignore = resource->mutable_resource()->PackFrom(vhost);
    }

    return to_ret;
  }

  Protobuf::RepeatedPtrField<std::string>
  buildRemovedResources(const std::vector<std::string>& removed) {
    return Protobuf::RepeatedPtrField<std::string>{removed.begin(), removed.end()};
  }
  // Applies a route configuration to a fresh receiver. The receiver creates and owns the VHDS
  // subscription of the route configuration itself, so this is also what instantiates VHDS, and a
  // VHDS configuration that doesn't validate surfaces as a failed status here.
  absl::StatusOr<RouteConfigUpdatePtr>
  makeRouteConfigUpdate(const envoy::config::route::v3::RouteConfiguration& rc) {
    RouteConfigUpdatePtr config_update_info =
        std::make_unique<RouteConfigUpdateReceiverImpl>(proto_traits_, factory_context_, context_);
    config_update_info->routeConfigProvider() = provider_;
    RETURN_IF_NOT_OK(config_update_info->onRdsUpdate(rc, init_manager_, "1").status());
    return config_update_info;
  }

  // Delivers a VHDS response to the subscription that the receiver created.
  absl::Status deliverVhdsUpdate(const std::vector<envoy::config::route::v3::VirtualHost>& added,
                                 const std::vector<std::string>& removed = {}) {
    const auto added_resources = buildAddedResources(added);
    const auto decoded_resources =
        TestUtility::decodeResources<envoy::config::route::v3::VirtualHost>(added_resources);
    return factory_context_.cluster_manager_.subscription_factory_.callbacks_->onConfigUpdate(
        decoded_resources.refvec_, buildRemovedResources(removed), "1");
  }

  ProtoTraitsImpl proto_traits_;
  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context_;
  Init::ManagerImpl init_manager_{"test route config"};
  Init::ExpectableWatcherImpl init_watcher_;
  Init::TargetHandlePtr init_target_handle_;
  const std::string context_ = "vhds_test";
  Envoy::Rds::RouteConfigProvider* provider_ = nullptr;
  Protobuf::util::MessageDifferencer messageDifferencer_;
  std::string default_vhds_config_;
  NiceMock<Envoy::Config::MockSubscriptionFactory> subscription_factory_;
};

// verify that api_type: DELTA_GRPC passes validation
TEST_F(VhdsTest, VhdsInstantiationShouldSucceedWithDELTA_GRPC) {
  const auto route_config =
      TestUtility::parseYaml<envoy::config::route::v3::RouteConfiguration>(default_vhds_config_);
  EXPECT_OK(makeRouteConfigUpdate(route_config).status());
}

// verify that api_type: GRPC fails validation
TEST_F(VhdsTest, VhdsInstantiationShouldFailWithoutDELTA_GRPC) {
  const auto route_config =
      TestUtility::parseYaml<envoy::config::route::v3::RouteConfiguration>(R"EOF(
name: my_route
vhds:
  config_source:
    api_config_source:
      api_type: GRPC
      grpc_services:
        envoy_grpc:
          cluster_name: xds_cluster
  )EOF");
  EXPECT_THAT(makeRouteConfigUpdate(route_config).status(), Not(IsOk()));
}

// Verify that VHDS over GRPC fails when ADS is using DELTA_GRPC.
TEST_F(VhdsTest, VhdsInstantiationShouldFailWithGrpcAndAdsDeltaGrpc) {
  factory_context_.bootstrap().mutable_dynamic_resources()->mutable_ads_config()->set_api_type(
      envoy::config::core::v3::ApiConfigSource::DELTA_GRPC);
  const auto route_config =
      TestUtility::parseYaml<envoy::config::route::v3::RouteConfiguration>(R"EOF(
name: my_route
vhds:
  config_source:
    api_config_source:
      api_type: GRPC
      grpc_services:
        envoy_grpc:
          cluster_name: xds_cluster
  )EOF");
  EXPECT_THAT(makeRouteConfigUpdate(route_config).status(), Not(IsOk()));
}

// verify that ADS with DELTA_GRPC in bootstrap passes validation
TEST_F(VhdsTest, VhdsInstantiationShouldSucceedWithAdsAndDeltaGrpc) {
  // Configure bootstrap with ADS using DELTA_GRPC
  auto& bootstrap = factory_context_.bootstrap();
  auto* dynamic_resources = bootstrap.mutable_dynamic_resources();
  auto* ads_config = dynamic_resources->mutable_ads_config();
  ads_config->set_api_type(envoy::config::core::v3::ApiConfigSource::DELTA_GRPC);

  const auto route_config =
      TestUtility::parseYaml<envoy::config::route::v3::RouteConfiguration>(R"EOF(
name: my_route
vhds:
  config_source:
    ads: {}
  )EOF");
  EXPECT_OK(makeRouteConfigUpdate(route_config).status());
}

// verify that ADS without ADS configured in bootstrap fails validation
TEST_F(VhdsTest, VhdsInstantiationShouldFailWithAdsButNoBootstrapConfig) {
  // Don't configure ADS in bootstrap (it's empty by default)

  const auto route_config =
      TestUtility::parseYaml<envoy::config::route::v3::RouteConfiguration>(R"EOF(
name: my_route
vhds:
  config_source:
    ads: {}
  )EOF");
  EXPECT_THAT(makeRouteConfigUpdate(route_config),
              HasStatusMessage(
                  "vhds: ADS config source specified but no ADS configured in bootstrap."));
}

// verify that ADS without DELTA_GRPC api_type in bootstrap fails validation
TEST_F(VhdsTest, VhdsInstantiationShouldFailWithAdsButWrongApiType) {
  // Configure bootstrap with ADS using GRPC (not DELTA_GRPC)
  auto& bootstrap = factory_context_.bootstrap();
  auto* dynamic_resources = bootstrap.mutable_dynamic_resources();
  auto* ads_config = dynamic_resources->mutable_ads_config();
  ads_config->set_api_type(envoy::config::core::v3::ApiConfigSource::GRPC);

  const auto route_config =
      TestUtility::parseYaml<envoy::config::route::v3::RouteConfiguration>(R"EOF(
name: my_route
vhds:
  config_source:
    ads: {}
  )EOF");
  EXPECT_THAT(
      makeRouteConfigUpdate(route_config),
      HasStatusMessage("vhds: ADS must use DELTA_GRPC api_type when used as VHDS config source."));
}

// verify addition/updating of virtual hosts
TEST_F(VhdsTest, VhdsAddsVirtualHosts) {
  const auto route_config =
      TestUtility::parseYaml<envoy::config::route::v3::RouteConfiguration>(default_vhds_config_);
  RouteConfigUpdatePtr config_update_info = makeRouteConfigUpdate(route_config).value();
  EXPECT_EQ(0UL, config_update_info->protobufConfigurationCast().virtual_hosts_size());

  auto vhost = buildVirtualHost("vhost1", "vhost.first");
  const auto& added_resources = buildAddedResources({vhost});
  const auto decoded_resources =
      TestUtility::decodeResources<envoy::config::route::v3::VirtualHost>(added_resources);
  const Protobuf::RepeatedPtrField<std::string> removed_resources;
  EXPECT_OK(factory_context_.cluster_manager_.subscription_factory_.callbacks_->onConfigUpdate(
      decoded_resources.refvec_, removed_resources, "1"));

  EXPECT_EQ(1UL, config_update_info->protobufConfigurationCast().virtual_hosts_size());
  EXPECT_TRUE(messageDifferencer_.Equals(
      vhost, config_update_info->protobufConfigurationCast().virtual_hosts(0)));
}

// verify that an RDS update of virtual hosts leaves VHDS virtual hosts intact
TEST_F(VhdsTest, RdsUpdatesVirtualHosts) {
  const auto route_config =
      TestUtility::parseYaml<envoy::config::route::v3::RouteConfiguration>(R"EOF(
name: my_route
virtual_hosts:
- name: vhost_rds1
  domains: ["vhost.rds.first"]
  routes:
  - match: { prefix: "/rdsone" }
    route: { cluster: my_service }
vhds:
  config_source:
    api_config_source:
      api_type: DELTA_GRPC
      grpc_services:
        envoy_grpc:
          cluster_name: xds_cluster
  )EOF");
  const auto updated_route_config =
      TestUtility::parseYaml<envoy::config::route::v3::RouteConfiguration>(R"EOF(
name: my_route
virtual_hosts:
- name: vhost_rds1
  domains: ["vhost.rds.first"]
  routes:
  - match: { prefix: "/rdsone" }
    route: { cluster: my_service }
- name: vhost_rds2
  domains: ["vhost.rds.second"]
  routes:
  - match: { prefix: "/rdstwo" }
    route: { cluster: my_other_service }
vhds:
  config_source:
    api_config_source:
      api_type: DELTA_GRPC
      grpc_services:
        envoy_grpc:
          cluster_name: xds_cluster
  )EOF");
  RouteConfigUpdatePtr config_update_info = makeRouteConfigUpdate(route_config).value();
  EXPECT_EQ(1UL, config_update_info->protobufConfigurationCast().virtual_hosts_size());
  EXPECT_EQ("vhost_rds1", config_update_info->protobufConfigurationCast().virtual_hosts(0).name());

  auto vhost = buildVirtualHost("vhost_vhds1", "vhost.first");
  const auto& added_resources = buildAddedResources({vhost});
  const auto decoded_resources =
      TestUtility::decodeResources<envoy::config::route::v3::VirtualHost>(added_resources);
  const Protobuf::RepeatedPtrField<std::string> removed_resources;
  EXPECT_OK(factory_context_.cluster_manager_.subscription_factory_.callbacks_->onConfigUpdate(
      decoded_resources.refvec_, removed_resources, "1"));
  EXPECT_EQ(2UL, config_update_info->protobufConfigurationCast().virtual_hosts_size());

  EXPECT_THAT(config_update_info->onRdsUpdate(updated_route_config, init_manager_, "2"),
              IsOkAndHolds(true));

  EXPECT_EQ(3UL, config_update_info->protobufConfigurationCast().virtual_hosts_size());
  auto actual_vhost_0 = config_update_info->protobufConfigurationCast().virtual_hosts(0);
  auto actual_vhost_1 = config_update_info->protobufConfigurationCast().virtual_hosts(1);
  auto actual_vhost_2 = config_update_info->protobufConfigurationCast().virtual_hosts(2);
  EXPECT_TRUE("vhost_rds1" == actual_vhost_0.name() || "vhost_rds1" == actual_vhost_1.name() ||
              "vhost_rds1" == actual_vhost_2.name());
  EXPECT_TRUE("vhost_rds2" == actual_vhost_0.name() || "vhost_rds2" == actual_vhost_1.name() ||
              "vhost_rds2" == actual_vhost_2.name());
  EXPECT_TRUE("vhost_vhds1" == actual_vhost_0.name() || "vhost_vhds1" == actual_vhost_1.name() ||
              "vhost_vhds1" == actual_vhost_2.name());
}

// Verify that a route configuration that uses VHDS isn't warmed up until the first batch of virtual
// hosts has been fetched, i.e. that the VHDS subscription registers with the init manager of the
// update that created it.
TEST_F(VhdsTest, RouteConfigurationWarmsUpWithTheInitialVhdsFetch) {
  const auto route_config =
      TestUtility::parseYaml<envoy::config::route::v3::RouteConfiguration>(default_vhds_config_);
  RouteConfigUpdatePtr config_update_info = makeRouteConfigUpdate(route_config).value();

  // Starting the init manager of the update starts the VHDS subscription, but the update stays
  // warming: nothing has been fetched yet.
  init_watcher_.expectReady().Times(0);
  init_manager_.initialize(init_watcher_);
  EXPECT_EQ(Init::Manager::State::Initializing, init_manager_.state());
  EXPECT_EQ(0UL, config_update_info->protobufConfigurationCast().virtual_hosts_size());
  ::testing::Mock::VerifyAndClearExpectations(&init_watcher_);

  // The first VHDS response completes the warming up of the update.
  init_watcher_.expectReady();
  EXPECT_OK(deliverVhdsUpdate({buildVirtualHost("vhost1", "vhost.first")}));
  EXPECT_EQ(Init::Manager::State::Initialized, init_manager_.state());
  EXPECT_EQ(1UL, config_update_info->protobufConfigurationCast().virtual_hosts_size());
}

// Verify that a VHDS subscription created by an update that is superseded before it finished warming
// up is not lost. The VHDS configuration of the superseding update is unchanged, so it doesn't
// create a subscription of its own and has to keep the one that is already running.
TEST_F(VhdsTest, VhdsSubscriptionSurvivesASupersededRdsUpdate) {
  const auto route_config =
      TestUtility::parseYaml<envoy::config::route::v3::RouteConfiguration>(default_vhds_config_);
  auto updated_route_config = route_config;
  *updated_route_config.mutable_virtual_hosts()->Add() =
      buildVirtualHost("vhost_rds1", "vhost.rds.first");

  RouteConfigUpdatePtr config_update_info = makeRouteConfigUpdate(route_config).value();
  init_manager_.initialize(init_watcher_);

  // A second RDS update arrives while the first one is still warming up. Its VHDS configuration is
  // identical, so no second subscription is created.
  EXPECT_CALL(factory_context_.cluster_manager_.subscription_factory_,
              subscriptionFromConfigSource(_, _, _, _, _, _))
      .Times(0);
  EXPECT_THAT(config_update_info->onRdsUpdate(updated_route_config, init_manager_, "2"),
              IsOkAndHolds(true));

  // The subscription of the first update is still live, so its virtual hosts still land in the
  // newest route configuration, and they finish warming it up.
  init_watcher_.expectReady();
  EXPECT_OK(deliverVhdsUpdate({buildVirtualHost("vhost_vhds1", "vhost.first")}));
  EXPECT_EQ(Init::Manager::State::Initialized, init_manager_.state());
  EXPECT_EQ(2UL, config_update_info->protobufConfigurationCast().virtual_hosts_size());
}

// Verify that the VHDS subscription is only recreated when the VHDS configuration itself changes,
// and that it is dropped and recreated across removal and re-addition of VHDS.
TEST_F(VhdsTest, VhdsSubscriptionFollowsTheVhdsConfiguration) {
  const auto route_config =
      TestUtility::parseYaml<envoy::config::route::v3::RouteConfiguration>(default_vhds_config_);
  auto same_vhds_config = route_config;
  *same_vhds_config.mutable_virtual_hosts()->Add() =
      buildVirtualHost("vhost_rds1", "vhost.rds.first");
  auto no_vhds_config = same_vhds_config;
  no_vhds_config.clear_vhds();

  // The first update creates the subscription.
  RouteConfigUpdatePtr config_update_info = makeRouteConfigUpdate(route_config).value();

  // An unchanged VHDS configuration keeps it.
  EXPECT_CALL(factory_context_.cluster_manager_.subscription_factory_,
              subscriptionFromConfigSource(_, _, _, _, _, _))
      .Times(0);
  EXPECT_THAT(config_update_info->onRdsUpdate(same_vhds_config, init_manager_, "2"),
              IsOkAndHolds(true));
  ::testing::Mock::VerifyAndClearExpectations(
      &factory_context_.cluster_manager_.subscription_factory_);

  // Removing VHDS drops it, so re-adding the very same VHDS configuration creates a new one.
  EXPECT_THAT(config_update_info->onRdsUpdate(no_vhds_config, init_manager_, "3"),
              IsOkAndHolds(true));
  EXPECT_CALL(factory_context_.cluster_manager_.subscription_factory_,
              subscriptionFromConfigSource(_, _, _, _, _, _))
      .Times(1);
  EXPECT_THAT(config_update_info->onRdsUpdate(same_vhds_config, init_manager_, "4"),
              IsOkAndHolds(true));
}

// Verify that an on-demand update request against a route configuration that doesn't use VHDS is a
// no-op rather than a crash.
TEST_F(VhdsTest, OnDemandUpdateWithoutVhdsIsANoop) {
  const auto route_config =
      TestUtility::parseYaml<envoy::config::route::v3::RouteConfiguration>(R"EOF(
name: my_route
virtual_hosts:
- name: vhost_rds1
  domains: ["vhost.rds.first"]
  routes:
  - match: { prefix: "/rdsone" }
    route: { cluster: my_service }
  )EOF");
  RouteConfigUpdatePtr config_update_info = makeRouteConfigUpdate(route_config).value();

  config_update_info->updateOnDemand("my_route/vhost.rds.first");
}

} // namespace
} // namespace Router
} // namespace Envoy
