#pragma once

#include "extensions/filters/network/dubbo_proxy/message.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {

class ContextImpl : public Context {
public:
  // DubboProxy::Context
  size_t headerSize() const override { return header_size_; }
  size_t bodySize() const override { return body_size_; }
  bool isHeartbeat() const override { return is_heartbeat_; }

  void setHeaderSize(size_t size) { header_size_ = size; }
  void setBodySize(size_t size) { body_size_ = size; }
  void setHeartbeat(bool is_heartbeat) { is_heartbeat_ = is_heartbeat; }

private:
  size_t header_size_{0};
  size_t body_size_{0};

  bool is_heartbeat_{false};
};

class RpcInvocationBase : public RpcInvocation {
public:
  ~RpcInvocationBase() override = default;

  void setServiceName(const std::string& name) { service_name_ = name; }
  const std::string& serviceName() const override { return service_name_; }

  void setMethodName(const std::string& name) { method_name_ = name; }
  const std::string& methodName() const override { return method_name_; }

  void setServiceVersion(const std::string& version) { service_version_ = version; }
  const absl::optional<std::string>& serviceVersion() const override { return service_version_; }

  void setServiceGroup(const std::string& group) { group_ = group; }
  const absl::optional<std::string>& serviceGroup() const override { return group_; }

protected:
  std::string service_name_;
  std::string method_name_;
  absl::optional<std::string> service_version_;
  absl::optional<std::string> group_;
};

} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
