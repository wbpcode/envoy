#include "extensions/filters/network/dubbo_proxy/hessian_utils.h"

#include <type_traits>

#include "common/common/assert.h"
#include "common/common/fmt.h"

#include "extensions/filters/network/dubbo_proxy/buffer_helper.h"

#include "absl/strings/str_cat.h"

#include <iostream>

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {

void BufferWriter::rawWrite(const void* data, uint64_t size) { buffer_.add(data, size); }

void BufferWriter::rawWrite(absl::string_view data) {
  std::cout << typeid(absl::string_view).hash_code() << std::endl;
  std::cout << typeid(absl::string_view).name() << std::endl;

  std::cout << "writer" << data.size() << std::endl;
  std::cout << data.substr(0,20).data() << std::endl;

  buffer_.add(data);
}

void BufferReader::rawReadNBytes(void* data, size_t len, size_t offset_value) {
  buffer_.copyOut(offset() + offset_value, len, data);
}

} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
