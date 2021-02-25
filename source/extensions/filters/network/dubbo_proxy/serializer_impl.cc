#include "extensions/filters/network/dubbo_proxy/serializer_impl.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {

void RpcInvocationImpl::Attachment::insert(const std::string& key, const std::string& value) {
  if (origin_headers_.find(key) != origin_headers_.end()) {
    return;
  }
  auto result = origin_headers_.insert({key, {Http::LowerCaseString(key), value}});

  if (!result.second) {
    return;
  }

  headers_.addReference(result.first->second.first, result.first->second.second);
}
void RpcInvocationImpl::Attachment::update(const std::string& key, const std::string& value) {
  origin_headers_[key] = {Http::LowerCaseString(key), value};

  auto iter = origin_headers_.find(key);
  ASSERT(iter != origin_headers_.end());

  auto entry = headers_.get(iter->second.first);
  if (!entry.empty()) {
    entry[0]->value(iter->second.second);
  } else {
    headers_.addReference(iter->second.first, iter->second.second);
  }
}
void RpcInvocationImpl::Attachment::remove(const std::string& key) {
  auto iter = origin_headers_.find(key);
  if (iter == origin_headers_.end()) {
    return;
  }

  headers_.remove(iter->second.first);
  origin_headers_.erase(iter);
}

const std::string* RpcInvocationImpl::Attachment::lookup(const std::string& key) {
  auto iter = origin_headers_.find(key);
  if (iter == origin_headers_.end()) {
    return nullptr;
  }

  return &iter->second.second;
}

const RpcInvocationImpl::Attachment& RpcInvocationImpl::attachment() const {
  assignAttachmentIfNeed();
  return *attachment_;
}

RpcInvocationImpl::Attachment& RpcInvocationImpl::mutableAttachment() {
  assignAttachmentIfNeed();
  return *attachment_;
}

const RpcInvocationImpl::Parameters& RpcInvocationImpl::parameters() const {
  assignParametersIfNeed();
  return *parameters_;
}

RpcInvocationImpl::Parameters& RpcInvocationImpl::mutableParameters() {
  assignParametersIfNeed();
  return *parameters_;
}

} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
