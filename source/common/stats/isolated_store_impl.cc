#include "source/common/stats/isolated_store_impl.h"

#include <algorithm>
#include <cstring>
#include <string>

#include "source/common/common/utility.h"
#include "source/common/runtime/runtime_features.h"
#include "source/common/stats/histogram_impl.h"
#include "source/common/stats/utility.h"

namespace Envoy {
namespace Stats {

IsolatedStoreImpl::IsolatedStoreImpl() : IsolatedStoreImpl(std::make_unique<SymbolTableImpl>()) {}

IsolatedStoreImpl::IsolatedStoreImpl(std::unique_ptr<SymbolTable>&& symbol_table)
    : IsolatedStoreImpl(*symbol_table) {
  symbol_table_storage_ = std::move(symbol_table);
}

IsolatedStoreImpl::IsolatedStoreImpl(SymbolTable& symbol_table)
    : alloc_(symbol_table),
      counters_([this](const TagUtility::TagStatNameJoiner& joiner) -> CounterSharedPtr {
        return alloc_.makeCounter(joiner.nameWithTags(), joiner.tagExtractedName(),
                                  joiner.effectiveTags());
      }),
      gauges_([this](const TagUtility::TagStatNameJoiner& joiner,
                     Gauge::ImportMode import_mode) -> GaugeSharedPtr {
        return alloc_.makeGauge(joiner.nameWithTags(), joiner.tagExtractedName(),
                                joiner.effectiveTags(), import_mode);
      }),
      histograms_([this](const TagUtility::TagStatNameJoiner& joiner,
                         Histogram::Unit unit) -> HistogramSharedPtr {
        return {new HistogramImpl(joiner.nameWithTags(), unit, *this, joiner.tagExtractedName(),
                                  joiner.effectiveTags())};
      }),
      text_readouts_([this](const TagUtility::TagStatNameJoiner& joiner,
                            TextReadout::Type) -> TextReadoutSharedPtr {
        return alloc_.makeTextReadout(joiner.nameWithTags(), joiner.tagExtractedName(),
                                      joiner.effectiveTags());
      }),
      null_counter_(symbol_table), null_gauge_(symbol_table), null_histogram_(symbol_table),
      null_text_readout_(symbol_table) {}

ScopeSharedPtr IsolatedStoreImpl::rootScope() {
  if (lazy_default_scope_ == nullptr) {
    if (Runtime::runtimeFeatureEnabled("envoy.reloadable_features.no_stats_tag_extraction")) {
      lazy_default_scope_ =
          std::make_shared<ElementScopeImpl>(nullptr, StatElementVec{}, *this, nullptr);
      return lazy_default_scope_;
    }
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
                                              const ScopeStatsLimitSettings& limits,
                                              StatsMatcherSharedPtr matcher) {
  StatNameManagedStorage stat_name_storage(Utility::sanitizeStatsName(name), symbolTable());
  return scopeFromStatName(stat_name_storage.statName(), false, limits, std::move(matcher));
}

ScopeSharedPtr IsolatedScopeImpl::scopeFromStatName(StatName name, bool,
                                                    const ScopeStatsLimitSettings&,
                                                    StatsMatcherSharedPtr matcher) {
  SymbolTable::StoragePtr prefix_name_storage = symbolTable().join({prefix(), name});
  // Use explicit matcher if provided; otherwise inherit scope_matcher_.
  StatsMatcherSharedPtr child_matcher = matcher ? std::move(matcher) : scope_matcher_;
  ScopeSharedPtr scope =
      store_.makeScope(StatName(prefix_name_storage.get()), std::move(child_matcher));
  addScopeToStore(scope);
  return scope;
}

ScopeSharedPtr IsolatedStoreImpl::makeScope(StatName name, StatsMatcherSharedPtr matcher) {
  return std::make_shared<IsolatedScopeImpl>(name, *this, std::move(matcher));
}

void mergeElements(StatElementVec& merged, StatNameTagVectorOptConstRef tags) {
  if (tags.has_value()) {
    for (const auto& tag : tags->get()) {
      merged.emplace_back(StatElement{
          .value_ = tag.second,
          .name_ = tag.first,
      });
    }
  }
}

ElementScopeImpl::ElementScopeImpl(std::unique_ptr<StatNamePool>&& pool,
                                   StatElementVec&& prefix_elements, IsolatedStoreImpl& store,
                                   StatsMatcherSharedPtr matcher)
    : IsolatedScopeImpl("", store, std::move(matcher)), pool_(std::move(pool)),
      prefix_elements_(std::move(prefix_elements)) {}

Counter& ElementScopeImpl::getOrCreateCounter(StatElementSpan names) {
  const OptRef<const StatsMatcher> matcher = makeOptRefFromPtr(scope_matcher_.get());
  const TagUtility::TagStatNameJoiner joiner(names, symbolTable());
  return store_.counters_.get(joiner, matcher).value_or(store_.null_counter_);
}

Gauge& ElementScopeImpl::getOrCreateGauge(StatElementSpan names, Gauge::ImportMode import_mode) {
  const OptRef<const StatsMatcher> matcher = makeOptRefFromPtr(scope_matcher_.get());
  const TagUtility::TagStatNameJoiner joiner(names, symbolTable());
  auto gauge = store_.gauges_.get(joiner, import_mode, matcher);
  if (!gauge.has_value()) {
    return store_.null_gauge_;
  }
  gauge->mergeImportMode(import_mode);
  return *gauge;
}

Histogram& ElementScopeImpl::getOrCreateHistogram(StatElementSpan names, Histogram::Unit unit) {
  const OptRef<const StatsMatcher> matcher = makeOptRefFromPtr(scope_matcher_.get());
  const TagUtility::TagStatNameJoiner joiner(names, symbolTable());
  return store_.histograms_.get(joiner, unit, matcher).value_or(store_.null_histogram_);
}

TextReadout& ElementScopeImpl::getOrCreateTextReadout(StatElementSpan names) {
  const OptRef<const StatsMatcher> matcher = makeOptRefFromPtr(scope_matcher_.get());
  const TagUtility::TagStatNameJoiner joiner(names, symbolTable());
  return store_.text_readouts_.get(joiner, TextReadout::Type::Default, matcher)
      .value_or(store_.null_text_readout_);
}

ScopeSharedPtr ElementScopeImpl::createScope(const std::string& name, bool,
                                             const ScopeStatsLimitSettings&,
                                             StatsMatcherSharedPtr matcher) {
  return createScope({StatElementView{.value_ = name}}, false, {}, std::move(matcher));
}

ScopeSharedPtr ElementScopeImpl::scopeFromStatName(StatName name, bool,
                                                   const ScopeStatsLimitSettings&,
                                                   StatsMatcherSharedPtr matcher) {
  return createScope({StatElement{.value_ = name}}, false, {}, std::move(matcher));
}

template <class T>
void populatePoolAndElementVec(StatElementSpan prefix, T names, StatNamePool* pool,
                               StatElementVec& out) {
  out.reserve(prefix.size() + names.size());
  auto internStatName = [](StatNamePool* pool, auto name) -> StatName {
    return name.empty() ? StatName{} : pool->add(name);
  };
  for (const auto& elem : prefix) {
    out.emplace_back(internStatName(pool, elem.value_), internStatName(pool, elem.name_),
                     elem.ignore_name_);
  }
  for (const auto& elem : names) {
    out.emplace_back(internStatName(pool, elem.value_), internStatName(pool, elem.name_),
                     elem.ignore_name_);
  }
}

ScopeSharedPtr ElementScopeImpl::createScope(StatElementSpan names, bool,
                                             const ScopeStatsLimitSettings&,
                                             StatsMatcherSharedPtr matcher) {
  // Build a new pool that owns StatName storage for all child elements.
  auto child_pool = std::make_unique<StatNamePool>(symbolTable());
  StatElementVec child_names;
  populatePoolAndElementVec(prefix_elements_, names, child_pool.get(), child_names);

  // Inherit parent scope's matcher if child doesn't have one.
  StatsMatcherSharedPtr child_matcher = matcher ? std::move(matcher) : scope_matcher_;
  auto child_scope = std::make_shared<ElementScopeImpl>(
      std::move(child_pool), std::move(child_names), store(), std::move(child_matcher));
  addScopeToStore(child_scope);
  return child_scope;
}

ScopeSharedPtr ElementScopeImpl::createScope(StatElementViewSpan names, bool,
                                             const ScopeStatsLimitSettings&,
                                             StatsMatcherSharedPtr matcher) {

  // Build a new pool that owns StatName storage for all child elements.
  auto child_pool = std::make_unique<StatNamePool>(symbolTable());
  StatElementVec child_names;
  populatePoolAndElementVec(prefix_elements_, names, child_pool.get(), child_names);

  // Inherit parent scope's matcher if child doesn't have one.
  StatsMatcherSharedPtr child_matcher = matcher ? std::move(matcher) : scope_matcher_;
  auto child_scope = std::make_shared<ElementScopeImpl>(
      std::move(child_pool), std::move(child_names), store(), std::move(child_matcher));

  addScopeToStore(child_scope);
  return child_scope;
}

Counter& ElementScopeImpl::counterFromStatNameWithTags(const StatName& name,
                                                       StatNameTagVectorOptConstRef tags) {
  StatElementVec elements;
  elements.emplace_back(StatElement{.value_ = name});
  mergeElements(elements, tags);
  return getOrCreateCounter(elements);
}

Gauge& ElementScopeImpl::gaugeFromStatNameWithTags(const StatName& name,
                                                   StatNameTagVectorOptConstRef tags,
                                                   Gauge::ImportMode import_mode) {
  StatElementVec elements;
  elements.emplace_back(StatElement{.value_ = name});
  mergeElements(elements, tags);
  return getOrCreateGauge(elements, import_mode);
}

Histogram& ElementScopeImpl::histogramFromStatNameWithTags(const StatName& name,
                                                           StatNameTagVectorOptConstRef tags,
                                                           Histogram::Unit unit) {
  StatElementVec elements;
  elements.emplace_back(StatElement{.value_ = name});
  mergeElements(elements, tags);
  return getOrCreateHistogram(elements, unit);
}

TextReadout& ElementScopeImpl::textReadoutFromStatNameWithTags(const StatName& name,
                                                               StatNameTagVectorOptConstRef tags) {
  StatElementVec elements;
  elements.emplace_back(StatElement{.value_ = name});
  mergeElements(elements, tags);
  return getOrCreateTextReadout(elements);
}

} // namespace Stats
} // namespace Envoy
