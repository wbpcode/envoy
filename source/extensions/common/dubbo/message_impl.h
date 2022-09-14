#pragma once

#include "envoy/http/header_map.h"

#include "source/extensions/common/dubbo/hessian2_utils.h"
#include "source/extensions/common/dubbo/message.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {

class RpcRequestBase : public RpcRequest {
public:
  void setServiceName(absl::string_view name) { service_name_ = std::string(name); }
  void setMethodName(absl::string_view name) { method_name_ = std::string(name); }
  void setServiceVersion(absl::string_view version) { service_version_ = std::string(version); }
  void setServiceGroup(absl::string_view group) { group_ = std::string(group); }

  // RpcRequest
  absl::string_view serviceName() const override { return service_name_; }
  absl::string_view methodName() const override { return method_name_; }
  absl::string_view serviceVersion() const override { return service_version_; }
  absl::optional<absl::string_view> serviceGroup() const override {
    return group_.has_value() ? absl::make_optional<absl::string_view>(group_.value())
                              : absl::nullopt;
  }

protected:
  std::string service_name_;
  std::string method_name_;
  std::string service_version_;
  absl::optional<std::string> group_;
};

class RpcRequestImpl : public RpcRequestBase {
public:
  // Each parameter consists of a parameter binary size and Hessian2::Object.
  using Parameters = std::vector<Hessian2::ObjectPtr>;
  using ParametersPtr = std::unique_ptr<Parameters>;

  class Attachment {
  public:
    using Map = Hessian2::UntypedMapObject;
    using MapPtr = std::unique_ptr<Hessian2::UntypedMapObject>;
    using String = Hessian2::StringObject;

    Attachment(MapPtr&& value, size_t offset);

    const Map& attachment() const { return *attachment_; }

    void insert(const std::string& key, const std::string& value);
    void remove(const std::string& key);
    const std::string* lookup(const std::string& key) const;

    // Http::HeaderMap wrapper to attachment.
    const Http::HeaderMap& headers() const { return *headers_; }

    // Whether the attachment should be re-serialized.
    bool attachmentUpdated() const { return attachment_updated_; }

    size_t attachmentOffset() const { return attachment_offset_; }

  private:
    bool attachment_updated_{false};

    MapPtr attachment_;

    // The binary offset of attachment in the original message. Retaining this value can help
    // subsequent re-serialization of the attachment without re-serializing the parameters.
    size_t attachment_offset_{};

    // To reuse the HeaderMatcher API and related tools provided by Envoy, we store the key/value
    // pair of the string type in the attachment in the Http::HeaderMap. This introduces additional
    // overhead and ignores the case of the key in the attachment. But for now, it's acceptable.
    Http::HeaderMapPtr headers_;
  };
  using AttachmentPtr = std::unique_ptr<Attachment>;

  using AttachmentLazyCallback = std::function<AttachmentPtr()>;
  using ParametersLazyCallback = std::function<ParametersPtr()>;

  bool hasParameters() const { return parameters_ != nullptr; }
  const Parameters& parameters() const;
  ParametersPtr& mutableParameters() const;

  bool hasAttachment() const { return attachment_ != nullptr; }
  const Attachment& attachment() const;
  AttachmentPtr& mutableAttachment() const;

  void setParametersLazyCallback(ParametersLazyCallback&& callback) {
    parameters_lazy_callback_ = std::move(callback);
  }

  void setAttachmentLazyCallback(AttachmentLazyCallback&& callback) {
    attachment_lazy_callback_ = std::move(callback);
  }

  absl::optional<absl::string_view> serviceGroup() const override;

private:
  void assignParametersIfNeed() const;
  void assignAttachmentIfNeed() const;

  AttachmentLazyCallback attachment_lazy_callback_;
  ParametersLazyCallback parameters_lazy_callback_;

  mutable ParametersPtr parameters_{};
  mutable AttachmentPtr attachment_{};
};

class RpcResponseImpl : public RpcResponse {
public:
  void setException(bool has_exception) { has_exception_ = has_exception; }
  void setResponseType(RpcResponseType type) { response_type_ = type; }

  // RpcResponse
  bool hasException() const override { return has_exception_; }
  absl::optional<RpcResponseType> responseType() const override { return response_type_; }

private:
  bool has_exception_{false};
  absl::optional<RpcResponseType> response_type_;
};

} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
