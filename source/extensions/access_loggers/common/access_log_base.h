#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "envoy/access_log/access_log.h"
#include "envoy/runtime/runtime.h"

#include "source/common/http/header_utility.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/access_log/access_log_impl.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace Common {

/**
 * Base implementation of Accesslog::Instance handles common filter logic.
 */
class ImplBase : public AccessLog::Instance {
public:
  ImplBase(AccessLog::FilterPtr filter) : filter_(std::move(filter)) {}

  /**
   * Log a completed request if the underlying AccessLog `filter_` allows it.
   */
  void log(const AccessLog::HttpLogContext& context,
           const StreamInfo::StreamInfo& stream_info) override;

private:
  /**
   * Log a completed request.
   * @param context supplies the necessary context to log.
   * @param stream_info supplies additional information about the request not
   * contained in the request headers.
   */
  virtual void emitLog(const AccessLog::HttpLogContext& context,
                       const StreamInfo::StreamInfo& stream_info) PURE;

  AccessLog::FilterPtr filter_;
};

} // namespace Common
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
