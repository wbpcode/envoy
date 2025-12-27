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

#include "contrib/common/workload_discovery/source/api.h"

#include "envoy/singleton/manager.h"

namespace Envoy::Extensions::Common::WorkloadDiscovery {

// Stub implementation that returns null provider
// In a full implementation, this would be registered via a bootstrap extension
WorkloadMetadataProviderSharedPtr
GetProvider(Server::Configuration::ServerFactoryContext& context) {
  UNREFERENCED_PARAMETER(context);
  // Return null - workload discovery is optional
  return nullptr;
}

} // namespace Envoy::Extensions::Common::WorkloadDiscovery
