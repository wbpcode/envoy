#include "source/common/stats/isolated_store_impl.h"

#include <algorithm>
#include <cstring>
#include <string>

#include "source/common/common/utility.h"
#include "source/common/stats/histogram_impl.h"
#include "source/common/stats/utility.h"

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
      counters_([this](const TagUtility::TagStatNameJoiner& joiner) -> CounterSharedPtr {
        return alloc_.makeCounter(joiner.nameWithTags(), joiner.tagExtractedName(),
                                  tagVectorFromOpt(joiner.effectiveTags()));
      }),
      gauges_([this](const TagUtility::TagStatNameJoiner& joiner,
                     Gauge::ImportMode import_mode) -> GaugeSharedPtr {
        return alloc_.makeGauge(joiner.nameWithTags(), joiner.tagExtractedName(),
                                tagVectorFromOpt(joiner.effectiveTags()), import_mode);
      }),
      histograms_([this](const TagUtility::TagStatNameJoiner& joiner,
                         Histogram::Unit unit) -> HistogramSharedPtr {
        return {new HistogramImpl(joiner.nameWithTags(), unit, *this, joiner.tagExtractedName(),
                                  tagVectorFromOpt(joiner.effectiveTags()))};
      }),
      text_readouts_([this](const TagUtility::TagStatNameJoiner& joiner,
                            TextReadout::Type) -> TextReadoutSharedPtr {
        return alloc_.makeTextReadout(joiner.nameWithTags(), joiner.tagExtractedName(),
                                      tagVectorFromOpt(joiner.effectiveTags()));
      }),
      null_counter_(symbol_table), null_gauge_(symbol_table), null_histogram_(symbol_table),
      null_text_readout_(symbol_table) {}

ScopeSharedPtr IsolatedStoreImpl::rootScope() {
  if (lazy_default_scope_ == nullptr) {
    StatNameManagedStorage name_storage("", symbolTable());
    lazy_default_scope_ = makeScope(name_storage.statName());
  }
  return lazy_default_scope_;
}

ConstScopeSharedPtr IsolatedStoreImpl::constRootScope() const {
  return const_cast<IsolatedStoreImpl*>(this)->rootScope();
}

IsolatedStoreImpl::~IsolatedStoreImpl() = default;

ScopeSharedPtr IsolatedScopeImpl::createScope(const std::string& name, bool,
                                              const ScopeStatsLimitSettings&,
                                              StatsMatcherSharedPtr matcher,
                                              TagViewVectorOptConstRef tags) {
  StatNameManagedStorage stat_name_storage(Utility::sanitizeStatsName(name), symbolTable());

  SymbolTable::StoragePtr prefix_name_storage =
      symbolTable().join({prefix(), stat_name_storage.statName()});
  StatsMatcherSharedPtr child_matcher = matcher ? std::move(matcher) : scope_matcher_;

  // Build the child scope's tag pool and tag vector. All StatNames are interned into child_pool
  // so the child scope is lifetime-independent of this scope and any caller-provided pool.
  std::unique_ptr<StatNamePool> child_pool;
  StatNameTagVector child_tags;
  const bool has_tags = !scope_tags_.empty() || tags.has_value();
  if (has_tags) {
    child_pool = std::make_unique<StatNamePool>(symbolTable());
    for (const auto& tag : scope_tags_) {
      child_tags.push_back(
          StatNameTag{child_pool->add(tag.name_), child_pool->add(tag.value_), tag.ignore_name_});
    }
    if (tags.has_value()) {
      for (const auto& tag : tags.ref()) {
        child_tags.push_back(
            StatNameTag{child_pool->add(tag.name_), child_pool->add(tag.value_), tag.ignore_name_});
      }
    }
  }

  ScopeSharedPtr scope =
      store_.makeScope(StatName(prefix_name_storage.get()), std::move(child_matcher),
                       std::move(child_pool), std::move(child_tags));
  addScopeToStore(scope);
  return scope;
}

ScopeSharedPtr IsolatedScopeImpl::scopeFromStatName(StatName name, bool,
                                                    const ScopeStatsLimitSettings&,
                                                    StatsMatcherSharedPtr matcher,
                                                    StatNameTagVectorOptConstRef tags) {
  SymbolTable::StoragePtr prefix_name_storage = symbolTable().join({prefix(), name});
  StatsMatcherSharedPtr child_matcher = matcher ? std::move(matcher) : scope_matcher_;

  // Build the child scope's tag pool and tag vector. All StatNames are interned into child_pool
  // so the child scope is lifetime-independent of this scope and any caller-provided pool.
  std::unique_ptr<StatNamePool> child_pool;
  StatNameTagVector child_tags;
  const bool has_tags = !scope_tags_.empty() || tags.has_value();
  if (has_tags) {
    child_pool = std::make_unique<StatNamePool>(symbolTable());
    for (const auto& tag : scope_tags_) {
      child_tags.push_back(
          StatNameTag{child_pool->add(tag.name_), child_pool->add(tag.value_), tag.ignore_name_});
    }
    if (tags.has_value()) {
      for (const auto& tag : tags->get()) {
        child_tags.push_back(
            StatNameTag{child_pool->add(tag.name_), child_pool->add(tag.value_), tag.ignore_name_});
      }
    }
  }

  ScopeSharedPtr scope =
      store_.makeScope(StatName(prefix_name_storage.get()), std::move(child_matcher),
                       std::move(child_pool), std::move(child_tags));
  addScopeToStore(scope);
  return scope;
}

ScopeSharedPtr IsolatedStoreImpl::makeScope(StatName name, StatsMatcherSharedPtr matcher,
                                            std::unique_ptr<StatNamePool> scope_tags_pool,
                                            StatNameTagVector scope_tags) {
  if (scope_tags.empty()) {
    return std::make_shared<IsolatedScopeImpl>(name, *this, std::move(matcher));
  }
  return std::make_shared<IsolatedScopeImpl>(name, *this, std::move(matcher),
                                             std::move(scope_tags_pool), std::move(scope_tags));
}

} // namespace Stats
} // namespace Envoy
