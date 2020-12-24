#include "extensions/filters/http/complex_example/config.h"

#include <string>

#include "envoy/registry/registry.h"

#include "common/protobuf/utility.h"

#include "extensions/filters/http/complex_example/example.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ComplexExample {

Http::FilterFactoryCb Config::createFilterFactoryFromProto(const Protobuf::Message&,
                                                           const std::string&,
                                                           Server::Configuration::FactoryContext&) {
  return [](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(Http::StreamFilterSharedPtr{new Filter()});
  };
}

/**
 * Static registration for the header-to-metadata filter. @see RegisterFactory.
 */
REGISTER_FACTORY(Config, Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace ComplexExample
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
