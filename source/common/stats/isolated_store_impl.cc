#include "source/common/stats/isolated_store_impl.h"

#include <algorithm>
#include <cstring>
#include <string>

#include "source/common/common/utility.h"
#include "source/common/stats/histogram_impl.h"
#include "source/common/stats/utility.h"

#include "symbol_table.h"

namespace Envoy {
namespace Stats {

IsolatedStoreImpl::IsolatedStoreImpl() : IsolatedStoreImpl(std::make_unique<SymbolTableImpl>()) {}

IsolatedStoreImpl::IsolatedStoreImpl(std::unique_ptr<SymbolTable>&& symbol_table)
    : IsolatedStoreImpl(*symbol_table) {
  symbol_table_storage_ = std::move(symbol_table);
}

static StatNameTagVector tagVectorFromOpt(StatNameTagVectorOptConstRef tags) {
  return tags ? tags->get() : StatNameTagVector{};
}

IsolatedStoreImpl::IsolatedStoreImpl(SymbolTable& symbol_table)
    : alloc_(symbol_table),
      counters_([this](const TagUtility::TagStatNameJoiner& joiner,
                       StatNameTagVectorOptConstRef tags) -> CounterSharedPtr {
        return alloc_.makeCounter(joiner.nameWithTags(), joiner.tagExtractedName(),
                                  tagVectorFromOpt(tags));
      }),
      gauges_([this](const TagUtility::TagStatNameJoiner& joiner, StatNameTagVectorOptConstRef tags,
                     Gauge::ImportMode import_mode) -> GaugeSharedPtr {
        return alloc_.makeGauge(joiner.nameWithTags(), joiner.tagExtractedName(),
                                tagVectorFromOpt(tags), import_mode);
      }),
      histograms_([this](const TagUtility::TagStatNameJoiner& joiner,
                         StatNameTagVectorOptConstRef tags,
                         Histogram::Unit unit) -> HistogramSharedPtr {
        return {new HistogramImpl(joiner.nameWithTags(), unit, *this, joiner.tagExtractedName(),
                                  tagVectorFromOpt(tags))};
      }),
      text_readouts_([this](const TagUtility::TagStatNameJoiner& joiner,
                            StatNameTagVectorOptConstRef tags,
                            TextReadout::Type) -> TextReadoutSharedPtr {
        return alloc_.makeTextReadout(joiner.nameWithTags(), joiner.tagExtractedName(),
                                      tagVectorFromOpt(tags));
      }),
      null_counter_(symbol_table), null_gauge_(symbol_table), null_histogram_(symbol_table),
      null_text_readout_(symbol_table) {}

ScopeSharedPtr IsolatedStoreImpl::rootScope() {
  if (lazy_default_scope_ == nullptr) {
    lazy_default_scope_ = makeScope({});
  }
  return lazy_default_scope_;
}

ConstScopeSharedPtr IsolatedStoreImpl::constRootScope() const {
  return const_cast<IsolatedStoreImpl*>(this)->rootScope();
}

IsolatedStoreImpl::~IsolatedStoreImpl() = default;

ScopeSharedPtr IsolatedScopeImpl::createScope(const std::string& name, bool,
                                              const ScopeStatsLimitSettings& limits,
                                              StatsMatcherSharedPtr matcher) {
  if (name.empty()) {
    return scopeFromStatName(StatNameSpan{}, false, limits, std::move(matcher));
  }

  StatNameManagedStorage stat_name_storage(Utility::sanitizeStatsName(name), symbolTable());
  return scopeFromStatName(stat_name_storage.statName(), false, limits, std::move(matcher));
}

ScopeSharedPtr IsolatedScopeImpl::scopeFromStatName(StatName name, bool,
                                                    const ScopeStatsLimitSettings&,
                                                    StatsMatcherSharedPtr matcher) {
  return scopeFromStatName(StatNameSpan{name}, false, {}, std::move(matcher));
}

ScopeSharedPtr IsolatedScopeImpl::scopeFromStatName(StatNameSpan names, bool,
                                                    const ScopeStatsLimitSettings&,
                                                    StatsMatcherSharedPtr matcher) {
  StatNameVec name_vec(prefix_vector_);
  name_vec.insert(name_vec.end(), names.begin(), names.end());

  StatsMatcherSharedPtr child_matcher = matcher ? std::move(matcher) : scope_matcher_;
  ScopeSharedPtr scope = store_.makeScope(name_vec, std::move(child_matcher));
  addScopeToStore(scope);
  return scope;
}

ScopeSharedPtr IsolatedStoreImpl::makeScope(StatNameSpan names, StatsMatcherSharedPtr matcher) {
  return std::make_shared<IsolatedScopeImpl>(names, *this, std::move(matcher));
}

} // namespace Stats
} // namespace Envoy
