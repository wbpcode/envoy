#pragma once

#include "envoy/common/callback.h"
#include "envoy/upstream/health_checker.h"

#include "source/common/common/callback_impl.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Upstream {
class MockHealthChecker : public HealthChecker {
public:
  MockHealthChecker();
  ~MockHealthChecker() override;

  MOCK_METHOD(Common::CallbackHandlePtr, addHostCheckCompleteCb, (HostStatusCb callback));
  MOCK_METHOD(void, start, ());

  void runCallbacks(Upstream::HostSharedPtr host, HealthTransition changed_state,
                    HealthState current_check_result) {
    callbacks_.runCallbacks(host, changed_state, current_check_result);
  }

  Common::CallbackManager<void, const HostSharedPtr&, HealthTransition, HealthState> callbacks_;
};
} // namespace Upstream
} // namespace Envoy
