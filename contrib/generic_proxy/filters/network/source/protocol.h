#pragma once

#include "contrib/generic_proxy/filters/network/source/string_registry.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace GenericProxy {

/**
 * Class that used to represent a protocol that has a unique ID and a unique name.
 */
class Protocol {
public:
  const absl::string_view name;
  const uint32_t id;

private:
  static StringRegistry& stringRegistry();

  template <const char* NAME> static Protocol protocol() {
    static const Protocol* protocol = []() -> const Protocol* {
      const uint32_t id = stringRegistry().add<true>(NAME);
      return new Protocol(NAME, id);
    }();
    return *protocol;
  }

  Protocol(absl::string_view name, uint32_t id) : name(name), id(id) {}
};

} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
