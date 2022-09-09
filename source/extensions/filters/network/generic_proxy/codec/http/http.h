#pragma once

#include "source/extensions/filters/network/generic_proxy/interface/codec.h"
#include "source/common/buffer/buffer_impl.h"
#include "source/common/http/http1/parser.h"
#include "source/common/http/http1/legacy_parser_impl.h"
#include "source/common/http/header_map_impl.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace GenericProxy {
namespace HttpCodec {

class HttpRequest : Request {
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
  Status status() const override {
    return headers_->getStatusValue() == "200" ? absl::OkStatus()
                                               : Status(StatusCode::kAborted, "error");
  }

  Http::ResponseHeaderMapPtr headers_;
  Buffer::OwnedImpl buffer_;
};

class HttpRequestDecoder : public RequestDecoder, public Envoy::Http::Http1::ParserCallbacks {
public:
  void setDecoderCallback(RequestDecoderCallback& callback) override { callback_ = &callback; }
  void decode(Buffer::Instance&) override {}

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

  Envoy::Http::Http1::CallbackResult onStatus(const char*, size_t) override {}

  virtual CallbackResult onHeaderField(const char* data, size_t length) PURE;

  virtual CallbackResult onHeaderValue(const char* data, size_t length) PURE;

  Envoy::Http::Http1::CallbackResult onHeadersComplete() override {
    return Envoy::Http::Http1::CallbackResult::Success;
  }

  /**
   * Called when body data is received.
   * @param data supplies the start address.
   * @param length supplies the length
   */
  virtual void bufferBody(const char* data, size_t length) PURE;

  /**
   * Called when the HTTP message has completed parsing.
   * @return CallbackResult representing success or failure.
   */
  virtual CallbackResult onMessageComplete() PURE;

  /**
   * Called when accepting a chunk header.
   */
  virtual void onChunkHeader(bool) PURE;

  std::unique_ptr<HttpRequest> request_;
  RequestDecoderCallback* callback_{};
};

/**
 * Decoder of response.
 */
class ResponseDecoder {
public:
  virtual ~ResponseDecoder() = default;

  // The decode() method may be called multiple times for single request or response.
  // So an independent setDecoderCallback() is used to set decoding callback.
  virtual void setDecoderCallback(ResponseDecoderCallback& callback) PURE;
  virtual void decode(Buffer::Instance& buffer) PURE;
};

/*
 * Encoder of request.
 */
class RequestEncoder {
public:
  virtual ~RequestEncoder() = default;

  virtual void encode(const Request&, RequestEncoderCallback& callback) PURE;
};

/*
 * Encoder of response.
 */
class ResponseEncoder {
public:
  virtual ~ResponseEncoder() = default;

  virtual void encode(const Response&, ResponseEncoderCallback& callback) PURE;
};

class MessageCreator {
public:
  virtual ~MessageCreator() = default;

  /**
   * Create local response message for local reply.
   */
  virtual ResponsePtr response(Status status, const Request& origin_request) PURE;
};

} // namespace HttpCodec
} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
