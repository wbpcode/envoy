#include <string>

#include "envoy/config/metrics/v3/stats.pb.h"
#include "envoy/stats/stats_macros.h"

#include "source/common/stats/isolated_store_impl.h"
#include "source/common/stats/null_counter.h"
#include "source/common/stats/null_gauge.h"
#include "source/common/stats/stats_matcher_impl.h"

#include "test/mocks/server/server_factory_context.h"
#include "test/test_common/utility.h"

#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Stats {

class StatsIsolatedStoreImplTest : public testing::Test {
protected:
  StatsIsolatedStoreImplTest()
      : store_(std::make_unique<IsolatedStoreImpl>(symbol_table_)), pool_(symbol_table_),
        scope_(store_->rootScope()) {}
  ~StatsIsolatedStoreImplTest() override {
    pool_.clear();
    scope_.reset();
    store_.reset();
    EXPECT_EQ(0, symbol_table_.numSymbols());
  }

  StatName makeStatName(absl::string_view name) { return pool_.add(name); }

  SymbolTableImpl symbol_table_;
  std::unique_ptr<IsolatedStoreImpl> store_;
  StatNamePool pool_;
  ScopeSharedPtr scope_;
};

TEST_F(StatsIsolatedStoreImplTest, All) {
  EXPECT_TRUE(store_->fixedTags().empty());
  ScopeSharedPtr scope1 = scope_->createScope("scope1.");
  Counter& c1 = scope_->counterFromString("c1");
  Counter& c2 = scope1->counterFromString("c2");
  EXPECT_EQ("c1", c1.name());
  EXPECT_EQ("scope1.c2", c2.name());
  EXPECT_EQ("c1", c1.tagExtractedName());
  EXPECT_EQ("scope1.c2", c2.tagExtractedName());
  EXPECT_EQ(0, c1.tags().size());
  EXPECT_EQ(0, c1.tags().size());
  CounterOptConstRef opt_counter = scope1->findCounter(c2.statName());
  ASSERT_TRUE(opt_counter);
  EXPECT_EQ(&c2, &opt_counter->get());
  StatName not_found = pool_.add("not_found");
  EXPECT_FALSE(scope1->findCounter(not_found));

  StatNameManagedStorage c1_name("c1", store_->symbolTable());
  c1.add(100);
  auto found_counter = scope_->findCounter(c1_name.statName());
  ASSERT_TRUE(found_counter.has_value());
  EXPECT_EQ(&c1, &found_counter->get());
  EXPECT_EQ(100, found_counter->get().value());
  c1.add(100);
  EXPECT_EQ(200, found_counter->get().value());

  Gauge& g1 = scope_->gaugeFromString("g1", Gauge::ImportMode::Accumulate);
  Gauge& g2 = scope1->gaugeFromString("g2", Gauge::ImportMode::Accumulate);
  EXPECT_EQ("g1", g1.name());
  EXPECT_EQ("scope1.g2", g2.name());
  EXPECT_EQ("g1", g1.tagExtractedName());
  EXPECT_EQ("scope1.g2", g2.tagExtractedName());
  EXPECT_EQ(0, g1.tags().size());
  EXPECT_EQ(0, g2.tags().size());
  GaugeOptConstRef opt_gauge = scope1->findGauge(g2.statName());
  ASSERT_TRUE(opt_gauge);
  EXPECT_EQ(&g2, &opt_gauge->get());
  EXPECT_FALSE(scope1->findGauge(not_found));
  // TODO(jmarantz): There may be a bug with
  // scope1->findGauge(h1.statName()), which finds the histogram added to
  // the store, which is arguably not in the scope. Investigate what the
  // behavior should be.

  StatNameManagedStorage g1_name("g1", store_->symbolTable());
  g1.set(100);
  auto found_gauge = scope_->findGauge(g1_name.statName());
  ASSERT_TRUE(found_gauge.has_value());
  EXPECT_EQ(&g1, &found_gauge->get());
  EXPECT_EQ(100, found_gauge->get().value());
  g1.set(0);
  EXPECT_EQ(0, found_gauge->get().value());

  Histogram& h1 = scope_->histogramFromString("h1", Stats::Histogram::Unit::Unspecified);
  EXPECT_TRUE(h1.used()); // hardcoded in impl to be true always.
  EXPECT_TRUE(h1.use_count() == 1);
  Histogram& h2 = scope1->histogramFromString("h2", Stats::Histogram::Unit::Unspecified);
  store_->deliverHistogramToSinks(h2, 0);
  EXPECT_EQ("h1", h1.name());
  EXPECT_EQ("scope1.h2", h2.name());
  EXPECT_EQ("h1", h1.tagExtractedName());
  EXPECT_EQ("scope1.h2", h2.tagExtractedName());
  EXPECT_EQ(0, h1.tags().size());
  EXPECT_EQ(0, h2.tags().size());
  h1.recordValue(200);
  h2.recordValue(200);
  HistogramOptConstRef opt_histogram = scope1->findHistogram(h2.statName());
  ASSERT_TRUE(opt_histogram);
  EXPECT_EQ(&h2, &opt_histogram->get());
  EXPECT_FALSE(scope1->findHistogram(not_found));
  // TODO(jmarantz): There may be a bug with
  // scope1->findHistogram(h1.statName()), which finds the histogram added to
  // the store, which is arguably not in the scope. Investigate what the
  // behavior should be.

  StatNameManagedStorage h1_name("h1", store_->symbolTable());
  auto found_histogram = scope_->findHistogram(h1_name.statName());
  ASSERT_TRUE(found_histogram.has_value());
  EXPECT_EQ(&h1, &found_histogram->get());

  ScopeSharedPtr scope2 = scope1->scopeFromStatName(makeStatName("foo."));
  EXPECT_EQ("scope1.foo.bar", scope2->counterFromString("bar").name());

  // Validate that we sanitize away bad characters in the stats prefix.
  ScopeSharedPtr scope3 = scope1->createScope(std::string("foo:\0:.", 7));
  EXPECT_EQ("scope1.foo___.bar", scope3->counterFromString("bar").name());

  EXPECT_EQ(4UL, store_->counters().size());
  EXPECT_EQ(2UL, store_->gauges().size());

  StatNameManagedStorage nonexistent_name("nonexistent", store_->symbolTable());
  EXPECT_EQ(scope_->findCounter(nonexistent_name.statName()), absl::nullopt);
  EXPECT_EQ(scope_->findGauge(nonexistent_name.statName()), absl::nullopt);
  EXPECT_EQ(scope_->findHistogram(nonexistent_name.statName()), absl::nullopt);
}

TEST_F(StatsIsolatedStoreImplTest, CleanupCallback) {
  bool called = false;
  {
    IsolatedStoreImpl local_store(symbol_table_);
    ScopeSharedPtr scope1 = local_store.rootScope()->createScope("scope1.");
    scope1->setCleanupCallback([&called]() { called = true; });
  }
  EXPECT_TRUE(called);
}

TEST_F(StatsIsolatedStoreImplTest, PrefixIsStatName) {
  ScopeSharedPtr scope1 = scope_->createScope("scope1");
  ScopeSharedPtr scope2 = scope1->scopeFromStatName(makeStatName("scope2"));
  Counter& c1 = scope2->counterFromString("c1");
  EXPECT_EQ("scope1.scope2.c1", c1.name());
}

TEST_F(StatsIsolatedStoreImplTest, AllWithSymbolTable) {
  ScopeSharedPtr scope1 = scope_->createScope("scope1.");
  Counter& c1 = scope_->counterFromStatName(makeStatName("c1"));
  Counter& c2 = scope1->counterFromStatName(makeStatName("c2"));
  EXPECT_EQ("c1", c1.name());
  EXPECT_EQ("scope1.c2", c2.name());
  EXPECT_EQ("c1", c1.tagExtractedName());
  EXPECT_EQ("scope1.c2", c2.tagExtractedName());
  EXPECT_EQ(0, c1.tags().size());
  EXPECT_EQ(0, c1.tags().size());

  Gauge& g1 = scope_->gaugeFromStatName(makeStatName("g1"), Gauge::ImportMode::Accumulate);
  Gauge& g2 = scope1->gaugeFromStatName(makeStatName("g2"), Gauge::ImportMode::Accumulate);
  EXPECT_EQ("g1", g1.name());
  EXPECT_EQ("scope1.g2", g2.name());
  EXPECT_EQ("g1", g1.tagExtractedName());
  EXPECT_EQ("scope1.g2", g2.tagExtractedName());
  EXPECT_EQ(0, g1.tags().size());
  EXPECT_EQ(0, g2.tags().size());

  TextReadout& b1 = scope_->textReadoutFromStatName(makeStatName("b1"));
  TextReadout& b2 = scope1->textReadoutFromStatName(makeStatName("b2"));
  EXPECT_NE(&b1, &b2);
  EXPECT_EQ("b1", b1.name());
  EXPECT_EQ("scope1.b2", b2.name());
  EXPECT_EQ("b1", b1.tagExtractedName());
  EXPECT_EQ("scope1.b2", b2.tagExtractedName());
  EXPECT_EQ(0, b1.tags().size());
  EXPECT_EQ(0, b2.tags().size());
  Histogram& h1 =
      scope_->histogramFromStatName(makeStatName("h1"), Stats::Histogram::Unit::Unspecified);
  Histogram& h2 =
      scope1->histogramFromStatName(makeStatName("h2"), Stats::Histogram::Unit::Unspecified);
  store_->deliverHistogramToSinks(h2, 0);
  EXPECT_EQ("h1", h1.name());
  EXPECT_EQ("scope1.h2", h2.name());
  EXPECT_EQ("h1", h1.tagExtractedName());
  EXPECT_EQ("scope1.h2", h2.tagExtractedName());
  EXPECT_EQ(0, h1.tags().size());
  EXPECT_EQ(0, h2.tags().size());
  h1.recordValue(200);
  h2.recordValue(200);

  ScopeSharedPtr scope2 = scope1->createScope("foo.");
  EXPECT_EQ("scope1.foo.bar", scope2->counterFromStatName(makeStatName("bar")).name());

  // Validate that we sanitize away bad characters in the stats prefix.
  ScopeSharedPtr scope3 = scope1->createScope(std::string("foo:\0:.", 7));
  EXPECT_EQ("scope1.foo___.bar", scope3->counterFromString("bar").name());

  EXPECT_EQ(4UL, store_->counters().size());
  EXPECT_EQ(2UL, store_->gauges().size());
  EXPECT_EQ(2UL, store_->textReadouts().size());
}

TEST_F(StatsIsolatedStoreImplTest, CounterWithTag) {
  StatNameTagVector tags{{makeStatName("tag1"), makeStatName("tag1Value")}};
  StatNameTagVector tags2{{makeStatName("tag1"), makeStatName("tag1Value2")}};
  StatName base = makeStatName("counter");
  Counter& c1 = scope_->counterFromStatNameWithTags(base, tags);
  Counter& c2 = scope_->counterFromStatNameWithTags(base, tags2);
  EXPECT_EQ("counter.tag1.tag1Value", c1.name());
  EXPECT_EQ("counter", c1.tagExtractedName());
  EXPECT_THAT(c1.tags(), testing::ElementsAre(Tag{"tag1", "tag1Value"}));
  EXPECT_EQ("counter.tag1.tag1Value2", c2.name());
  EXPECT_EQ("counter", c2.tagExtractedName());
  EXPECT_THAT(c2.tags(), testing::ElementsAre(Tag{"tag1", "tag1Value2"}));
  // Verify that counterFromStatNameWithTags with the same params returns
  // the existing stat object.
  EXPECT_EQ(&c1, &scope_->counterFromStatNameWithTags(base, tags));
}

TEST_F(StatsIsolatedStoreImplTest, GaugeWithTags) {
  StatNameTagVector tags{{makeStatName("tag1"), makeStatName("tag1Value")},
                         {makeStatName("tag2"), makeStatName("tag2Value")}};
  // tags2 being a subset of tags to ensure no collision in that case.
  StatNameTagVector tags2{{makeStatName("tag2"), makeStatName("tag2Value")}};
  StatName base = makeStatName("gauge");
  Gauge& g1 = scope_->gaugeFromStatNameWithTags(base, tags, Gauge::ImportMode::Accumulate);
  Gauge& g2 = scope_->gaugeFromStatNameWithTags(base, tags2, Gauge::ImportMode::Accumulate);
  EXPECT_EQ("gauge.tag1.tag1Value.tag2.tag2Value", g1.name());
  EXPECT_EQ("gauge", g1.tagExtractedName());
  EXPECT_THAT(g1.tags(), testing::ElementsAre(Tag{"tag1", "tag1Value"}, Tag{"tag2", "tag2Value"}));
  EXPECT_EQ("gauge.tag2.tag2Value", g2.name());
  EXPECT_EQ("gauge", g2.tagExtractedName());
  EXPECT_THAT(g2.tags(), testing::ElementsAre(Tag{"tag2", "tag2Value"}));
  // Verify that gaugeFromStatNameWithTags with the same params returns
  // the existing stat object.
  EXPECT_EQ(&g1, &scope_->gaugeFromStatNameWithTags(base, tags, Gauge::ImportMode::Accumulate));
}

TEST_F(StatsIsolatedStoreImplTest, TextReadoutWithTag) {
  StatNameTagVector tags{{makeStatName("tag1"), makeStatName("tag1Value")}};
  StatNameTagVector tags2{{makeStatName("tag1"), makeStatName("tag1Value2")}};
  StatName base = makeStatName("textreadout");
  TextReadout& b1 = scope_->textReadoutFromStatNameWithTags(base, tags);
  TextReadout& b2 = scope_->textReadoutFromStatNameWithTags(base, tags2);
  EXPECT_EQ("textreadout.tag1.tag1Value", b1.name());
  EXPECT_EQ("textreadout", b1.tagExtractedName());
  EXPECT_THAT(b1.tags(), testing::ElementsAre(Tag{"tag1", "tag1Value"}));
  EXPECT_EQ("textreadout.tag1.tag1Value2", b2.name());
  EXPECT_EQ("textreadout", b2.tagExtractedName());
  EXPECT_THAT(b2.tags(), testing::ElementsAre(Tag{"tag1", "tag1Value2"}));
  // Verify that textReadoutFromStatNameWithTags with the same params returns
  // the existing stat object.
  EXPECT_EQ(&b1, &scope_->textReadoutFromStatNameWithTags(base, tags));
}

TEST(StatsIsolatedStoreElementScopeTest, CreateScopedStatsFromElements) {
  SymbolTableImpl symbol_table;
  StatNamePool pool(symbol_table);
  IsolatedStoreImpl store(symbol_table, true);

  ScopeSharedPtr scope = store.rootScope()->createScope(StatElementViewVec{
      {.value = "cluster"}, {.value = "service", .name = "cluster_name", .ignore_name = true}});

  Counter& counter = scope->getOrCreateCounter(StatElementVec{{.value = pool.add("rq_total")}});
  Gauge& gauge = scope->getOrCreateGauge(StatElementVec{{.value = pool.add("rq_active")}},
                                         Gauge::ImportMode::Accumulate);
  Histogram& histogram = scope->getOrCreateHistogram(StatElementVec{{.value = pool.add("latency")}},
                                                     Histogram::Unit::Milliseconds);
  TextReadout& text_readout =
      scope->getOrCreateTextReadout(StatElementVec{{.value = pool.add("state")}});

  EXPECT_EQ("cluster.service.rq_total", counter.name());
  EXPECT_EQ("cluster.rq_total", counter.tagExtractedName());
  EXPECT_THAT(counter.tags(), testing::ElementsAre(Tag{"cluster_name", "service"}));

  EXPECT_EQ("cluster.service.rq_active", gauge.name());
  EXPECT_EQ("cluster.rq_active", gauge.tagExtractedName());
  EXPECT_THAT(gauge.tags(), testing::ElementsAre(Tag{"cluster_name", "service"}));

  EXPECT_EQ("cluster.service.latency", histogram.name());
  EXPECT_EQ("cluster.latency", histogram.tagExtractedName());
  EXPECT_THAT(histogram.tags(), testing::ElementsAre(Tag{"cluster_name", "service"}));

  EXPECT_EQ("cluster.service.state", text_readout.name());
  EXPECT_EQ("cluster.state", text_readout.tagExtractedName());
  EXPECT_THAT(text_readout.tags(), testing::ElementsAre(Tag{"cluster_name", "service"}));
}

// Direct-StatName variant of createScope on an element-backed scope. Mirrors
// the StatElementViewSpan path tested above but exercises populatePoolAndElementVec's
// StatElement specialization.
TEST(StatsIsolatedStoreElementScopeTest, CreateScopeStatNameVariant) {
  SymbolTableImpl symbol_table;
  StatNamePool pool(symbol_table);
  IsolatedStoreImpl store(symbol_table, true);

  StatElementVec prefix{
      {.value = pool.add("cluster")},
      {.value = pool.add("service"), .name = pool.add("cluster_name"), .ignore_name = true}};
  ScopeSharedPtr scope = store.rootScope()->createScope(StatElementSpan(prefix));

  Counter& counter = scope->getOrCreateCounter(StatElementVec{{.value = pool.add("rq_total")}});
  EXPECT_EQ("cluster.service.rq_total", counter.name());
  EXPECT_EQ("cluster.rq_total", counter.tagExtractedName());
  EXPECT_THAT(counter.tags(), testing::ElementsAre(Tag{"cluster_name", "service"}));
}

// Legacy string-based createScope on an element-backed scope must produce the
// same name/tag layout as the direct element API for well-known prefixes.
TEST(StatsIsolatedStoreElementScopeTest, LegacyCreateScopeStringWellKnownPrefix) {
  SymbolTableImpl symbol_table;
  StatNamePool pool(symbol_table);
  IsolatedStoreImpl store(symbol_table, true);

  ScopeSharedPtr scope = store.rootScope()->createScope("cluster.service.");
  Counter& counter = scope->counterFromStatNameWithTags(pool.add("rq_total"), absl::nullopt);
  EXPECT_EQ("cluster.service.rq_total", counter.name());
  EXPECT_EQ("cluster.rq_total", counter.tagExtractedName());
  EXPECT_THAT(counter.tags(), testing::ElementsAre(Tag{"envoy.cluster_name", "service"}));
}

// Legacy StatName-based scopeFromStatName must walk through the same legacy
// prefix decomposition.
TEST(StatsIsolatedStoreElementScopeTest, LegacyScopeFromStatNameWellKnownPrefix) {
  SymbolTableImpl symbol_table;
  StatNamePool pool(symbol_table);
  IsolatedStoreImpl store(symbol_table, true);

  ScopeSharedPtr scope = store.rootScope()->scopeFromStatName(pool.add("http.ingress"));
  Gauge& gauge = scope->gaugeFromStatNameWithTags(pool.add("rq_active"), absl::nullopt,
                                                  Gauge::ImportMode::Accumulate);
  EXPECT_EQ("http.ingress.rq_active", gauge.name());
  EXPECT_EQ("http.rq_active", gauge.tagExtractedName());
  EXPECT_THAT(gauge.tags(), testing::ElementsAre(Tag{"envoy.http_conn_manager_prefix", "ingress"}));
}

// Legacy createScope with a prefix that has no well-known mapping should fall
// through to a single opaque path element with no tag extraction.
TEST(StatsIsolatedStoreElementScopeTest, LegacyCreateScopeStringUnknownPrefix) {
  SymbolTableImpl symbol_table;
  StatNamePool pool(symbol_table);
  IsolatedStoreImpl store(symbol_table, true);

  ScopeSharedPtr scope = store.rootScope()->createScope("custom.subsystem.");
  Counter& counter = scope->counterFromStatNameWithTags(pool.add("events"), absl::nullopt);
  EXPECT_EQ("custom.subsystem.events", counter.name());
  EXPECT_EQ("custom.subsystem.events", counter.tagExtractedName());
  EXPECT_EQ(0u, counter.tags().size());
}

// Legacy *FromStatNameWithTags variants on element-backed scope translate
// caller-supplied tags into element form.
TEST(StatsIsolatedStoreElementScopeTest, LegacyFromStatNameWithTagsAttachesTags) {
  SymbolTableImpl symbol_table;
  StatNamePool pool(symbol_table);
  IsolatedStoreImpl store(symbol_table, true);

  ScopeSharedPtr scope = store.rootScope()->createScope("cluster.svc.");
  StatNameTagVector extra_tags{{pool.add("envoy.zone"), pool.add("us-east-1a")}};

  Counter& counter = scope->counterFromStatNameWithTags(pool.add("upstream_rq_total"), extra_tags);
  EXPECT_EQ("cluster.svc.upstream_rq_total.envoy.zone.us-east-1a", counter.name());
  EXPECT_EQ("cluster.upstream_rq_total", counter.tagExtractedName());
  EXPECT_THAT(counter.tags(), testing::UnorderedElementsAre(Tag{"envoy.cluster_name", "svc"},
                                                            Tag{"envoy.zone", "us-east-1a"}));

  Gauge& gauge = scope->gaugeFromStatNameWithTags(pool.add("upstream_rq_active"), extra_tags,
                                                  Gauge::ImportMode::Accumulate);
  EXPECT_EQ("cluster.upstream_rq_active", gauge.tagExtractedName());
  EXPECT_THAT(gauge.tags(), testing::UnorderedElementsAre(Tag{"envoy.cluster_name", "svc"},
                                                          Tag{"envoy.zone", "us-east-1a"}));

  Histogram& histogram = scope->histogramFromStatNameWithTags(
      pool.add("upstream_rq_time"), extra_tags, Histogram::Unit::Milliseconds);
  EXPECT_EQ("cluster.upstream_rq_time", histogram.tagExtractedName());
  EXPECT_THAT(histogram.tags(), testing::UnorderedElementsAre(Tag{"envoy.cluster_name", "svc"},
                                                              Tag{"envoy.zone", "us-east-1a"}));

  TextReadout& text_readout =
      scope->textReadoutFromStatNameWithTags(pool.add("control_plane.identifier"), extra_tags);
  EXPECT_EQ("cluster.control_plane.identifier", text_readout.tagExtractedName());
  EXPECT_THAT(text_readout.tags(), testing::UnorderedElementsAre(Tag{"envoy.cluster_name", "svc"},
                                                                 Tag{"envoy.zone", "us-east-1a"}));
}

// Nested element scopes must accumulate the prefix and tags from each level.
TEST(StatsIsolatedStoreElementScopeTest, NestedScopesAccumulatePrefix) {
  SymbolTableImpl symbol_table;
  StatNamePool pool(symbol_table);
  IsolatedStoreImpl store(symbol_table, true);

  ScopeSharedPtr l1 = store.rootScope()->createScope(StatElementViewVec{
      {.value = "cluster"}, {.value = "svc", .name = "cluster_name", .ignore_name = true}});
  ScopeSharedPtr l2 = l1->createScope(StatElementViewVec{{.value = "upstream"}});
  Counter& counter = l2->getOrCreateCounter(StatElementVec{{.value = pool.add("rq_total")}});
  EXPECT_EQ("cluster.svc.upstream.rq_total", counter.name());
  EXPECT_EQ("cluster.upstream.rq_total", counter.tagExtractedName());
  EXPECT_THAT(counter.tags(), testing::ElementsAre(Tag{"cluster_name", "svc"}));
}

TEST_F(StatsIsolatedStoreImplTest, HistogramWithTag) {
  StatNameTagVector tags{{makeStatName("tag1"), makeStatName("tag1Value")}};
  StatNameTagVector tags2{{makeStatName("tag1"), makeStatName("tag1Value2")}};
  StatName base = makeStatName("histogram");
  Histogram& h1 =
      scope_->histogramFromStatNameWithTags(base, tags, Stats::Histogram::Unit::Unspecified);
  Histogram& h2 =
      scope_->histogramFromStatNameWithTags(base, tags2, Stats::Histogram::Unit::Unspecified);
  EXPECT_EQ("histogram.tag1.tag1Value", h1.name());
  EXPECT_EQ("histogram", h1.tagExtractedName());
  EXPECT_THAT(h1.tags(), testing::ElementsAre(Tag{"tag1", "tag1Value"}));
  EXPECT_EQ("histogram.tag1.tag1Value2", h2.name());
  EXPECT_EQ("histogram", h2.tagExtractedName());
  EXPECT_THAT(h2.tags(), testing::ElementsAre(Tag{"tag1", "tag1Value2"}));
  // Verify that histogramFromStatNameWithTags with the same params returns
  // the existing stat object.
  EXPECT_EQ(
      &h1, &scope_->histogramFromStatNameWithTags(base, tags, Stats::Histogram::Unit::Unspecified));
}

TEST_F(StatsIsolatedStoreImplTest, ConstSymtabAccessor) {
  ScopeSharedPtr scope = store_->createScope("scope.");
  const Scope& cscope = *scope;
  const SymbolTable& const_symbol_table = cscope.constSymbolTable();
  SymbolTable& symbol_table = scope->symbolTable();
  EXPECT_EQ(&const_symbol_table, &symbol_table);
}

TEST_F(StatsIsolatedStoreImplTest, LongStatName) {
  const std::string long_string(128, 'A');

  ScopeSharedPtr scope = store_->createScope("scope.");
  Counter& counter = scope->counterFromString(long_string);
  EXPECT_EQ(absl::StrCat("scope.", long_string), counter.name());
}

/**
 * Test stats macros. @see stats_macros.h
 */
#define ALL_TEST_STATS(COUNTER, GAUGE, HISTOGRAM, TEXT_READOUT, STATNAME)                          \
  COUNTER(test_counter)                                                                            \
  GAUGE(test_gauge, Accumulate)                                                                    \
  HISTOGRAM(test_histogram, Microseconds)                                                          \
  TEXT_READOUT(test_text_readout)                                                                  \
  STATNAME(prefix)

struct TestStats {
  ALL_TEST_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT, GENERATE_HISTOGRAM_STRUCT,
                 GENERATE_TEXT_READOUT_STRUCT, GENERATE_STATNAME_STRUCT)
};

TEST_F(StatsIsolatedStoreImplTest, StatsMacros) {
  TestStats test_stats{
      ALL_TEST_STATS(POOL_COUNTER_PREFIX(*store_, "test."), POOL_GAUGE_PREFIX(*store_, "test."),
                     POOL_HISTOGRAM_PREFIX(*store_, "test."),
                     POOL_TEXT_READOUT_PREFIX(*store_, "test."), GENERATE_STATNAME_STRUCT)};

  Counter& counter = test_stats.test_counter_;
  EXPECT_EQ("test.test_counter", counter.name());

  Gauge& gauge = test_stats.test_gauge_;
  EXPECT_EQ("test.test_gauge", gauge.name());

  TextReadout& textReadout = test_stats.test_text_readout_;
  EXPECT_EQ("test.test_text_readout", textReadout.name());

  Histogram& histogram = test_stats.test_histogram_;
  EXPECT_EQ("test.test_histogram", histogram.name());
  EXPECT_EQ(Histogram::Unit::Microseconds, histogram.unit());
}

TEST_F(StatsIsolatedStoreImplTest, NullImplCoverage) {
  NullCounterImpl& c = store_->nullCounter();
  c.inc();
  EXPECT_EQ(0, c.value());
  NullGaugeImpl& g = store_->nullGauge();
  g.inc();
  EXPECT_EQ(0, g.value());
}

TEST_F(StatsIsolatedStoreImplTest, StatNamesStruct) {
  MAKE_STAT_NAMES_STRUCT(StatNames, ALL_TEST_STATS);
  StatNames stat_names(store_->symbolTable());
  EXPECT_EQ("prefix", store_->symbolTable().toString(stat_names.prefix_));
  ScopeSharedPtr scope1 = store_->createScope("scope1.");
  ScopeSharedPtr scope2 = store_->createScope("scope2.");
  MAKE_STATS_STRUCT(Stats, StatNames, ALL_TEST_STATS);
  Stats stats1(stat_names, *scope1);
  EXPECT_EQ("scope1.test_counter", stats1.test_counter_.name());
  EXPECT_EQ("scope1.test_gauge", stats1.test_gauge_.name());
  EXPECT_EQ("scope1.test_histogram", stats1.test_histogram_.name());
  EXPECT_EQ("scope1.test_text_readout", stats1.test_text_readout_.name());
  Stats stats2(stat_names, *scope2, stat_names.prefix_);
  EXPECT_EQ("scope2.prefix.test_counter", stats2.test_counter_.name());
  EXPECT_EQ("scope2.prefix.test_gauge", stats2.test_gauge_.name());
  EXPECT_EQ("scope2.prefix.test_histogram", stats2.test_histogram_.name());
  EXPECT_EQ("scope2.prefix.test_text_readout", stats2.test_text_readout_.name());
}

TEST_F(StatsIsolatedStoreImplTest, SharedScopes) {
  std::vector<ConstScopeSharedPtr> scopes;

  // Verifies shared_ptr functionality by creating some scopes, iterating
  // through them from the store and saving them in a vector, dropping the
  // references, and then referencing the scopes, verifying their names.
  {
    ScopeSharedPtr scope1 = store_->createScope("scope1.");
    ScopeSharedPtr scope2 = store_->createScope("scope2.");
    store_->forEachScope(
        [](size_t) {}, [&scopes](const Scope& scope) { scopes.push_back(scope.getConstShared()); });
  }
  ASSERT_EQ(3, scopes.size());
  store_->symbolTable().sortByStatNames<ConstScopeSharedPtr>(
      scopes.begin(), scopes.end(),
      [](const ConstScopeSharedPtr& scope) -> StatName { return scope->prefix(); });
  EXPECT_EQ("", store_->symbolTable().toString(scopes[0]->prefix())); // default scope
  EXPECT_EQ("scope1", store_->symbolTable().toString(scopes[1]->prefix()));
  EXPECT_EQ("scope2", store_->symbolTable().toString(scopes[2]->prefix()));
}

class IsolatedStoreScopeMatcherTest : public testing::Test {
protected:
  IsolatedStoreScopeMatcherTest()
      : store_(std::make_unique<IsolatedStoreImpl>(symbol_table_)), pool_(symbol_table_),
        scope_(store_->rootScope()) {}
  ~IsolatedStoreScopeMatcherTest() override {
    pool_.clear();
    scope_.reset();
    store_.reset();
    EXPECT_EQ(0, symbol_table_.numSymbols());
  }

  // Builds a matcher that rejects stats whose full name starts with the given prefix.
  StatsMatcherSharedPtr makePrefixMatcher(absl::string_view prefix) {
    envoy::config::metrics::v3::StatsConfig cfg;
    cfg.mutable_stats_matcher()->mutable_exclusion_list()->add_patterns()->set_prefix(
        std::string(prefix));
    return std::make_shared<StatsMatcherImpl>(cfg, symbol_table_, context_);
  }

  SymbolTableImpl symbol_table_;
  NiceMock<Server::Configuration::MockServerFactoryContext> context_;
  std::unique_ptr<IsolatedStoreImpl> store_;
  StatNamePool pool_;
  ScopeSharedPtr scope_;
};

// Tests that a scope-level matcher rejects the appropriate stat types and accepts others.
TEST_F(IsolatedStoreScopeMatcherTest, ScopeMatcherRejectsAllStatTypes) {
  // Create a scope whose matcher rejects everything prefixed "scope.rejected.".
  ScopeSharedPtr my_scope =
      scope_->createScope("scope", false, {}, makePrefixMatcher("scope.rejected."));

  // Rejected counter returns null counter (empty name, value always 0).
  Counter& rejected_counter = my_scope->counterFromString("rejected.foo");
  EXPECT_EQ("", rejected_counter.name());
  rejected_counter.inc();
  EXPECT_EQ(0, rejected_counter.value());

  // Accepted counter is real.
  Counter& accepted_counter = my_scope->counterFromString("accepted.foo");
  EXPECT_EQ("scope.accepted.foo", accepted_counter.name());
  accepted_counter.inc();
  EXPECT_EQ(1, accepted_counter.value());

  // Rejected gauge returns null gauge.
  Gauge& rejected_gauge = my_scope->gaugeFromString("rejected.g", Gauge::ImportMode::Accumulate);
  EXPECT_EQ("", rejected_gauge.name());
  rejected_gauge.set(42);
  EXPECT_EQ(0, rejected_gauge.value());

  // Accepted gauge is real.
  Gauge& accepted_gauge = my_scope->gaugeFromString("accepted.g", Gauge::ImportMode::Accumulate);
  EXPECT_EQ("scope.accepted.g", accepted_gauge.name());

  // Rejected histogram returns null histogram (Unit::Null, used() == false).
  Histogram& rejected_histogram =
      my_scope->histogramFromString("rejected.h", Histogram::Unit::Unspecified);
  EXPECT_EQ(Histogram::Unit::Null, rejected_histogram.unit());
  EXPECT_FALSE(rejected_histogram.used());

  // Accepted histogram is real.
  Histogram& accepted_histogram =
      my_scope->histogramFromString("accepted.h", Histogram::Unit::Unspecified);
  EXPECT_EQ(Histogram::Unit::Unspecified, accepted_histogram.unit());

  // Rejected text readout returns null text readout (empty name, value always "").
  TextReadout& rejected_tr = my_scope->textReadoutFromString("rejected.tr");
  EXPECT_EQ("", rejected_tr.name());
  rejected_tr.set("hello");
  EXPECT_EQ("", rejected_tr.value());

  // Accepted text readout is real.
  TextReadout& accepted_tr = my_scope->textReadoutFromString("accepted.tr");
  EXPECT_EQ("scope.accepted.tr", accepted_tr.name());
}

// Tests that a scope without a matcher accepts all stats (no filtering).
TEST_F(IsolatedStoreScopeMatcherTest, ScopeWithNoMatcherAcceptsAll) {
  ScopeSharedPtr my_scope = scope_->createScope("scope");

  Counter& c = my_scope->counterFromString("foo");
  EXPECT_EQ("scope.foo", c.name());

  Gauge& g = my_scope->gaugeFromString("bar", Gauge::ImportMode::Accumulate);
  EXPECT_EQ("scope.bar", g.name());

  Histogram& h = my_scope->histogramFromString("baz", Histogram::Unit::Unspecified);
  EXPECT_EQ("scope.baz", h.name());

  TextReadout& tr = my_scope->textReadoutFromString("qux");
  EXPECT_EQ("scope.qux", tr.name());
}

// Tests that child scopes inherit the parent scope's matcher.
TEST_F(IsolatedStoreScopeMatcherTest, ChildScopeInheritsMatcher) {
  // Parent matcher rejects full names starting with "parent.child.".
  ScopeSharedPtr parent_scope =
      scope_->createScope("parent", false, {}, makePrefixMatcher("parent.child."));

  // Stats directly in parent are not rejected.
  Counter& parent_counter = parent_scope->counterFromString("direct");
  EXPECT_EQ("parent.direct", parent_counter.name());

  // Child created without an explicit matcher inherits parent's matcher.
  ScopeSharedPtr child_scope = parent_scope->createScope("child");

  // Stats in child (full name "parent.child.*") are rejected.
  Counter& child_rejected = child_scope->counterFromString("foo");
  EXPECT_EQ("", child_rejected.name());

  Gauge& child_gauge_rejected = child_scope->gaugeFromString("bar", Gauge::ImportMode::Accumulate);
  EXPECT_EQ("", child_gauge_rejected.name());

  Histogram& child_histogram_rejected =
      child_scope->histogramFromString("baz", Histogram::Unit::Unspecified);
  EXPECT_EQ(Histogram::Unit::Null, child_histogram_rejected.unit());

  TextReadout& child_tr_rejected = child_scope->textReadoutFromString("qux");
  EXPECT_EQ("", child_tr_rejected.name());
}

// useElementScope() reflects the constructor flag and gates rootScope's type.
TEST(StatsIsolatedStoreElementScopeTest, UseElementScopeAccessor) {
  SymbolTableImpl symbol_table;
  IsolatedStoreImpl element_store(symbol_table, /*use_element_scope=*/true);
  EXPECT_TRUE(element_store.useElementScope());

  IsolatedStoreImpl legacy_store(symbol_table, /*use_element_scope=*/false);
  EXPECT_FALSE(legacy_store.useElementScope());

  // Default ctor (the single-arg form) defaults to legacy.
  IsolatedStoreImpl default_store(symbol_table);
  EXPECT_FALSE(default_store.useElementScope());
}

// On a legacy (non-element) scope:
//   - getOrCreate* uses the (StatName, StatElementSpan, SymbolTable) joiner.
//     `ignore_name=true` elements are treated as PLAIN path tokens (no
//     explicit tag) so the legacy TagProducer pipeline still gets a chance to
//     regex-extract them. `ignore_name=false` elements ARE honored as
//     explicit tags.
//   - createScope drops tag metadata entirely, because scopes can only store
//     a flat StatName prefix.
TEST(StatsIsolatedStoreElementScopeTest, LegacyScopeJoinsElementValues) {
  SymbolTableImpl symbol_table;
  StatNamePool pool(symbol_table);
  IsolatedStoreImpl store(symbol_table, /*use_element_scope=*/false);
  ScopeSharedPtr scope = store.rootScope();

  // Multiple `value`-only elements are joined with '.'.
  const StatElementVec elements{{.value = pool.add("foo")}, {.value = pool.add("bar")}};
  EXPECT_EQ("foo.bar", scope->getOrCreateCounter(elements).name());
  EXPECT_EQ("foo.bar", scope->getOrCreateGauge(elements, Gauge::ImportMode::Accumulate).name());
  EXPECT_EQ("foo.bar", scope->getOrCreateHistogram(elements, Histogram::Unit::Unspecified).name());
  EXPECT_EQ("foo.bar", scope->getOrCreateTextReadout(elements).name());

  // ignore_name=true element: treated as a plain path token on a legacy
  // scope. effective_tags_ is NOT populated; downstream regex extraction is
  // expected to recognize the tag (the IsolatedStore here has no TagProducer
  // configured, so the resulting counter simply has no tags).
  const StatElementVec well_known_tag_elements{
      {.value = pool.add("foo")},
      {.value = pool.add("v1"), .name = pool.add("version"), .ignore_name = true},
  };
  Counter& well_known_counter = scope->getOrCreateCounter(well_known_tag_elements);
  EXPECT_EQ("foo.v1", well_known_counter.name());
  EXPECT_EQ("foo.v1", well_known_counter.tagExtractedName());
  EXPECT_TRUE(well_known_counter.tags().empty());

  // ignore_name=false element: explicit caller-declared tag. The joiner
  // records it in effective_tags_; downstream code uses it verbatim and
  // skips regex extraction.
  const StatElementVec explicit_tag_elements{
      {.value = pool.add("rq_total")},
      {.value = pool.add("us-east-1a"), .name = pool.add("envoy.zone")},
  };
  Counter& explicit_tag_counter = scope->getOrCreateCounter(explicit_tag_elements);
  EXPECT_EQ("rq_total.envoy.zone.us-east-1a", explicit_tag_counter.name());
  EXPECT_EQ("rq_total", explicit_tag_counter.tagExtractedName());
  EXPECT_THAT(explicit_tag_counter.tags(), testing::ElementsAre(Tag{"envoy.zone", "us-east-1a"}));

  // createScope still drops tag metadata. The resulting scope's prefix is the
  // joined `value`s only, and child stats see no scope-level tags.
  ScopeSharedPtr stat_name_scope = scope->createScope(StatElementSpan(elements));
  EXPECT_EQ("foo.bar.x", stat_name_scope->counterFromString("x").name());

  StatElementViewVec view_elements{{.value = "foo"}, {.value = "bar"}};
  ScopeSharedPtr view_scope = scope->createScope(StatElementViewSpan(view_elements));
  EXPECT_EQ("foo.bar.y", view_scope->counterFromString("y").name());
}

// A scope-level matcher on an element-backed scope must reject through the
// element-API path (different code from the legacy counterFromStatNameWithTags
// path tested elsewhere).
TEST_F(IsolatedStoreScopeMatcherTest, ElementScopeMatcherRejectsViaElementApi) {
  SymbolTableImpl element_symbol_table;
  IsolatedStoreImpl element_store(element_symbol_table, /*use_element_scope=*/true);
  StatNamePool element_pool(element_symbol_table);

  // Matcher rejects exactly "scope.rejected.foo".
  envoy::config::metrics::v3::StatsConfig cfg;
  cfg.mutable_stats_matcher()->mutable_exclusion_list()->add_patterns()->set_prefix(
      "scope.rejected.");
  StatsMatcherSharedPtr scope_matcher =
      std::make_shared<StatsMatcherImpl>(cfg, element_symbol_table, context_);

  ScopeSharedPtr scope = element_store.rootScope()->createScope(
      StatElementViewVec{{.value = "scope"}}, false, {}, scope_matcher);

  // Rejected via the element API.
  Counter& rejected =
      scope->getOrCreateCounter(StatElementVec{{.value = element_pool.add("rejected.foo")}});
  EXPECT_EQ("", rejected.name());

  // Accepted via the element API.
  Counter& accepted =
      scope->getOrCreateCounter(StatElementVec{{.value = element_pool.add("accepted.foo")}});
  EXPECT_EQ("scope.accepted.foo", accepted.name());

  // The Gauge path returns the null gauge via an explicit `if (!gauge.has_value())`
  // branch (distinct from Counter's `.value_or` path).
  Gauge& rejected_gauge = scope->getOrCreateGauge(
      StatElementVec{{.value = element_pool.add("rejected.foo")}}, Gauge::ImportMode::Accumulate);
  EXPECT_EQ("", rejected_gauge.name());
  Gauge& accepted_gauge = scope->getOrCreateGauge(
      StatElementVec{{.value = element_pool.add("accepted.foo")}}, Gauge::ImportMode::Accumulate);
  EXPECT_EQ("scope.accepted.foo", accepted_gauge.name());

  // Histogram and TextReadout rejection paths use `.value_or(null_*)` and
  // round out the per-stat-type coverage on the element API.
  Histogram& rejected_hist = scope->getOrCreateHistogram(
      StatElementVec{{.value = element_pool.add("rejected.foo")}}, Histogram::Unit::Milliseconds);
  EXPECT_EQ("", rejected_hist.name());
  TextReadout& rejected_tr =
      scope->getOrCreateTextReadout(StatElementVec{{.value = element_pool.add("rejected.foo")}});
  EXPECT_EQ("", rejected_tr.name());
}

// Parallels ChildElementScopeInheritsAndOverridesMatcher from
// thread_local_store_test.cc: a child element scope inherits its parent's
// matcher when no explicit matcher is supplied, and an explicit child matcher
// overrides the inherited one.
TEST(StatsIsolatedStoreElementScopeTest, ElementScopeMatcherInheritsAndOverrides) {
  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  SymbolTableImpl symbol_table;
  StatNamePool pool(symbol_table);
  IsolatedStoreImpl store(symbol_table, /*use_element_scope=*/true);

  auto make_prefix_matcher = [&](absl::string_view prefix) -> StatsMatcherSharedPtr {
    envoy::config::metrics::v3::StatsConfig cfg;
    cfg.mutable_stats_matcher()->mutable_exclusion_list()->add_patterns()->set_prefix(
        std::string(prefix));
    return std::make_shared<StatsMatcherImpl>(cfg, symbol_table, context);
  };

  // Parent matcher rejects names beginning with "parent.child.rejected_by_parent."
  ScopeSharedPtr parent =
      store.rootScope()->createScope(StatElementViewVec{{.value = "parent"}}, false, {},
                                     make_prefix_matcher("parent.child.rejected_by_parent."));

  // Inheritance: child without an explicit matcher uses parent's matcher.
  ScopeSharedPtr child = parent->createScope(StatElementViewVec{{.value = "child"}});
  Counter& inherited_rejected =
      child->getOrCreateCounter(StatElementVec{{.value = pool.add("rejected_by_parent.x")}});
  EXPECT_EQ("", inherited_rejected.name());
  Counter& inherited_accepted =
      child->getOrCreateCounter(StatElementVec{{.value = pool.add("ok.x")}});
  EXPECT_EQ("parent.child.ok.x", inherited_accepted.name());

  // Override: an explicit matcher on the child replaces the parent's matcher.
  ScopeSharedPtr override_child =
      parent->createScope(StatElementViewVec{{.value = "child2"}}, false, {},
                          make_prefix_matcher("parent.child2.rejected_by_child."));
  // Parent's rule no longer applies on this branch.
  Counter& parent_rule_inactive = override_child->getOrCreateCounter(
      StatElementVec{{.value = pool.add("rejected_by_parent.x")}});
  EXPECT_EQ("parent.child2.rejected_by_parent.x", parent_rule_inactive.name());
  // Child's own rule does apply.
  Counter& child_rule_active = override_child->getOrCreateCounter(
      StatElementVec{{.value = pool.add("rejected_by_child.x")}});
  EXPECT_EQ("", child_rule_active.name());
}

// Tests that an explicit matcher on a child scope overrides the inherited parent matcher.
TEST_F(IsolatedStoreScopeMatcherTest, ChildScopeOverridesMatcher) {
  // Parent rejects "parent.child.rejected_by_parent.".
  ScopeSharedPtr parent_scope = scope_->createScope(
      "parent", false, {}, makePrefixMatcher("parent.child.rejected_by_parent."));

  // Child gets its own matcher that rejects "parent.child.rejected_by_child." instead.
  ScopeSharedPtr child_scope = parent_scope->createScope(
      "child", false, {}, makePrefixMatcher("parent.child.rejected_by_child."));

  // "rejected_by_parent" prefix is NOT rejected — child's matcher replaced parent's.
  Counter& not_rejected = child_scope->counterFromString("rejected_by_parent.foo");
  EXPECT_EQ("parent.child.rejected_by_parent.foo", not_rejected.name());

  // "rejected_by_child" prefix IS rejected by the child's own matcher.
  Counter& rejected = child_scope->counterFromString("rejected_by_child.foo");
  EXPECT_EQ("", rejected.name());
}

} // namespace Stats
} // namespace Envoy
