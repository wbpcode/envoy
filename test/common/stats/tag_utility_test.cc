#include "source/common/stats/symbol_table.h"
#include "source/common/stats/tag_utility.h"

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
  tags.push_back(StatNameTag{symbolic_pool_.add("tag_name"), symbolic_pool_.add("tag_value")});
  TagStatNameJoiner joiner(symbolic_pool_.add("prefix"), symbolic_pool_.add("name"), tags,
                           symbol_table_);
  EXPECT_EQ("prefix.name.tag_name.tag_value", symbol_table_.toString(joiner.nameWithTags()));
  EXPECT_EQ("prefix.name", symbol_table_.toString(joiner.tagExtractedName()));
}

TEST_F(TagUtilityTest, Dynamic) {
  StatNameTagVector tags;
  tags.push_back(StatNameTag{dynamic_pool_.add("tag_name"), dynamic_pool_.add("tag_value")});
  TagStatNameJoiner joiner(dynamic_pool_.add("prefix"), dynamic_pool_.add("name"), tags,
                           symbol_table_);
  EXPECT_EQ("prefix.name.tag_name.tag_value", symbol_table_.toString(joiner.nameWithTags()));
  EXPECT_EQ("prefix.name", symbol_table_.toString(joiner.tagExtractedName()));
}

TEST_F(TagUtilityTest, IgnoreTagName) {
  StatNameTagVector tags;
  tags.push_back(
      StatNameTag{symbolic_pool_.add("tag_name"), symbolic_pool_.add("tag_value"), true});
  TagStatNameJoiner joiner(symbolic_pool_.add("prefix"), symbolic_pool_.add("name"), tags,
                           symbol_table_);
  // With ignore_name_=true, only the value is appended to the stat name.
  EXPECT_EQ("prefix.name.tag_value", symbol_table_.toString(joiner.nameWithTags()));
  EXPECT_EQ("prefix.name", symbol_table_.toString(joiner.tagExtractedName()));
}

TEST_F(TagUtilityTest, ScopeTags) {
  StatNameTagVector scope_tags;
  scope_tags.push_back(
      StatNameTag{symbolic_pool_.add("cluster"), symbolic_pool_.add("my_cluster")});
  StatNameTagVector stat_tags;
  stat_tags.push_back(
      StatNameTag{symbolic_pool_.add("response_code"), symbolic_pool_.add("200"), true});

  TagStatNameJoiner joiner(symbolic_pool_.add("prefix"), scope_tags, symbolic_pool_.add("rq_total"),
                           stat_tags, symbol_table_);
  // scope tag name+value, then stat name, then stat tag value only (ignore_name_=true).
  EXPECT_EQ("prefix.cluster.my_cluster.rq_total.200",
            symbol_table_.toString(joiner.nameWithTags()));
  EXPECT_EQ("prefix.rq_total", symbol_table_.toString(joiner.tagExtractedName()));
}

} // namespace
} // namespace TagUtility
} // namespace Stats
} // namespace Envoy
