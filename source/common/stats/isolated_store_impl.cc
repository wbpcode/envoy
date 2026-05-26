#include "source/common/stats/isolated_store_impl.h"

#include <algorithm>
#include <cstring>
#include <string>

#include "source/common/common/utility.h"
#include "source/common/stats/histogram_impl.h"
#include "source/common/stats/utility.h"

#include "tag_utility.h"

namespace Envoy {
namespace Stats {

IsolatedStoreImpl::IsolatedStoreImpl(bool use_element_scope)
    : IsolatedStoreImpl(std::make_unique<SymbolTableImpl>(), use_element_scope) {}

IsolatedStoreImpl::IsolatedStoreImpl(std::unique_ptr<SymbolTable>&& symbol_table,
                                     bool use_element_scope)
    : IsolatedStoreImpl(*symbol_table, use_element_scope) {
  symbol_table_storage_ = std::move(symbol_table);
}

static StatNameTagSpan tagSpanFromOpt(absl::optional<StatNameTagSpan> tags) {
  return tags ? tags.value() : StatNameTagSpan{};
}

IsolatedStoreImpl::IsolatedStoreImpl(SymbolTable& symbol_table, bool use_element_scope)
    : alloc_(symbol_table),
      counters_([this](const TagUtility::TagStatNameJoiner& joiner) -> CounterSharedPtr {
        return alloc_.makeCounter(joiner.nameWithTags(), joiner.tagExtractedName(),
                                  tagSpanFromOpt(joiner.effectiveTags()));
      }),
      gauges_([this](const TagUtility::TagStatNameJoiner& joiner,
                     Gauge::ImportMode import_mode) -> GaugeSharedPtr {
        return alloc_.makeGauge(joiner.nameWithTags(), joiner.tagExtractedName(),
                                tagSpanFromOpt(joiner.effectiveTags()), import_mode);
      }),
      histograms_([this](const TagUtility::TagStatNameJoiner& joiner,
                         Histogram::Unit unit) -> HistogramSharedPtr {
        return {new HistogramImpl(joiner.nameWithTags(), unit, *this, joiner.tagExtractedName(),
                                  tagSpanFromOpt(joiner.effectiveTags()))};
      }),
      text_readouts_([this](const TagUtility::TagStatNameJoiner& joiner,
                            TextReadout::Type) -> TextReadoutSharedPtr {
        return alloc_.makeTextReadout(joiner.nameWithTags(), joiner.tagExtractedName(),
                                      tagSpanFromOpt(joiner.effectiveTags()));
      }),
      null_counter_(symbol_table), null_gauge_(symbol_table), null_histogram_(symbol_table),
      null_text_readout_(symbol_table), use_element_scope_(use_element_scope) {}

ScopeSharedPtr IsolatedStoreImpl::rootScope() {
  if (lazy_default_scope_ == nullptr) {
    if (use_element_scope_) {
      lazy_default_scope_ =
          std::make_shared<ElementScopeImpl>(nullptr, StatElementVec{}, *this, nullptr);
    } else {
      StatNameManagedStorage name_storage("", symbolTable());
      lazy_default_scope_ = makeScope(name_storage.statName());
    }
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

ElementScopeImpl::ElementScopeImpl(std::unique_ptr<StatNamePool>&& pool,
                                   StatElementVec&& prefix_elements, IsolatedStoreImpl& store,
                                   StatsMatcherSharedPtr matcher)
    : IsolatedScopeImpl(
          TagUtility::buildScopePrefixStorage(prefix_elements, store.symbolTable()).statName(),
          store, std::move(matcher)),
      pool_(std::move(pool)), prefix_elements_(std::move(prefix_elements)) {}

Counter& ElementScopeImpl::getOrCreateCounter(StatElementSpan names) {
  const OptRef<const StatsMatcher> matcher = makeOptRefFromPtr(scope_matcher_.get());
  const TagUtility::TagStatNameJoiner joiner(prefix_elements_, names, symbolTable());
  return store_.counters_.get(joiner, matcher).value_or(store_.null_counter_);
}

Gauge& ElementScopeImpl::getOrCreateGauge(StatElementSpan names, Gauge::ImportMode import_mode) {
  const OptRef<const StatsMatcher> matcher = makeOptRefFromPtr(scope_matcher_.get());
  const TagUtility::TagStatNameJoiner joiner(prefix_elements_, names, symbolTable());
  auto gauge = store_.gauges_.get(joiner, import_mode, matcher);
  if (!gauge.has_value()) {
    return store_.null_gauge_;
  }
  gauge->mergeImportMode(import_mode);
  return *gauge;
}

Histogram& ElementScopeImpl::getOrCreateHistogram(StatElementSpan names, Histogram::Unit unit) {
  const OptRef<const StatsMatcher> matcher = makeOptRefFromPtr(scope_matcher_.get());
  const TagUtility::TagStatNameJoiner joiner(prefix_elements_, names, symbolTable());
  return store_.histograms_.get(joiner, unit, matcher).value_or(store_.null_histogram_);
}

TextReadout& ElementScopeImpl::getOrCreateTextReadout(StatElementSpan names) {
  const OptRef<const StatsMatcher> matcher = makeOptRefFromPtr(scope_matcher_.get());
  const TagUtility::TagStatNameJoiner joiner(prefix_elements_, names, symbolTable());
  return store_.text_readouts_.get(joiner, TextReadout::Type::Default, matcher)
      .value_or(store_.null_text_readout_);
}

ScopeSharedPtr ElementScopeImpl::createScope(const std::string& name, bool,
                                             const ScopeStatsLimitSettings&,
                                             StatsMatcherSharedPtr matcher) {
  const std::string sanitized_name = Utility::sanitizeStatsName(name);
  StatElementViewVec names;
  TagUtility::populateWellKnownLegacyStatPrefix(sanitized_name, names);
  return createScope(names, false, {}, std::move(matcher));
}

ScopeSharedPtr ElementScopeImpl::scopeFromStatName(StatName name, bool,
                                                   const ScopeStatsLimitSettings&,
                                                   StatsMatcherSharedPtr matcher) {
  const std::string stat_name = symbolTable().toString(name);
  StatElementViewVec names;
  TagUtility::populateWellKnownLegacyStatPrefix(stat_name, names);
  return createScope(names, false, {}, std::move(matcher));
}

ScopeSharedPtr ElementScopeImpl::createScope(StatElementSpan names, bool,
                                             const ScopeStatsLimitSettings&,
                                             StatsMatcherSharedPtr matcher) {
  auto child_pool = std::make_unique<StatNamePool>(symbolTable());
  StatElementVec child_names;
  TagUtility::populatePoolAndElementVec(prefix_elements_, names, child_pool.get(), child_names);

  StatsMatcherSharedPtr child_matcher = matcher ? std::move(matcher) : scope_matcher_;
  auto child_scope = std::make_shared<ElementScopeImpl>(
      std::move(child_pool), std::move(child_names), store_, std::move(child_matcher));
  addScopeToStore(child_scope);
  return child_scope;
}

ScopeSharedPtr ElementScopeImpl::createScope(StatElementViewSpan names, bool,
                                             const ScopeStatsLimitSettings&,
                                             StatsMatcherSharedPtr matcher) {
  auto child_pool = std::make_unique<StatNamePool>(symbolTable());
  StatElementVec child_names;
  TagUtility::populatePoolAndElementVec(prefix_elements_, names, child_pool.get(), child_names);

  StatsMatcherSharedPtr child_matcher = matcher ? std::move(matcher) : scope_matcher_;
  auto child_scope = std::make_shared<ElementScopeImpl>(
      std::move(child_pool), std::move(child_names), store_, std::move(child_matcher));
  addScopeToStore(child_scope);
  return child_scope;
}

Counter& ElementScopeImpl::counterFromStatNameWithTags(const StatName& name,
                                                       StatNameTagVectorOptConstRef tags) {
  StatElementVec elements;
  elements.emplace_back(StatElement{.value = name});
  TagUtility::mergeTagsToElements(elements, tags);
  return getOrCreateCounter(elements);
}

Gauge& ElementScopeImpl::gaugeFromStatNameWithTags(const StatName& name,
                                                   StatNameTagVectorOptConstRef tags,
                                                   Gauge::ImportMode import_mode) {
  StatElementVec elements;
  elements.emplace_back(StatElement{.value = name});
  TagUtility::mergeTagsToElements(elements, tags);
  return getOrCreateGauge(elements, import_mode);
}

Histogram& ElementScopeImpl::histogramFromStatNameWithTags(const StatName& name,
                                                           StatNameTagVectorOptConstRef tags,
                                                           Histogram::Unit unit) {
  StatElementVec elements;
  elements.emplace_back(StatElement{.value = name});
  TagUtility::mergeTagsToElements(elements, tags);
  return getOrCreateHistogram(elements, unit);
}

TextReadout& ElementScopeImpl::textReadoutFromStatNameWithTags(const StatName& name,
                                                               StatNameTagVectorOptConstRef tags) {
  StatElementVec elements;
  elements.emplace_back(StatElement{.value = name});
  TagUtility::mergeTagsToElements(elements, tags);
  return getOrCreateTextReadout(elements);
}

} // namespace Stats
} // namespace Envoy
