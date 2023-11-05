#include "source/extensions/access_loggers/common/access_log_base.h"

#include "source/common/http/header_map_impl.h"
#include "source/common/singleton/const_singleton.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace Common {

void ImplBase::log(const AccessLog::HttpLogContext& context, const StreamInfo::StreamInfo& info) {
  if (filter_ && !filter_->evaluate(context, info)) {
    return;
  }
  return emitLog(context, info);
}

} // namespace Common
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
