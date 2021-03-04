#include "extensions/filters/network/dubbo_proxy/hessian_utils.h"

#include <iostream>
#include <type_traits>

#include "common/common/assert.h"
#include "common/common/fmt.h"

#include "extensions/filters/network/dubbo_proxy/buffer_helper.h"

#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {

uint32_t HessianUtils::getParametersNumber(const std::string& parameters_type) {
  if (parameters_type.empty()) {
    return 0;
  }

  uint32_t count = 0;
  bool next = false;

  for (auto ch : parameters_type) {
    if (ch == '[') {
      // Is array.
      continue;
    }

    if (next && ch != ';') {
      // Is Object.
      continue;
    }

    switch (ch) {
    case 'V':
    case 'Z':
    case 'B':
    case 'C':
    case 'D':
    case 'F':
    case 'I':
    case 'J':
    case 'S':
      count++;
      break;
    case 'L':
      // Start of Object.
      count++;
      next = true;
      break;
    case ';':
      // End of Object.
      next = false;
      break;
    default:
      break;
    }
  }
  return count;
}

void BufferWriter::rawWrite(const void* data, uint64_t size) { buffer_.add(data, size); }

void BufferWriter::rawWrite(absl::string_view data) { buffer_.add(data); }

void BufferReader::rawReadNBytes(void* data, size_t len, size_t peek_offset) {
  ASSERT(byteAvailable() - peek_offset >= len);
  buffer_.copyOut(offset() + peek_offset, len, data);
}

} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
