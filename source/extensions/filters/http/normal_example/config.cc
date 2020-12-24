#include "extensions/filters/http/normal_example/config.h"

#include <string>

#include "envoy/registry/registry.h"

#include "common/protobuf/utility.h"

#include "extensions/filters/http/normal_example/example.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace NormalExample {

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

} // namespace NormalExample
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
