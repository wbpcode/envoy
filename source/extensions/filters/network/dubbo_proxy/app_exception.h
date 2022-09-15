#pragma once

#include "envoy/common/exception.h"

#include "source/common/common/utility.h"
#include "source/extensions/filters/network/dubbo_proxy/filters/filter.h"
#include "source/extensions/common/dubbo/metadata.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {

struct DownstreamConnectionCloseException : public EnvoyException {
  DownstreamConnectionCloseException(const std::string& what);
};

} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
