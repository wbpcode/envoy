#include <string>

#include "test/mocks/stats/mocks.h"
#include "test/test_common/utility.h"

#include "contrib/dynamo/filters/http/source/dynamo_stats.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Dynamo {
namespace {

TEST(DynamoStats, PartitionIdStatString) {
  Stats::IsolatedStoreImpl store;
  auto build_partition_string =
      [&store](const std::string& stat_prefix, const std::string& table_name,
               const std::string& operation, const std::string& partition_id) -> std::string {
    DynamoStats stats(*store.rootScope(), stat_prefix);
    Stats::Counter& counter = stats.buildPartitionStatCounter(table_name, operation, partition_id);
    return counter.name();
  };

  {
    std::string stats_prefix = "prefix.";
    std::string table_name = "locations";
    std::string operation = "GetItem";
    std::string partition_id = "6235c781-1d0d-47a3-a4ea-eec04c5883ca";
    std::string partition_stat_string =
        build_partition_string(stats_prefix, table_name, operation, partition_id);
    std::string expected_stat_string =
        "prefix.dynamodb.table.locations.capacity.GetItem.__partition_id=c5883ca";
    EXPECT_EQ(expected_stat_string, partition_stat_string);
  }

  {
    std::string stats_prefix = "http.egress_dynamodb_iad.";
    std::string table_name = "locations-sandbox-partition-test-iad-mytest-really-long-name";
    std::string operation = "GetItem";
    std::string partition_id = "6235c781-1d0d-47a3-a4ea-eec04c5883ca";

    std::string partition_stat_string =
        build_partition_string(stats_prefix, table_name, operation, partition_id);
    std::string expected_stat_string =
        "http.egress_dynamodb_iad.dynamodb.table.locations-sandbox-partition-test-iad-mytest-"
        "really-long-name.capacity.GetItem.__partition_id=c5883ca";
    EXPECT_EQ(expected_stat_string, partition_stat_string);
  }
  {
    std::string stats_prefix = "http.egress_dynamodb_iad.";
    std::string table_name = "locations-sandbox-partition-test-iad-mytest-rea";
    std::string operation = "GetItem";
    std::string partition_id = "6235c781-1d0d-47a3-a4ea-eec04c5883ca";

    std::string partition_stat_string =
        build_partition_string(stats_prefix, table_name, operation, partition_id);
    std::string expected_stat_string = "http.egress_dynamodb_iad.dynamodb.table.locations-sandbox-"
                                       "partition-test-iad-mytest-rea.capacity.GetItem.__partition_"
                                       "id=c5883ca";

    EXPECT_EQ(expected_stat_string, partition_stat_string);
  }
}

// The stats carry explicit tags: the connection manager prefix is extracted from the stats prefix,
// and the operation, table and partition id segments are tags of their own.
TEST(DynamoStats, StatsAreTagged) {
  // The tags of a stat as (name, value) pairs.
  const auto tags_of = [](const Stats::Metric& metric) {
    std::vector<std::pair<std::string, std::string>> tags;
    for (const Stats::Tag& tag : metric.tags()) {
      tags.emplace_back(tag.name_, tag.value_);
    }
    return tags;
  };

  Stats::IsolatedStoreImpl store;
  DynamoStats stats(*store.rootScope(), "http.egress_dynamodb_iad.");
  Stats::StatNameDynamicPool pool(store.symbolTable());

  const Stats::Counter& partition = stats.buildPartitionStatCounter(
      "locations", "GetItem", "6235c781-1d0d-47a3-a4ea-eec04c5883ca");
  EXPECT_EQ("http.egress_dynamodb_iad.dynamodb.table.locations.capacity.GetItem."
            "__partition_id=c5883ca",
            partition.name());
  EXPECT_EQ("http.dynamodb.table.capacity", partition.tagExtractedName());
  EXPECT_THAT(tags_of(partition),
              testing::UnorderedElementsAre(
                  testing::Pair("envoy.http_conn_manager_prefix", "egress_dynamodb_iad"),
                  testing::Pair("envoy.dynamo_table", "locations"),
                  testing::Pair("envoy.dynamo_operation", "GetItem"),
                  testing::Pair("envoy.dynamo_partition_id", "c5883ca")));

  stats.incEntityCounter(stats.getBuiltin("operation", stats.unknown_entity_type_),
                         stats.operation_tag_, pool.add("Query"), stats.upstream_rq_total_);
  const Stats::CounterSharedPtr operation = TestUtility::findCounter(
      store, "http.egress_dynamodb_iad.dynamodb.operation.Query.upstream_rq_total");
  ASSERT_NE(operation, nullptr);
  EXPECT_EQ(1U, operation->value());
  EXPECT_EQ("http.dynamodb.operation.upstream_rq_total", operation->tagExtractedName());
  EXPECT_THAT(tags_of(*operation),
              testing::UnorderedElementsAre(
                  testing::Pair("envoy.http_conn_manager_prefix", "egress_dynamodb_iad"),
                  testing::Pair("envoy.dynamo_operation", "Query")));

  stats.recordEntityHistogram(stats.getBuiltin("table", stats.unknown_entity_type_),
                              stats.table_tag_, pool.add("bar_table"), stats.upstream_rq_time_,
                              Stats::Histogram::Unit::Milliseconds, 5);
  const Stats::ParentHistogramSharedPtr table_time = TestUtility::findHistogram(
      store, "http.egress_dynamodb_iad.dynamodb.table.bar_table.upstream_rq_time");
  ASSERT_NE(table_time, nullptr);
  EXPECT_EQ("http.dynamodb.table.upstream_rq_time", table_time->tagExtractedName());
  EXPECT_THAT(tags_of(*table_time),
              testing::UnorderedElementsAre(
                  testing::Pair("envoy.http_conn_manager_prefix", "egress_dynamodb_iad"),
                  testing::Pair("envoy.dynamo_table", "bar_table")));

  stats.incTableErrorCounter(pool.add("bar_table"), stats.batch_failure_unprocessed_keys_);
  const Stats::CounterSharedPtr table_error = TestUtility::findCounter(
      store, "http.egress_dynamodb_iad.dynamodb.error.bar_table.BatchFailureUnprocessedKeys");
  ASSERT_NE(table_error, nullptr);
  EXPECT_EQ("http.dynamodb.error.BatchFailureUnprocessedKeys", table_error->tagExtractedName());
  EXPECT_THAT(tags_of(*table_error),
              testing::UnorderedElementsAre(
                  testing::Pair("envoy.http_conn_manager_prefix", "egress_dynamodb_iad"),
                  testing::Pair("envoy.dynamo_table", "bar_table")));

  stats.incCounter({stats.operation_missing_});
  const Stats::CounterSharedPtr missing =
      TestUtility::findCounter(store, "http.egress_dynamodb_iad.dynamodb.operation_missing");
  ASSERT_NE(missing, nullptr);
  EXPECT_EQ("http.dynamodb.operation_missing", missing->tagExtractedName());
  EXPECT_THAT(tags_of(*missing), testing::UnorderedElementsAre(testing::Pair(
                                     "envoy.http_conn_manager_prefix", "egress_dynamodb_iad")));

  // Without a recognized parent prefix, the entity segments are the only tags.
  DynamoStats plain_stats(*store.rootScope(), "prefix.");
  const Stats::Counter& plain_partition = plain_stats.buildPartitionStatCounter(
      "locations", "GetItem", "6235c781-1d0d-47a3-a4ea-eec04c5883ca");
  EXPECT_EQ("prefix.dynamodb.table.capacity", plain_partition.tagExtractedName());
  EXPECT_THAT(tags_of(plain_partition),
              testing::UnorderedElementsAre(testing::Pair("envoy.dynamo_table", "locations"),
                                            testing::Pair("envoy.dynamo_operation", "GetItem"),
                                            testing::Pair("envoy.dynamo_partition_id", "c5883ca")));
  plain_stats.incCounter({plain_stats.operation_missing_});
  const Stats::CounterSharedPtr plain_missing =
      TestUtility::findCounter(store, "prefix.dynamodb.operation_missing");
  ASSERT_NE(plain_missing, nullptr);
  EXPECT_EQ("prefix.dynamodb.operation_missing", plain_missing->tagExtractedName());
  EXPECT_TRUE(plain_missing->tags().empty());
}

} // namespace
} // namespace Dynamo
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
