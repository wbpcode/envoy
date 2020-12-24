#pragma once

#include <string>
#include <tuple>
#include <vector>

#include "envoy/server/filter_config.h"

#include "common/common/logger.h"
#include "common/common/matchers.h"

#include "absl/strings/string_view.h"

#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/stream.h"
#include "common/buffer/buffer_impl.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ComplexExample {

class Filter : public Http::StreamFilter, public Logger::Loggable<Logger::Id::filter> {
public:
  Filter() {}
  ~Filter() {}

  // Http::StreamFilterBase
  void onDestroy() override {}

  // StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers, bool) override;
  Http::FilterDataStatus decodeData(Buffer::Instance& data, bool end_stream) override {

    if (!end_stream) {
      buffer.move(data);
      return Http::FilterDataStatus::StopIterationNoBuffer;
    }

    rapidjson::Document doc;

    auto body_string = buffer.toString();
    doc.Parse(body_string.c_str(), body_string.size());

    if (doc.HasParseError()) {
      auto update_header = [](Http::ResponseHeaderMap& headers) {
        headers.addCopy(Http::LowerCaseString("why"), "Not Json Body");
      };

      decoder_callbacks_->sendLocalReply(
          Http::Code::BadRequest,
          "{\"name\": \"WASM Example\", \"info\": \"Make Cpp great Again!!!\"}", update_header,
          absl::nullopt, "random_local_response");
      return Http::FilterDataStatus::StopIterationNoBuffer;
    }

    rapidjson::Value key1, key2, key3, value1, value2, value3;
    key1.SetString("fake_key1");
    key2.SetString("fake_key2");
    key3.SetString("fake_key3");

    value1.SetString("fake_value_xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx");
    value2.SetString("fake_value_xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx");
    value3.SetString("fake_value_xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx");

    doc.AddMember(key1, value1, doc.GetAllocator());
    doc.AddMember(key2, value2, doc.GetAllocator());
    doc.AddMember(key3, value3, doc.GetAllocator());

    rapidjson::StringBuffer string_buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(string_buffer);

    doc.Accept(writer);

    std::string new_body(string_buffer.GetString(), string_buffer.GetSize());

    ENVOY_LOG(info, "{}", new_body);

    ENVOY_LOG(info, "{}", std::to_string(new_body.size()));

    data.add(new_body);

    return Http::FilterDataStatus::Continue;
  }
  Http::FilterTrailersStatus decodeTrailers(Http::RequestTrailerMap&) override {
    return Http::FilterTrailersStatus::Continue;
  }
  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) override;

  // StreamEncoderFilter
  Http::FilterHeadersStatus encode100ContinueHeaders(Http::ResponseHeaderMap&) override {
    return Http::FilterHeadersStatus::Continue;
  }
  Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap& headers, bool) override;
  Http::FilterDataStatus encodeData(Buffer::Instance&, bool) override {
    return Http::FilterDataStatus::Continue;
  }
  Http::FilterTrailersStatus encodeTrailers(Http::ResponseTrailerMap&) override {
    return Http::FilterTrailersStatus::Continue;
  }
  Http::FilterMetadataStatus encodeMetadata(Http::MetadataMap&) override {
    return Http::FilterMetadataStatus::Continue;
  }
  void setEncoderFilterCallbacks(Http::StreamEncoderFilterCallbacks& callbacks) override;

  int id() { return 123456; }

private:
  Buffer::OwnedImpl buffer;

  Http::StreamDecoderFilterCallbacks* decoder_callbacks_{};
  Http::StreamEncoderFilterCallbacks* encoder_callbacks_{};
};

} // namespace ComplexExample
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
