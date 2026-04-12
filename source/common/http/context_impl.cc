#include "source/common/http/context_impl.h"

#include "source/common/runtime/runtime_features.h"

namespace Envoy {
namespace Http {

ContextImpl::ContextImpl(Stats::SymbolTable& symbol_table)
    : no_tags_extraction_(
          Runtime::runtimeFeatureEnabled("envoy.reloadable_features.no_stats_tag_extraction")),
      code_stats_(symbol_table), element_code_stats_(symbol_table),
      user_agent_context_(symbol_table),
      async_client_stat_prefix_(user_agent_context_.pool_.add("http.async-client")) {}

CodeStats& ContextImpl::codeStats() {
  return no_tags_extraction_ ? static_cast<CodeStats&>(element_code_stats_)
                             : static_cast<CodeStats&>(code_stats_);
}

} // namespace Http
} // namespace Envoy
