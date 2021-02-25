#pragma once

#include "extensions/filters/network/dubbo_proxy/message_impl.h"
#include "extensions/filters/network/dubbo_proxy/serializer.h"

#include "hessian2/object.hpp"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {

class RpcInvocationImpl : public RpcInvocationBase {
public:
  // Each parameter consists of a parameter binary size and Hessian2::Object.
  using Parameters = std::vector<Hessian2::ObjectPtr>;
  using ParametersPtr = std::unique_ptr<Parameters>;

  class Attachment {
  public:
    using KeyValue = std::pair<Http::LowerCaseString, std::string>;
    using StringMap = absl::node_hash_map<std::string, KeyValue>;

    // Whether the attachment should be reserialized.
    bool shouldUpdate() const { return headers_updated_; }
    void shouldUpdate(bool update) { headers_updated_ = update; }

    const Http::HeaderMap& headers() const { return headers_; }
    const StringMap& originHeaders() const { return origin_headers_; }

    void insert(const std::string& key, const std::string& value);
    void update(const std::string& key, const std::string& value);
    void remove(const std::string& key);
    const std::string* lookup(const std::string& key);

  private:
    bool headers_updated_{false};

    StringMap origin_headers_;
    Http::RequestHeaderMapImpl headers_;
  };
  using AttachmentPtr = std::unique_ptr<Attachment>;

  class ParametersDetail {
    // Binary size before parameters. Include dubbo version, service name, service version, method
    // name and parameters type.
    size_t before_{};
    // Parameters number.
    size_t number_{};

    // The binary size of each parameter.
    std::vector<size_t> detail_{};
  };

  bool hasParameters() const { return parameters_ != nullptr; }
  const Parameters& parameters() const;
  Parameters& mutableParameters();

  ParametersDetail& parametersDetail() { return parameters_detail_; }

  bool hasAttachment() const { return attachment_ != nullptr; }
  const Attachment& attachment() const;
  Attachment& mutableAttachment();

private:
  inline void assignAttachmentIfNeed() {
    if (!attachment_) {
      attachment_ = std::make_unique<Parameters>();
    }
  }

  inline void assignParametersIfNeed() {
    if (!parameters_) {
      parameters_ = std::make_unique<Parameters>();
    }
  }

  ParametersDetail parameters_detail_;

  ParametersPtr parameters_;
  AttachmentPtr attachment_; // attachment
};

class RpcResultImpl : public RpcResult {
public:
  bool hasException() const override { return has_exception_; }
  void setException(bool has_exception) { has_exception_ = has_exception; }

private:
  bool has_exception_ = false;
};

} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
