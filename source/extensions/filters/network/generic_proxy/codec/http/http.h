#pragma once

#include "source/extensions/filters/network/generic_proxy/interface/codec.h"
#include "source/common/buffer/buffer_impl.h"
#include "source/common/http/http1/parser.h"
#include "source/common/http/http1/legacy_parser_impl.h"
#include "source/common/http/utility.h"
#include "source/common/http/codes.h"
#include "source/common/http/header_map_impl.h"
#include <memory>

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace GenericProxy {
namespace HttpCodec {

class HttpRequest : public Request {
public:
  absl::string_view protocol() const override { return "envoy.codec.generic.http"; }

  absl::string_view host() const override { return headers_->getHostValue(); }
  absl::string_view path() const override { return headers_->getPathValue(); }

  absl::string_view method() const override { return headers_->getMethodValue(); }

  void forEach(Tracing::TraceContext::IterateCallback callback) const override {
    headers_->iterate([cb = std::move(callback)](const Envoy::Http::HeaderEntry& entry) {
      if (cb(entry.key().getStringView(), entry.value().getStringView())) {
        return Envoy::Http::HeaderMap::Iterate::Continue;
      }
      return Envoy::Http::HeaderMap::Iterate::Break;
    });
  }

  absl::optional<absl::string_view> getByKey(absl::string_view key) const override {
    return headers_->getByKey(key);
  }

  void setByKey(absl::string_view key, absl::string_view val) override {
    headers_->setByKey(key, val);
  }

  void setByReferenceKey(absl::string_view key, absl::string_view val) override {
    headers_->setByReferenceKey(key, val);
  }

  void setByReference(absl::string_view key, absl::string_view val) override {
    headers_->setByReference(key, val);
  }

  Http::RequestHeaderMapPtr headers_;
  Buffer::OwnedImpl buffer_;
};

class HttpResponse : public Response {
public:
  absl::string_view protocol() const override { return "envoy.codec.generic.http"; }

  Status status() const override {
    return headers_->getStatusValue() == "200" ? absl::OkStatus()
                                               : Status(StatusCode::kAborted, "error");
  }

  void forEach(Tracing::TraceContext::IterateCallback callback) const override {
    headers_->iterate([cb = std::move(callback)](const Envoy::Http::HeaderEntry& entry) {
      if (cb(entry.key().getStringView(), entry.value().getStringView())) {
        return Envoy::Http::HeaderMap::Iterate::Continue;
      }
      return Envoy::Http::HeaderMap::Iterate::Break;
    });
  }

  absl::optional<absl::string_view> getByKey(absl::string_view key) const override {
    auto result = headers_->get(Http::LowerCaseString(key));
    if (result.empty()) {
      return absl::nullopt;
    }
    return result[0]->value().getStringView();
  }

  void setByKey(absl::string_view key, absl::string_view val) override {
    headers_->setCopy(Http::LowerCaseString(key), val);
  }

  void setByReferenceKey(absl::string_view key, absl::string_view val) override {
    setByKey(key, val);
  }

  void setByReference(absl::string_view key, absl::string_view val) override { setByKey(key, val); }

  Http::ResponseHeaderMapPtr headers_;
  Buffer::OwnedImpl buffer_;
};

class HttpRequestDecoder : public RequestDecoder, public Envoy::Http::Http1::ParserCallbacks {
public:
  HttpRequestDecoder() {
    parser_ = std::make_unique<Envoy::Http::Http1::LegacyHttpParserImpl>(
        Envoy::Http::Http1::MessageType::Request, this);
  }

  void setDecoderCallback(RequestDecoderCallback& callback) override { callback_ = &callback; }

  void decode(Buffer::Instance& buffer) override {
    parser_->resume();

    buffer_.move(buffer);

    while (buffer_.length() > 0) {
      auto slice = buffer_.frontSlice();
      auto nread = parser_->execute(static_cast<const char*>(slice.mem_), slice.len_);
      buffer_.drain(nread);

      auto status = parser_->getStatus();

      if (status == Envoy::Http::Http1::ParserStatus::Ok) {
        continue;
      }
      if (status == Envoy::Http::Http1::ParserStatus::Paused) {
        break;
      }

      callback_->onDecodingFailure();
    }
  }

  Envoy::Http::Http1::CallbackResult onMessageBegin() override {
    ASSERT(request_ == nullptr);
    request_ = std::make_unique<HttpRequest>();
    request_->headers_ = Envoy::Http::RequestHeaderMapImpl::create();
    return Envoy::Http::Http1::CallbackResult::Success;
  }

  Envoy::Http::Http1::CallbackResult onUrl(const char* data, size_t length) override {
    request_->headers_->setPath(absl::string_view{data, length});
    return Envoy::Http::Http1::CallbackResult::Success;
  }

  Envoy::Http::Http1::CallbackResult onStatus(const char*, size_t) override {
    return Envoy::Http::Http1::CallbackResult::Success;
  }

  Envoy::Http::Http1::CallbackResult onHeaderField(const char* data, size_t length) override {
    header_field_.append(data, length);
    return Envoy::Http::Http1::CallbackResult::Success;
  }

  Envoy::Http::Http1::CallbackResult onHeaderValue(const char* data, size_t length) override {
    header_value_.append(data, length);

    request_->headers_->addCopy(Envoy::Http::LowerCaseString(header_field_), header_value_);

    header_field_.clear();
    header_value_.clear();

    return Envoy::Http::Http1::CallbackResult::Success;
  }

  Envoy::Http::Http1::CallbackResult onHeadersComplete() override {
    return Envoy::Http::Http1::CallbackResult::Success;
  }

  void bufferBody(const char* data, size_t length) override { request_->buffer_.add(data, length); }

  Envoy::Http::Http1::CallbackResult onMessageComplete() override {
    request_->headers_->setMethod(parser_->methodName());
    callback_->onDecodingSuccess(std::move(request_));
    return Envoy::Http::Http1::CallbackResult::Success;
  }

  void onChunkHeader(bool) override {}

  Buffer::OwnedImpl buffer_;

  std::unique_ptr<Envoy::Http::Http1::LegacyHttpParserImpl> parser_;

  std::string header_field_;
  std::string header_value_;

  std::unique_ptr<HttpRequest> request_;
  RequestDecoderCallback* callback_{};
};

/**
 * Decoder of response.
 */
class HttpResponseDecoder : public ResponseDecoder, public Envoy::Http::Http1::ParserCallbacks {
public:
  HttpResponseDecoder() {
    parser_ = std::make_unique<Envoy::Http::Http1::LegacyHttpParserImpl>(
        Envoy::Http::Http1::MessageType::Response, this);
  }

  void setDecoderCallback(ResponseDecoderCallback& callback) override { callback_ = &callback; }

  void decode(Buffer::Instance& buffer) override {
    parser_->resume();

    buffer_.move(buffer);

    while (buffer_.length() > 0) {
      auto slice = buffer_.frontSlice();
      auto nread = parser_->execute(static_cast<const char*>(slice.mem_), slice.len_);
      buffer_.drain(nread);

      auto status = parser_->getStatus();

      if (status == Envoy::Http::Http1::ParserStatus::Ok) {
        continue;
      }
      if (status == Envoy::Http::Http1::ParserStatus::Paused) {
        break;
      }

      callback_->onDecodingFailure();
    }
  }

  Envoy::Http::Http1::CallbackResult onMessageBegin() override {
    ASSERT(response_ == nullptr);
    response_ = std::make_unique<HttpResponse>();
    response_->headers_ = Envoy::Http::ResponseHeaderMapImpl::create();
    return Envoy::Http::Http1::CallbackResult::Success;
  }

  Envoy::Http::Http1::CallbackResult onUrl(const char*, size_t) override {
    return Envoy::Http::Http1::CallbackResult::Success;
  }

  Envoy::Http::Http1::CallbackResult onStatus(const char* data, size_t length) override {
    response_->headers_->setStatus(absl::string_view{data, length});
    return Envoy::Http::Http1::CallbackResult::Success;
  }

  Envoy::Http::Http1::CallbackResult onHeaderField(const char* data, size_t length) override {
    header_field_.append(data, length);
    return Envoy::Http::Http1::CallbackResult::Success;
  }

  Envoy::Http::Http1::CallbackResult onHeaderValue(const char* data, size_t length) override {
    header_value_.append(data, length);

    response_->headers_->addCopy(Envoy::Http::LowerCaseString(header_field_), header_value_);

    header_field_.clear();
    header_value_.clear();

    return Envoy::Http::Http1::CallbackResult::Success;
  }

  Envoy::Http::Http1::CallbackResult onHeadersComplete() override {
    return Envoy::Http::Http1::CallbackResult::Success;
  }

  void bufferBody(const char* data, size_t length) override {
    response_->buffer_.add(data, length);
  }

  Envoy::Http::Http1::CallbackResult onMessageComplete() override {
    callback_->onDecodingSuccess(std::move(response_));
    return Envoy::Http::Http1::CallbackResult::Success;
  }

  void onChunkHeader(bool) override {}

  Buffer::OwnedImpl buffer_;

  std::unique_ptr<Envoy::Http::Http1::LegacyHttpParserImpl> parser_;

  std::string header_field_;
  std::string header_value_;

  std::unique_ptr<HttpResponse> response_;
  ResponseDecoderCallback* callback_{};
};

/*
 * Encoder of request.
 */
class HttpRequestEncoder : public RequestEncoder {
public:
  void encode(const Request& request, RequestEncoderCallback& callback) override {
    const HttpRequest* http_request = dynamic_cast<const HttpRequest*>(&request);

    buffer_.addFragments({http_request->headers_->getMethodValue(), " ",
                          http_request->headers_->getPathValue(), " HTTP/1.1\r\n"});

    http_request->headers_->iterate(
        [this](const Envoy::Http::HeaderEntry& header) -> Envoy::Http::HeaderMap::Iterate {
          absl::string_view key_to_use = header.key().getStringView();
          uint32_t key_size_to_use = header.key().size();
          // Translate :authority -> host so that upper layers do not need to deal with this.
          if (key_size_to_use > 1 && key_to_use[0] == ':' && key_to_use[1] == 'a') {
            key_to_use = absl::string_view("host");
          }

          // Skip all headers starting with ':' that make it here.
          if (key_to_use[0] == ':') {
            return Envoy::Http::HeaderMap::Iterate::Continue;
          }

          buffer_.addFragments({key_to_use, ": ", header.value().getStringView(), "\r\n"});

          return Envoy::Http::HeaderMap::Iterate::Continue;
        });

    buffer_.addFragments({"\r\n"});

    if (http_request->buffer_.length() > 0) {
      buffer_.add(http_request->buffer_);
    }

    callback.onEncodingSuccess(buffer_, true);
  }

  Buffer::OwnedImpl buffer_;
};

/*
 * Encoder of response.
 */
class HttpResponseEncoder : public ResponseEncoder {
public:
  void encode(const Response& response, ResponseEncoderCallback& callback) override {
    const HttpResponse* http_response = dynamic_cast<const HttpResponse*>(&response);

    uint64_t numeric_status = Envoy::Http::Utility::getResponseStatus(*http_response->headers_);

    absl::string_view reason_phrase;
    const char* status_string =
        Envoy::Http::CodeUtility::toString(static_cast<Envoy::Http::Code>(numeric_status));
    uint32_t status_string_len = strlen(status_string);
    reason_phrase = {status_string, status_string_len};

    buffer_.addFragments({"HTTP/1.1 ", absl::StrCat(numeric_status), " ", reason_phrase, "\r\n"});

    http_response->headers_->iterate(
        [this](const Envoy::Http::HeaderEntry& header) -> Envoy::Http::HeaderMap::Iterate {
          absl::string_view key_to_use = header.key().getStringView();
          uint32_t key_size_to_use = header.key().size();
          // Translate :authority -> host so that upper layers do not need to deal with this.
          if (key_size_to_use > 1 && key_to_use[0] == ':' && key_to_use[1] == 'a') {
            key_to_use = absl::string_view("host");
          }

          // Skip all headers starting with ':' that make it here.
          if (key_to_use[0] == ':') {
            return Envoy::Http::HeaderMap::Iterate::Continue;
          }

          buffer_.addFragments({key_to_use, ": ", header.value().getStringView(), "\r\n"});

          return Envoy::Http::HeaderMap::Iterate::Continue;
        });

    buffer_.addFragments({"\r\n"});

    if (http_response->buffer_.length() > 0) {
      buffer_.add(http_response->buffer_);
    }

    callback.onEncodingSuccess(buffer_, true);
  }

  Buffer::OwnedImpl buffer_;
};

class HttpMessageCreator : public MessageCreator {
public:
  /**
   * Create local response message for local reply.
   */
  ResponsePtr response(Status status, const Request&) override {

    auto response = std::make_unique<HttpResponse>();
    response->headers_ = Envoy::Http::ResponseHeaderMapImpl::create();
    response->buffer_.add(status.message());
    response->headers_->setStatus("503");
    response->headers_->setContentLength(response->buffer_.length());
    return response;
  }
};

class HttpStreamCodecFactory : public CodecFactory {
public:
  RequestDecoderPtr requestDecoder() const override;
  ResponseDecoderPtr responseDecoder() const override;
  RequestEncoderPtr requestEncoder() const override;
  ResponseEncoderPtr responseEncoder() const override;
  MessageCreatorPtr messageCreator() const override;
};

class HttpStreamCodecFactoryConfig : public CodecFactoryConfig {
public:
  CodecFactoryPtr createFactory(const Protobuf::Message& config,
                                Envoy::Server::Configuration::FactoryContext& context) override;
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<ProtobufWkt::Struct>();
  }

  std::set<std::string> configTypes() override { return {"envoy.generic_proxy.codec.http.type"}; }

  std::string name() const override { return "envoy.generic_proxy.codec.http"; }
};

} // namespace HttpCodec
} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
