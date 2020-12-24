#include "extensions/filters/http/complex_example/example.h"

#include "common/common/base64.h"
#include "common/common/regex.h"
#include "common/config/well_known_names.h"
#include "common/http/header_utility.h"
#include "common/http/utility.h"
#include "common/protobuf/protobuf.h"

#include "extensions/filters/http/well_known_names.h"

#include "absl/strings/numbers.h"
#include "absl/strings/string_view.h"

#include <random>

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ComplexExample {

Http::FilterHeadersStatus Filter::decodeHeaders(Http::RequestHeaderMap& headers, bool end_stream) {
  ENVOY_LOG(info, "{}", std::string("onRequestHeaders ") + std::to_string(id()));

  if (end_stream) {
    auto update_header = [](Http::ResponseHeaderMap& headers) {
      headers.addCopy(Http::LowerCaseString("why"), "Body cannot be empty!");
    };
    decoder_callbacks_->sendLocalReply(
        Http::Code::BadRequest,
        "{\"name\": \"WASM Example\", \"info\": \"Make Cpp great Again!!!\"}", update_header,
        absl::nullopt, "random_local_response");
    return Http::FilterHeadersStatus::StopIteration;
  }

  headers.removeContentLength();

  headers.addCopy(Http::LowerCaseString("complex"), "complex");

  std::string fake_header_key_prefix = "fake_key_";
  std::string fake_header_value_prefix = "fake_value_";

  std::default_random_engine random_engine;
  std::uniform_int_distribution<int> range(0, 20);

  // Add 20 headers to request headers
  for (size_t i = 0; i < 20; i++) {
    std::string key = fake_header_key_prefix + std::to_string(i);
    std::string value = fake_header_value_prefix + std::to_string(i);
    headers.addCopy(Http::LowerCaseString(key), value);
  }

  // Check 10 times random headers.
  for (size_t i = 0; i < 10; i++) {
    int random = range(random_engine);
    const auto& value =
        headers.get(Http::LowerCaseString(fake_header_key_prefix + std::to_string(random)));

    if (std::string(value[0]->value().getStringView()) == fake_header_value_prefix + "5" ||
        std::string(value[0]->value().getStringView()) == fake_header_value_prefix + "10") {
      ENVOY_LOG(info, "{}", std::string(value[0]->value().getStringView()));
    }
  }

  for (size_t i = 0; i < 20; i++) {
    std::string key = fake_header_key_prefix + std::to_string(i);
    headers.remove(Http::LowerCaseString(key));
  }

  auto path = headers.Path();
  ENVOY_LOG(info, "{}", std::string("header path ") + std::string(path->value().getStringView()));

  return Http::FilterHeadersStatus::Continue;
}

void Filter::setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) {
  decoder_callbacks_ = &callbacks;
}

Http::FilterHeadersStatus Filter::encodeHeaders(Http::ResponseHeaderMap&, bool) {
  return Http::FilterHeadersStatus::Continue;
}

void Filter::setEncoderFilterCallbacks(Http::StreamEncoderFilterCallbacks& callbacks) {
  encoder_callbacks_ = &callbacks;
}

} // namespace ComplexExample
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
