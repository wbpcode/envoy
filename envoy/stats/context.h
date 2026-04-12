#pragma once

#include "envoy/common/pure.h"

namespace Envoy {
namespace Stats {

struct WellKnownTagStatNames;

class Context {
public:
  virtual ~Context() = default;

  /**
   * @return a struct containing StatNames for all well-known stats tags.
   */
  virtual const WellKnownTagStatNames& wellKnownTagStatNames() const PURE;
};

} // namespace Stats
} // namespace Envoy
