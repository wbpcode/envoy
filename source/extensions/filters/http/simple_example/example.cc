#include "extensions/filters/http/simple_example/example.h"

#include "common/common/base64.h"
#include "common/common/regex.h"
#include "common/config/well_known_names.h"
#include "common/http/header_utility.h"
#include "common/http/utility.h"
#include "common/protobuf/protobuf.h"

#include "extensions/filters/http/well_known_names.h"

#include "absl/strings/numbers.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace SimpleExample {

Http::FilterHeadersStatus Filter::decodeHeaders(Http::RequestHeaderMap& headers, bool) {
  ENVOY_LOG(info, "onRequestHeaders {}", id());

  std::string fake_key = "fake_key";
  std::string fake_value = "fake_value";

  headers.addCopy(Http::LowerCaseString(fake_key), fake_value);

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

} // namespace SimpleExample
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
