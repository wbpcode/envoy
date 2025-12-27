// Copyright Istio Authors. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "contrib/peer_metadata/filters/http/source/config.h"

#include "test/mocks/server/factory_context.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace PeerMetadata {

TEST(PeerMetadataConfigTest, PeerMetadataFilter) {
  NiceMock<Server::Configuration::MockFactoryContext> context;
  FilterConfigFactory factory;
  const std::string yaml_string = R"EOF(
    downstream_discovery:
      - istio_headers: {}
  )EOF";

  envoy::extensions::filters::http::peer_metadata::v3alpha::PeerMetadataConfig proto_config;
  TestUtility::loadFromYaml(yaml_string, proto_config);

  Http::FilterFactoryCb cb = factory.createFilterFactoryFromProtoTyped(proto_config, "", context);
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamFilter(_));
  cb(filter_callback);
}

} // namespace PeerMetadata
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
