#include "contrib/generic_proxy/filters/network/source/protocol.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace GenericProxy {

StringRegistry& Protocol::stringRegistry() { MUTABLE_CONSTRUCT_ON_FIRST_USE(StringRegistry); }

} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
