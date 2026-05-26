#include "source/common/config/well_known_names.h"
#include "source/common/stats/symbol_table.h"
#include "source/common/stats/tag_utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Stats {
namespace TagUtility {

namespace {

class TagUtilityTest : public ::testing::Test {
protected:
  SymbolTable symbol_table_;
  StatNamePool symbolic_pool_{symbol_table_};
  StatNameDynamicPool dynamic_pool_{symbol_table_};
};

TEST_F(TagUtilityTest, Symbolic) {
  StatNameTagVector tags;
  tags.push_back(StatNameTag(symbolic_pool_.add("tag_name"), symbolic_pool_.add("tag_value")));
  TagStatNameJoiner joiner(symbolic_pool_.add("prefix"), symbolic_pool_.add("name"), tags,
                           symbol_table_);
  EXPECT_EQ("prefix.name.tag_name.tag_value", symbol_table_.toString(joiner.nameWithTags()));
  EXPECT_EQ("prefix.name", symbol_table_.toString(joiner.tagExtractedName()));
}

TEST_F(TagUtilityTest, Dynamic) {
  StatNameTagVector tags;
  tags.push_back(StatNameTag(dynamic_pool_.add("tag_name"), dynamic_pool_.add("tag_value")));
  TagStatNameJoiner joiner(dynamic_pool_.add("prefix"), dynamic_pool_.add("name"), tags,
                           symbol_table_);
  EXPECT_EQ("prefix.name.tag_name.tag_value", symbol_table_.toString(joiner.nameWithTags()));
  EXPECT_EQ("prefix.name", symbol_table_.toString(joiner.tagExtractedName()));
}

TEST_F(TagUtilityTest, JoinerFromElementsWithoutTags) {
  StatElementVec prefix{{.value = symbolic_pool_.add("foo")}};
  StatElementVec names{{.value = symbolic_pool_.add("bar")}};
  TagStatNameJoiner joiner(prefix, names, symbol_table_);
  EXPECT_EQ("foo.bar", symbol_table_.toString(joiner.nameWithTags()));
  EXPECT_EQ("foo.bar", symbol_table_.toString(joiner.tagExtractedName()));
  EXPECT_FALSE(joiner.effectiveTags().has_value());
}

TEST_F(TagUtilityTest, JoinerFromElementsWithTaggedElements) {
  // Prefix element contributes both a tag and a path token (name + value
  // emitted, since ignore_name=false).
  StatElementVec prefix{
      {.value = symbolic_pool_.add("ingress"), .name = symbolic_pool_.add("listener")}};
  // Stat element contributes a path token only (no tag).
  StatElementVec names{{.value = symbolic_pool_.add("rq_total")}};
  TagStatNameJoiner joiner(prefix, names, symbol_table_);
  EXPECT_EQ("listener.ingress.rq_total", symbol_table_.toString(joiner.nameWithTags()));
  EXPECT_EQ("rq_total", symbol_table_.toString(joiner.tagExtractedName()));
  ASSERT_TRUE(joiner.effectiveTags().has_value());
  ASSERT_EQ(1u, joiner.effectiveTags()->size());
  EXPECT_EQ("listener", symbol_table_.toString((*joiner.effectiveTags())[0].first));
  EXPECT_EQ("ingress", symbol_table_.toString((*joiner.effectiveTags())[0].second));
}

TEST_F(TagUtilityTest, JoinerFromElementsWithIgnoreName) {
  // ignore_name=true drops the tag name from the full stat name but keeps it
  // as a tag entry. Mirrors how legacy stats render: "cluster.svc.rq_total".
  StatElementVec prefix{{.value = symbolic_pool_.add("cluster")},
                        {.value = symbolic_pool_.add("svc"),
                         .name = symbolic_pool_.add("envoy.cluster_name"),
                         .ignore_name = true}};
  StatElementVec names{{.value = symbolic_pool_.add("rq_total")}};
  TagStatNameJoiner joiner(prefix, names, symbol_table_);
  EXPECT_EQ("cluster.svc.rq_total", symbol_table_.toString(joiner.nameWithTags()));
  EXPECT_EQ("cluster.rq_total", symbol_table_.toString(joiner.tagExtractedName()));
  ASSERT_TRUE(joiner.effectiveTags().has_value());
  ASSERT_EQ(1u, joiner.effectiveTags()->size());
  EXPECT_EQ("envoy.cluster_name", symbol_table_.toString((*joiner.effectiveTags())[0].first));
  EXPECT_EQ("svc", symbol_table_.toString((*joiner.effectiveTags())[0].second));
}

// populateWellKnownLegacyStatPrefix tests --------------------------------------

class PopulateWellKnownLegacyStatPrefixTest : public ::testing::Test {};

TEST_F(PopulateWellKnownLegacyStatPrefixTest, KnownPrefixWithStatName) {
  StatElementViewVec out;
  populateWellKnownLegacyStatPrefix("cluster.svc.rq_total", out);
  ASSERT_EQ(2u, out.size());
  EXPECT_EQ("cluster", out[0].value);
  EXPECT_TRUE(out[0].name.empty());
  EXPECT_FALSE(out[0].ignore_name);
  EXPECT_EQ("svc.rq_total", out[1].value);
  EXPECT_EQ(Config::TagNames::get().CLUSTER_NAME, out[1].name);
  EXPECT_TRUE(out[1].ignore_name);
}

TEST_F(PopulateWellKnownLegacyStatPrefixTest, KnownPrefixStripsTrailingDot) {
  StatElementViewVec out;
  populateWellKnownLegacyStatPrefix("cluster.svc.", out);
  ASSERT_EQ(2u, out.size());
  EXPECT_EQ("cluster", out[0].value);
  EXPECT_EQ("svc", out[1].value);
  EXPECT_EQ(Config::TagNames::get().CLUSTER_NAME, out[1].name);
  EXPECT_TRUE(out[1].ignore_name);
}

TEST_F(PopulateWellKnownLegacyStatPrefixTest, KnownPrefixWithoutRestEmitsOnlyToken) {
  // After stripping trailing dot, "cluster" has no inner dot, so we fall
  // through to the single-token path.
  StatElementViewVec out;
  populateWellKnownLegacyStatPrefix("cluster.", out);
  ASSERT_EQ(1u, out.size());
  EXPECT_EQ("cluster", out[0].value);
  EXPECT_TRUE(out[0].name.empty());
  EXPECT_FALSE(out[0].ignore_name);
}

TEST_F(PopulateWellKnownLegacyStatPrefixTest, UnknownPrefixPassThrough) {
  StatElementViewVec out;
  populateWellKnownLegacyStatPrefix("nonexistent.foo", out);
  ASSERT_EQ(1u, out.size());
  EXPECT_EQ("nonexistent.foo", out[0].value);
  EXPECT_TRUE(out[0].name.empty());
}

TEST_F(PopulateWellKnownLegacyStatPrefixTest, NoDotSingleToken) {
  StatElementViewVec out;
  populateWellKnownLegacyStatPrefix("singleton", out);
  ASSERT_EQ(1u, out.size());
  EXPECT_EQ("singleton", out[0].value);
}

TEST_F(PopulateWellKnownLegacyStatPrefixTest, EmptyInputProducesNothing) {
  StatElementViewVec out;
  populateWellKnownLegacyStatPrefix("", out);
  EXPECT_TRUE(out.empty());
}

TEST_F(PopulateWellKnownLegacyStatPrefixTest, TrailingDotOnlyProducesNothing) {
  StatElementViewVec out;
  populateWellKnownLegacyStatPrefix(".", out);
  EXPECT_TRUE(out.empty());
}

TEST_F(PopulateWellKnownLegacyStatPrefixTest, AppendsToExistingVector) {
  StatElementViewVec out;
  out.emplace_back(StatElementView{.value = "preexisting"});
  populateWellKnownLegacyStatPrefix("cluster.svc", out);
  ASSERT_EQ(3u, out.size());
  EXPECT_EQ("preexisting", out[0].value);
  EXPECT_EQ("cluster", out[1].value);
  EXPECT_EQ("svc", out[2].value);
}

TEST_F(PopulateWellKnownLegacyStatPrefixTest, AllKnownPrefixesMapToExpectedTag) {
  const auto& tags = Config::TagNames::get();
  struct Case {
    absl::string_view prefix;
    absl::string_view expected_tag;
  };
  const std::array<Case, 9> cases{{
      {"http", tags.HTTP_CONN_MANAGER_PREFIX},
      {"cluster", tags.CLUSTER_NAME},
      {"tcp", tags.TCP_PREFIX},
      {"udp", tags.UDP_PREFIX},
      {"vhost", tags.VIRTUAL_HOST},
      {"mongo", tags.MONGO_PREFIX},
      {"redis", tags.REDIS_PREFIX},
      {"thrift", tags.THRIFT_PREFIX},
      {"grpc", tags.GOOGLE_GRPC_CLIENT_PREFIX},
  }};
  for (const Case& c : cases) {
    StatElementViewVec out;
    const std::string input = absl::StrCat(c.prefix, ".value");
    populateWellKnownLegacyStatPrefix(input, out);
    ASSERT_EQ(2u, out.size()) << "input=" << input;
    EXPECT_EQ(c.prefix, out[0].value) << "input=" << input;
    EXPECT_EQ("value", out[1].value) << "input=" << input;
    EXPECT_EQ(c.expected_tag, out[1].name) << "input=" << input;
    EXPECT_TRUE(out[1].ignore_name) << "input=" << input;
  }
}

// mergeTagsToElements ---------------------------------------------------------

TEST_F(TagUtilityTest, MergeTagsToElementsAppendsTags) {
  StatElementVec out;
  out.emplace_back(StatElement{.value = symbolic_pool_.add("base")});
  StatNameTagVector tags{
      {symbolic_pool_.add("envoy.foo"), symbolic_pool_.add("foo_value")},
      {symbolic_pool_.add("envoy.bar"), symbolic_pool_.add("bar_value")},
  };
  StatNameTagVectorOptConstRef tags_ref(tags);
  mergeTagsToElements(out, tags_ref);
  ASSERT_EQ(3u, out.size());
  EXPECT_EQ("base", symbol_table_.toString(out[0].value));
  EXPECT_TRUE(out[0].name.empty());
  EXPECT_EQ("foo_value", symbol_table_.toString(out[1].value));
  EXPECT_EQ("envoy.foo", symbol_table_.toString(out[1].name));
  EXPECT_EQ("bar_value", symbol_table_.toString(out[2].value));
  EXPECT_EQ("envoy.bar", symbol_table_.toString(out[2].name));
}

TEST_F(TagUtilityTest, MergeTagsToElementsAbsentTagsIsNoop) {
  StatElementVec out;
  out.emplace_back(StatElement{.value = symbolic_pool_.add("base")});
  mergeTagsToElements(out, StatNameTagVectorOptConstRef{});
  ASSERT_EQ(1u, out.size());
}

// Element joiner with tags in BOTH the prefix and the stat elements. Verifies
// that effective_tags_ accumulates from both sides in order, and that the
// tag-extracted name skips the tagged entries from both sides.
TEST_F(TagUtilityTest, JoinerFromElementsTagsInPrefixAndStat) {
  StatElementVec prefix{{.value = symbolic_pool_.add("cluster")},
                        {.value = symbolic_pool_.add("svc"),
                         .name = symbolic_pool_.add("envoy.cluster_name"),
                         .ignore_name = true}};
  StatElementVec stat{
      {.value = symbolic_pool_.add("rq_total")},
      {.value = symbolic_pool_.add("us-east-1a"), .name = symbolic_pool_.add("envoy.zone")}};
  TagStatNameJoiner joiner(prefix, stat, symbol_table_);
  EXPECT_EQ("cluster.svc.rq_total.envoy.zone.us-east-1a",
            symbol_table_.toString(joiner.nameWithTags()));
  EXPECT_EQ("cluster.rq_total", symbol_table_.toString(joiner.tagExtractedName()));
  ASSERT_TRUE(joiner.effectiveTags().has_value());
  ASSERT_EQ(2u, joiner.effectiveTags()->size());
  EXPECT_EQ("envoy.cluster_name", symbol_table_.toString((*joiner.effectiveTags())[0].first));
  EXPECT_EQ("svc", symbol_table_.toString((*joiner.effectiveTags())[0].second));
  EXPECT_EQ("envoy.zone", symbol_table_.toString((*joiner.effectiveTags())[1].first));
  EXPECT_EQ("us-east-1a", symbol_table_.toString((*joiner.effectiveTags())[1].second));
}

// (StatName prefix, StatElementSpan stat_elements) joiner ---------------------
//
// This constructor is the one legacy scopes use to honor element tag metadata
// on getOrCreate* without going through the value-only join path.

TEST_F(TagUtilityTest, JoinerStatNamePrefixWithUntaggedElements) {
  TagStatNameJoiner joiner(
      symbolic_pool_.add("prefix"),
      StatElementVec{{.value = symbolic_pool_.add("a")}, {.value = symbolic_pool_.add("b")}},
      symbol_table_);
  EXPECT_EQ("prefix.a.b", symbol_table_.toString(joiner.nameWithTags()));
  EXPECT_EQ("prefix.a.b", symbol_table_.toString(joiner.tagExtractedName()));
  EXPECT_FALSE(joiner.effectiveTags().has_value());
}

TEST_F(TagUtilityTest, JoinerStatNamePrefixWithExplicitTag) {
  // ignore_name=false (default): explicit caller-declared tag. The tag name
  // appears in the full path, neither appears in the tag-extracted name, and
  // the (name, value) pair is recorded in effective_tags_ for downstream
  // code to consume verbatim (skipping regex extraction).
  TagStatNameJoiner joiner(symbolic_pool_.add("cluster.svc"),
                           StatElementVec{{.value = symbolic_pool_.add("rq_total")},
                                          {.value = symbolic_pool_.add("us-east-1a"),
                                           .name = symbolic_pool_.add("envoy.zone")}},
                           symbol_table_);
  EXPECT_EQ("cluster.svc.rq_total.envoy.zone.us-east-1a",
            symbol_table_.toString(joiner.nameWithTags()));
  EXPECT_EQ("cluster.svc.rq_total", symbol_table_.toString(joiner.tagExtractedName()));
  ASSERT_TRUE(joiner.effectiveTags().has_value());
  ASSERT_EQ(1u, joiner.effectiveTags()->size());
  EXPECT_EQ("envoy.zone", symbol_table_.toString((*joiner.effectiveTags())[0].first));
  EXPECT_EQ("us-east-1a", symbol_table_.toString((*joiner.effectiveTags())[0].second));
}

TEST_F(TagUtilityTest, JoinerStatNamePrefixIgnoreNameSkipsExplicitTag) {
  // ignore_name=true on the legacy-scope joiner: the element advertises a tag
  // NAME via metadata, but we treat it like a plain path token here — the
  // value contributes to BOTH the full and tag-extracted names, and we do NOT
  // populate effective_tags_. This preserves the legacy regex-extraction
  // pipeline downstream (the TagProducer is expected to recognize the same
  // positional tag via its built-in extractors).
  TagStatNameJoiner joiner(symbolic_pool_.add("cluster"),
                           StatElementVec{{.value = symbolic_pool_.add("svc"),
                                           .name = symbolic_pool_.add("envoy.cluster_name"),
                                           .ignore_name = true},
                                          {.value = symbolic_pool_.add("rq_total")}},
                           symbol_table_);
  EXPECT_EQ("cluster.svc.rq_total", symbol_table_.toString(joiner.nameWithTags()));
  EXPECT_EQ("cluster.svc.rq_total", symbol_table_.toString(joiner.tagExtractedName()));
  EXPECT_FALSE(joiner.effectiveTags().has_value());
}

TEST_F(TagUtilityTest, JoinerStatNamePrefixMixedIgnoreNameAndExplicitTag) {
  // A realistic mix: cluster name is positional (ignore_name=true, regex
  // extracts it downstream); zone is an explicit caller-declared tag
  // (ignore_name=false, honored here). Only the explicit tag lands in
  // effective_tags_; the positional one shows up as a plain token in BOTH
  // names so the TagProducer can still recognize it.
  TagStatNameJoiner joiner(symbolic_pool_.add("cluster"),
                           StatElementVec{{.value = symbolic_pool_.add("svc"),
                                           .name = symbolic_pool_.add("envoy.cluster_name"),
                                           .ignore_name = true},
                                          {.value = symbolic_pool_.add("rq_total")},
                                          {.value = symbolic_pool_.add("us-east-1a"),
                                           .name = symbolic_pool_.add("envoy.zone")}},
                           symbol_table_);
  EXPECT_EQ("cluster.svc.rq_total.envoy.zone.us-east-1a",
            symbol_table_.toString(joiner.nameWithTags()));
  EXPECT_EQ("cluster.svc.rq_total", symbol_table_.toString(joiner.tagExtractedName()));
  ASSERT_TRUE(joiner.effectiveTags().has_value());
  ASSERT_EQ(1u, joiner.effectiveTags()->size());
  EXPECT_EQ("envoy.zone", symbol_table_.toString((*joiner.effectiveTags())[0].first));
  EXPECT_EQ("us-east-1a", symbol_table_.toString((*joiner.effectiveTags())[0].second));
}

TEST_F(TagUtilityTest, JoinerEmptyStatNamePrefixSkipsPrefixSlot) {
  // Empty StatName prefix is skipped — same names as the (empty, elements)
  // case of the StatElementSpan,StatElementSpan ctor.
  TagStatNameJoiner joiner(StatName{}, StatElementVec{{.value = symbolic_pool_.add("solo")}},
                           symbol_table_);
  EXPECT_EQ("solo", symbol_table_.toString(joiner.nameWithTags()));
  EXPECT_EQ("solo", symbol_table_.toString(joiner.tagExtractedName()));
  EXPECT_FALSE(joiner.effectiveTags().has_value());
}

TEST_F(TagUtilityTest, JoinerStatNamePrefixWithEmptyElements) {
  // Empty stat_elements span: result is just the prefix.
  TagStatNameJoiner joiner(symbolic_pool_.add("only_prefix"), StatElementSpan{}, symbol_table_);
  EXPECT_EQ("only_prefix", symbol_table_.toString(joiner.nameWithTags()));
  EXPECT_EQ("only_prefix", symbol_table_.toString(joiner.tagExtractedName()));
  EXPECT_FALSE(joiner.effectiveTags().has_value());
}

// buildScopePrefixStorage --------------------------------------------------

TEST_F(TagUtilityTest, BuildScopePrefixStorageEmpty) {
  StatNameManagedStorage storage = buildScopePrefixStorage({}, symbol_table_);
  EXPECT_EQ("", symbol_table_.toString(storage.statName()));
}

TEST_F(TagUtilityTest, BuildScopePrefixStorageWithTaggedElement) {
  StatElementVec prefix{{.value = symbolic_pool_.add("cluster")},
                        {.value = symbolic_pool_.add("svc"),
                         .name = symbolic_pool_.add("envoy.cluster_name"),
                         .ignore_name = true}};
  StatNameManagedStorage storage = buildScopePrefixStorage(prefix, symbol_table_);
  EXPECT_EQ("cluster.svc", symbol_table_.toString(storage.statName()));
}

TEST_F(TagUtilityTest, BuildScopePrefixStorageUntaggedOnly) {
  StatElementVec prefix{{.value = symbolic_pool_.add("a")}, {.value = symbolic_pool_.add("b")}};
  StatNameManagedStorage storage = buildScopePrefixStorage(prefix, symbol_table_);
  EXPECT_EQ("a.b", symbol_table_.toString(storage.statName()));
}

// populatePoolAndElementVec -------------------------------------------------

// StatElementSpan specialization: prefix names are already interned StatNames,
// but populatePoolAndElementVec still walks them through the pool.
TEST_F(TagUtilityTest, PopulatePoolAndElementVecStatElement) {
  StatNamePool pool(symbol_table_);
  StatElementVec prefix{{.value = symbolic_pool_.add("cluster")},
                        {.value = symbolic_pool_.add("svc"),
                         .name = symbolic_pool_.add("envoy.cluster_name"),
                         .ignore_name = true}};
  StatElementVec names{{.value = symbolic_pool_.add("rq_total")}};
  StatElementVec out;
  populatePoolAndElementVec(prefix, StatElementSpan(names), &pool, out);
  ASSERT_EQ(3u, out.size());
  EXPECT_EQ("cluster", symbol_table_.toString(out[0].value));
  EXPECT_TRUE(out[0].name.empty());
  EXPECT_FALSE(out[0].ignore_name);
  EXPECT_EQ("svc", symbol_table_.toString(out[1].value));
  EXPECT_EQ("envoy.cluster_name", symbol_table_.toString(out[1].name));
  EXPECT_TRUE(out[1].ignore_name);
  EXPECT_EQ("rq_total", symbol_table_.toString(out[2].value));
  EXPECT_TRUE(out[2].name.empty());
}

// StatElementViewSpan specialization: prefix is StatElement (already-interned),
// names are string_view-typed and get interned via the supplied pool.
TEST_F(TagUtilityTest, PopulatePoolAndElementVecStatElementView) {
  StatNamePool pool(symbol_table_);
  StatElementVec prefix{{.value = symbolic_pool_.add("cluster")}};
  StatElementViewVec names{{.value = "svc", .name = "cluster_name", .ignore_name = true},
                           {.value = "rq_total"}};
  StatElementVec out;
  populatePoolAndElementVec(prefix, StatElementViewSpan(names), &pool, out);
  ASSERT_EQ(3u, out.size());
  EXPECT_EQ("cluster", symbol_table_.toString(out[0].value));
  EXPECT_EQ("svc", symbol_table_.toString(out[1].value));
  EXPECT_EQ("cluster_name", symbol_table_.toString(out[1].name));
  EXPECT_TRUE(out[1].ignore_name);
  EXPECT_EQ("rq_total", symbol_table_.toString(out[2].value));
  EXPECT_TRUE(out[2].name.empty());
}

// Empty value/name strings produce empty StatNames (no symbol_table churn).
TEST_F(TagUtilityTest, PopulatePoolAndElementVecEmptyTokensYieldEmptyStatNames) {
  StatNamePool pool(symbol_table_);
  StatElementViewVec names{{.value = "", .name = ""}};
  StatElementVec out;
  populatePoolAndElementVec({}, StatElementViewSpan(names), &pool, out);
  ASSERT_EQ(1u, out.size());
  EXPECT_TRUE(out[0].value.empty());
  EXPECT_TRUE(out[0].name.empty());
}

// Element joiner with empty prefix and stat spans is the root-scope edge case:
// it produces empty tag-extracted / full names and no tags.
TEST_F(TagUtilityTest, JoinerFromElementsEmptySpans) {
  TagStatNameJoiner joiner(StatElementSpan{}, StatElementSpan{}, symbol_table_);
  EXPECT_EQ("", symbol_table_.toString(joiner.nameWithTags()));
  EXPECT_EQ("", symbol_table_.toString(joiner.tagExtractedName()));
  EXPECT_FALSE(joiner.effectiveTags().has_value());
}

// joinElementValues(StatElementSpan, SymbolTable&) -----------------------------
//
// 0 / 1 / N non-empty branches; the 0/1 branches return `storage == nullptr`
// and either an empty StatName (0) or an alias of the caller's StatName (1).

TEST_F(TagUtilityTest, JoinElementValuesStatNameEmptySpan) {
  JoinedElementName joined = joinElementValues(StatElementSpan{}, symbol_table_);
  EXPECT_EQ(nullptr, joined.storage);
  EXPECT_EQ("", symbol_table_.toString(joined.name));
}

TEST_F(TagUtilityTest, JoinElementValuesStatNameAllEmptyValues) {
  // Elements with empty `.value` are skipped; the result behaves like an empty
  // span — no storage, default StatName.
  StatElementVec elements{StatElement{}, StatElement{}};
  JoinedElementName joined = joinElementValues(elements, symbol_table_);
  EXPECT_EQ(nullptr, joined.storage);
  EXPECT_EQ("", symbol_table_.toString(joined.name));
}

TEST_F(TagUtilityTest, JoinElementValuesStatNameSingleValueAliases) {
  // Single non-empty value: the returned name aliases the caller's StatName,
  // so `storage` is null and the alias is byte-equivalent to the source.
  StatName source = symbolic_pool_.add("solo");
  StatElementVec elements{{.value = source}};
  JoinedElementName joined = joinElementValues(elements, symbol_table_);
  EXPECT_EQ(nullptr, joined.storage);
  EXPECT_EQ("solo", symbol_table_.toString(joined.name));
  EXPECT_EQ(source.data(), joined.name.data());
}

TEST_F(TagUtilityTest, JoinElementValuesStatNameSkipsEmpty) {
  // Mixing empty and non-empty values: only the single non-empty value
  // contributes, and the fast-path single-value alias still applies.
  StatElementVec elements{StatElement{}, {.value = symbolic_pool_.add("only")}, StatElement{}};
  JoinedElementName joined = joinElementValues(elements, symbol_table_);
  EXPECT_EQ(nullptr, joined.storage);
  EXPECT_EQ("only", symbol_table_.toString(joined.name));
}

TEST_F(TagUtilityTest, JoinElementValuesStatNameMultiJoined) {
  // 2+ non-empty values go through the symbol table; tag metadata
  // (`name`/`ignore_name`) is dropped — only `.value` contributes.
  StatElementVec elements{
      {.value = symbolic_pool_.add("a")},
      {.value = symbolic_pool_.add("b"), .name = symbolic_pool_.add("tag"), .ignore_name = true},
      {.value = symbolic_pool_.add("c")}};
  JoinedElementName joined = joinElementValues(elements, symbol_table_);
  ASSERT_NE(nullptr, joined.storage);
  EXPECT_EQ("a.b.c", symbol_table_.toString(joined.name));
}

// joinElementValues(StatElementViewSpan) ---------------------------------------
//
// Same 0 / 1 / N branches, but returns a `std::string`.

TEST_F(TagUtilityTest, JoinElementValuesViewEmptySpan) {
  EXPECT_EQ("", joinElementValues(StatElementViewSpan{}));
}

TEST_F(TagUtilityTest, JoinElementValuesViewAllEmptyValues) {
  StatElementViewVec elements{StatElementView{}, StatElementView{}};
  EXPECT_EQ("", joinElementValues(elements));
}

TEST_F(TagUtilityTest, JoinElementValuesViewSingleValue) {
  StatElementViewVec elements{{.value = "solo"}};
  EXPECT_EQ("solo", joinElementValues(elements));
}

TEST_F(TagUtilityTest, JoinElementValuesViewSkipsEmpty) {
  StatElementViewVec elements{StatElementView{}, {.value = "only"}, StatElementView{}};
  EXPECT_EQ("only", joinElementValues(elements));
}

TEST_F(TagUtilityTest, JoinElementValuesViewMultiJoined) {
  // 2+ non-empty values join with '.'; tag metadata is ignored.
  StatElementViewVec elements{
      {.value = "a"},
      {.value = "b", .name = "tag", .ignore_name = true},
      {.value = "c"},
  };
  EXPECT_EQ("a.b.c", joinElementValues(elements));
}

} // namespace
} // namespace TagUtility
} // namespace Stats
} // namespace Envoy
