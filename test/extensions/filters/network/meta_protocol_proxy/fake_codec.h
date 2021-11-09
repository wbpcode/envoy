#pragma once

#include "source/common/buffer/buffer_impl.h"
#include "source/extensions/filters/network/meta_protocol_proxy/interface/codec.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MetaProtocolProxy {

template <typename InterfaceType> class FakeStreamBase : public InterfaceType {
public:
  void forEach(StreamBase::IterateCallback callback) const override {
    for (const auto& pair : data_) {
      callback(pair.first, pair.second);
    }
  }
  absl::optional<absl::string_view> getByKey(absl::string_view key) const override {
    auto iter = data_.find(key);
    if (iter == data_.end()) {
      return absl::nullopt;
    }
    return absl::make_optional<absl::string_view>(iter->second);
  }
  void setByKey(absl::string_view key, absl::string_view val) override {
    data_[key] = std::string(val);
  }
  void setByReferenceKey(absl::string_view key, absl::string_view val) override {
    setByKey(key, val);
  }
  void setByReference(absl::string_view key, absl::string_view val) override { setByKey(key, val); }

  absl::flat_hash_map<std::string, std::string> data_;
};

/**
 * Fake stream codec factory for test. A simple plain protocol is created for this fake
 * factory. The message format of this protocol is shown below.
 *
 * Fake request message format:
 *   <INT Message Size><Protocol>|<Authority>|<PATH>|<METHOD>|<key>:<value>;*
 * Fake response message format:
     <INT Message Size><INT Status><Protocol>|<Status Detail>|<key>:<value>;*
 */
class FakeStreamCodecFactory : public CodecFactory {
public:
  class FakeRequest : public FakeStreamBase<Request> {
  public:
    absl::string_view protocol() const override { return protocol_; }
    absl::string_view authority() const override { return authority_; }
    absl::string_view path() const override { return path_; }
    absl::string_view method() const override { return method_; }

    std::string protocol_;
    std::string authority_;
    std::string path_;
    std::string method_;
  };

  class FakeResponse : public FakeStreamBase<Response> {
  public:
    absl::string_view protocol() const override { return protocol_; }
    Status status() const override { return status_; }
    absl::string_view statusDetail() const override { return status_detail_; }

    std::string protocol_;
    Status status_;
    std::string status_detail_;
  };

  class FakeRequestDecoder : public RequestDecoder {
  public:
    bool parseRequestBody() {
      std::string body(message_size_.value(), 0);
      buffer_.copyOut(0, message_size_.value(), body.data());
      buffer_.drain(message_size_.value());
      message_size_.reset();

      std::vector<absl::string_view> result = absl::StrSplit(body, '|');
      if (result.size() != 5) {
        callback_->onDecodingError();
        return false;
      }

      auto request = std::make_unique<FakeRequest>();
      request->protocol_ = std::string(result[0]);
      request->authority_ = std::string(result[1]);
      request->path_ = std::string(result[2]);
      request->method_ = std::string(result[3]);
      for (absl::string_view pair_str : absl::StrSplit(result[4], ';')) {
        auto pair = absl::StrSplit(pair_str, absl::MaxSplits(':', 1));
        request->data_.emplace(pair);
      }

      callback_->onRequest(std::move(request));
      return true;
    }

    void setDecoderCallback(RequestDecoderCallback& callback) override { callback_ = &callback; }
    void decode(Buffer::Instance& buffer) override {
      buffer_.move(buffer);
      while (true) {
        if (!message_size_.has_value()) {
          if (buffer_.length() < 4) {
            // Wait for more data.
            return;
          }
          // Parsing message size.
          message_size_ = buffer_.peekBEInt<uint32_t>();
          buffer_.drain(4);
        }

        if (buffer_.length() < message_size_.value()) {
          // Wait for more data.
          return;
        }
        // There is enough data to parse a request.
        if (!parseRequestBody()) {
          return;
        }
      }
    }

    absl::optional<uint32_t> message_size_;
    Buffer::OwnedImpl buffer_;
    RequestDecoderCallback* callback_{};
  };

  class FakeResponseDecoder : public ResponseDecoder {
  public:
    bool parseResponseBody() {
      uint32_t status = buffer_.peekBEInt<uint32_t>();
      buffer_.drain(4);
      message_size_ = message_size_.value() - 4;

      std::string body(message_size_.value(), 0);
      buffer_.copyOut(0, message_size_.value(), body.data());
      buffer_.drain(message_size_.value());
      message_size_.reset();

      std::vector<absl::string_view> result = absl::StrSplit(body, '|');
      if (result.size() != 3) {
        callback_->onDecodingError();
        return false;
      }

      auto response = std::make_unique<FakeResponse>();
      response->status_ = Status(status);
      response->protocol_ = std::string(result[0]);
      response->status_detail_ = std::string(result[1]);
      for (absl::string_view pair_str : absl::StrSplit(result[2], ';')) {
        auto pair = absl::StrSplit(pair_str, absl::MaxSplits(':', 1));
        response->data_.emplace(pair);
      }

      callback_->onResponse(std::move(response));
      return true;
    }

    void setDecoderCallback(ResponseDecoderCallback& callback) override { callback_ = &callback; }
    void decode(Buffer::Instance& buffer) override {
      buffer_.move(buffer);
      while (true) {
        if (!message_size_.has_value()) {
          if (buffer_.length() < 4) {
            // Wait for more data.
            return;
          }
          // Parsing message size.
          message_size_ = buffer_.peekBEInt<uint32_t>();
          buffer_.drain(4);

          if (message_size_.value() < 4) {
            callback_->onDecodingError();
            return;
          }
        }

        if (buffer_.length() < message_size_.value()) {
          // Wait for more data.
          return;
        }

        // There is enough data to parse a response.
        if (!parseResponseBody()) {
          return;
        }
      }
    }

    absl::optional<uint32_t> message_size_;
    Buffer::OwnedImpl buffer_;
    ResponseDecoderCallback* callback_{};
  };

  class FakeRequestEncoder : public RequestEncoder {
  public:
    void encode(const Request& request, Buffer::Instance& buffer) override {
      const FakeRequest* typed_request = dynamic_cast<const FakeRequest*>(&request);
      ASSERT(typed_request != nullptr);

      std::string body;
      body.reserve(512);
      body = typed_request->protocol_ + "|" + typed_request->authority_ + "|" +
             typed_request->path_ + "|" + typed_request->method_ + "|";
      for (const auto& pair : typed_request->data_) {
        body += pair.first + ":" + pair.second + ";";
      }
      buffer.writeBEInt<uint32_t>(body.size());
      buffer.add(body);
    }
  };

  class FakeResponseEncoder : public ResponseEncoder {
  public:
    void encode(const Response& request, Buffer::Instance& buffer) override {
      const FakeResponse* typed_response = dynamic_cast<const FakeResponse*>(&request);
      ASSERT(typed_response != nullptr);

      std::string body;
      body.reserve(512);
      body = typed_response->protocol_ + "|" + typed_response->status_detail_ + "|";
      for (const auto& pair : typed_response->data_) {
        body += pair.first + ":" + pair.second + ";";
      }
      // Additional 4 bytes for status.
      buffer.writeBEInt<uint32_t>(body.size() + 4);
      buffer.writeBEInt<uint32_t>(static_cast<uint32_t>(typed_response->status_));
      buffer.add(body);
    }
  };

  class FakeMessageCreator : public MessageCreator {
  public:
    ResponsePtr response(Status status, absl::string_view status_detail, Request*) override {
      auto response = std::make_unique<FakeResponse>();
      response->status_ = status;
      response->status_detail_ = std::string(status_detail);
      response->protocol_ = "fake_protocol_for_test";
      return response;
    }
  };

  RequestDecoderPtr requestDecoder() const override;
  ResponseDecoderPtr responseDecoder() const override;
  RequestEncoderPtr requestEncoder() const override;
  ResponseEncoderPtr responseEncoder() const override;
  MessageCreatorPtr messageCreator() const override;
};

class FakeStreamCodecFactoryConfig : public CodecFactoryConfig {
public:
  CodecFactoryPtr createFactory(const Protobuf::Message& config,
                                Envoy::Server::Configuration::FactoryContext& context) override;
  ProtobufTypes::MessagePtr createEmptyConfigProto() override { return nullptr; }

  std::string name() const override { return "envoy.generic_proxy.codec.fake"; }
};

} // namespace MetaProtocolProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
