#include "source/extensions/filters/http/credential_injector/config.h"
#include "source/extensions/filters/http/credential_injector/credential_injector_filter.h"

#include "test/extensions/filters/http/credential_injector/mock_credential.pb.h"
#include "test/mocks/server/factory_context.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CredentialInjector {
namespace {

using testing::NiceMock;

const ::test::mock_credential::Unregistered _mock_credential_dummy;

TEST(Factory, UnregisteredExtension) {
  const std::string yaml_string = R"EOF(
  overwrite: true
  allow_request_without_credential: true
  credential:
    name: undefined_credential
    typed_config:
      "@type": type.googleapis.com/test.mock_credential.Unregistered
  )EOF";

  envoy::extensions::filters::http::credential_injector::v3::CredentialInjector proto_config;
  TestUtility::loadFromYaml(yaml_string, proto_config);
  CredentialInjectorFilterFactory factory;
  NiceMock<Server::Configuration::MockFactoryContext> context;
  EXPECT_THAT(
      factory.createFilterFactoryFromProto(proto_config, "stats", context).status().message(),
      testing::HasSubstr("Didn't find a registered implementation for 'undefined_credential' with "
                         "type URL: 'test.mock_credential.Unregistered'"));
}

TEST(Factory, CredentialInjectorFilterWithServerContext) {
  const std::string yaml_string = R"EOF(
  overwrite: true
  allow_request_without_credential: true
  credential:
    name: undefined_credential
    typed_config:
      "@type": type.googleapis.com/test.mock_credential.Unregistered
  )EOF";

  envoy::extensions::filters::http::credential_injector::v3::CredentialInjector proto_config;
  TestUtility::loadFromYaml(yaml_string, proto_config);
  CredentialInjectorFilterFactory factory;
  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  // The method should throw since the credential is not registered
  EXPECT_THROW(factory.createFilterFactoryFromProtoWithServerContext(proto_config, "stats", context),
               EnvoyException);
}

} // namespace
} // namespace CredentialInjector
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
