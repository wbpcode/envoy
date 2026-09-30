#include "contrib/dynamo/filters/http/source/dynamo_stats.h"

#include <format>
#include <memory>
#include <string>

#include "envoy/stats/scope.h"

#include "source/common/config/well_known_names.h"
#include "source/common/stats/prefix_utility.h"
#include "source/common/stats/symbol_table.h"

#include "contrib/dynamo/filters/http/source/dynamo_request_parser.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Dynamo {

DynamoStats::DynamoStats(Stats::Scope& scope, const std::string& prefix)
    : scope_(scope), stat_name_set_(scope.symbolTable().makeSet("Dynamo")),
      // http.[<stat_prefix>.]dynamodb.*
      prefix_(Stats::mergeStatPrefix(scope.symbolTable(), prefix, "dynamodb")),
      batch_failure_unprocessed_keys_(stat_name_set_->add("BatchFailureUnprocessedKeys")),
      capacity_(stat_name_set_->add("capacity")),
      empty_response_body_(stat_name_set_->add("empty_response_body")),
      error_(stat_name_set_->add("error")),
      invalid_req_body_(stat_name_set_->add("invalid_req_body")),
      invalid_resp_body_(stat_name_set_->add("invalid_resp_body")),
      multiple_tables_(stat_name_set_->add("multiple_tables")),
      no_table_(stat_name_set_->add("no_table")),
      operation_missing_(stat_name_set_->add("operation_missing")),
      table_(stat_name_set_->add("table")), table_missing_(stat_name_set_->add("table_missing")),
      upstream_rq_time_(stat_name_set_->add("upstream_rq_time")),
      upstream_rq_total_(stat_name_set_->add("upstream_rq_total")),
      unknown_entity_type_(stat_name_set_->add("unknown_entity_type")),
      unknown_operation_(stat_name_set_->add("unknown_operation")),
      operation_tag_(stat_name_set_->add(Envoy::Config::TagNames::get().DYNAMO_OPERATION)),
      table_tag_(stat_name_set_->add(Envoy::Config::TagNames::get().DYNAMO_TABLE)),
      partition_id_tag_(stat_name_set_->add(Envoy::Config::TagNames::get().DYNAMO_PARTITION_ID)) {
  upstream_rq_total_groups_[0] = stat_name_set_->add("upstream_rq_total_unknown");
  upstream_rq_time_groups_[0] = stat_name_set_->add("upstream_rq_time_unknown");
  for (size_t i = 1; i < DynamoStats::NumGroupEntries; ++i) {
    upstream_rq_total_groups_[i] = stat_name_set_->add(std::format("upstream_rq_total_{}xx", i));
    upstream_rq_time_groups_[i] = stat_name_set_->add(std::format("upstream_rq_time_{}xx", i));
  }
  RequestParser::forEachStatString(
      [this](const std::string& str) { stat_name_set_->rememberBuiltin(str); });
  for (uint32_t status_code : {200, 400, 403, 502}) {
    stat_name_set_->rememberBuiltin(absl::StrCat("upstream_rq_time_", status_code));
    stat_name_set_->rememberBuiltin(absl::StrCat("upstream_rq_total_", status_code));
  }
  stat_name_set_->rememberBuiltins({"operation", "table"});
}

Stats::SymbolTable::StoragePtr DynamoStats::join(Stats::StatName prefix,
                                                 const Stats::StatNameVec& names) {
  Stats::StatNameVec names_with_prefix;
  names_with_prefix.reserve(1 + names.size());
  names_with_prefix.push_back(prefix);
  names_with_prefix.insert(names_with_prefix.end(), names.begin(), names.end());
  return scope_.symbolTable().join(names_with_prefix);
}

Stats::StatNameTagVec DynamoStats::mergeTags(Stats::StatNameTagSpan tags) {
  Stats::StatNameTagVec merged(prefix_.tags().begin(), prefix_.tags().end());
  merged.insert(merged.end(), tags.begin(), tags.end());
  return merged;
}

Stats::Counter& DynamoStats::counter(const Stats::StatNameVec& base_names,
                                     Stats::StatNameTagSpan tags, const Stats::StatNameVec& names) {
  const Stats::SymbolTable::StoragePtr base_name = join(prefix_.baseName(), base_names);
  const Stats::SymbolTable::StoragePtr name = join(prefix_.name(), names);
  const Stats::StatNameTagVec merged_tags = mergeTags(tags);
  return scope_.counterFromTaggedName(Stats::StatName(base_name.get()),
                                      Stats::StatNameTagSpan(merged_tags),
                                      Stats::StatName(name.get()));
}

Stats::Histogram& DynamoStats::histogram(const Stats::StatNameVec& base_names,
                                         Stats::StatNameTagSpan tags,
                                         const Stats::StatNameVec& names,
                                         Stats::Histogram::Unit unit) {
  const Stats::SymbolTable::StoragePtr base_name = join(prefix_.baseName(), base_names);
  const Stats::SymbolTable::StoragePtr name = join(prefix_.name(), names);
  const Stats::StatNameTagVec merged_tags = mergeTags(tags);
  return scope_.histogramFromTaggedName(Stats::StatName(base_name.get()),
                                        Stats::StatNameTagSpan(merged_tags),
                                        Stats::StatName(name.get()), unit);
}

void DynamoStats::incCounter(const Stats::StatNameVec& names) { counter(names, {}, names).inc(); }

void DynamoStats::incEntityCounter(Stats::StatName entity_type, Stats::StatName entity_tag,
                                   Stats::StatName entity, Stats::StatName name) {
  const Stats::StatNameTag tag{entity_tag, entity};
  counter({entity_type, name}, Stats::StatNameTagSpan(&tag, 1), {entity_type, entity, name}).inc();
}

void DynamoStats::recordEntityHistogram(Stats::StatName entity_type, Stats::StatName entity_tag,
                                        Stats::StatName entity, Stats::StatName name,
                                        Stats::Histogram::Unit unit, uint64_t value) {
  const Stats::StatNameTag tag{entity_tag, entity};
  histogram({entity_type, name}, Stats::StatNameTagSpan(&tag, 1), {entity_type, entity, name}, unit)
      .recordValue(value);
}

void DynamoStats::incTableErrorCounter(Stats::StatName table, Stats::StatName name) {
  const Stats::StatNameTag tag{table_tag_, table};
  counter({error_, name}, Stats::StatNameTagSpan(&tag, 1), {error_, table, name}).inc();
}

Stats::Counter& DynamoStats::buildPartitionStatCounter(const std::string& table_name,
                                                       const std::string& operation,
                                                       const std::string& partition_id) {
  // Use the last 7 characters of the partition id.
  const absl::string_view id_last_7 =
      absl::string_view(partition_id).substr(partition_id.size() - 7);
  Stats::StatNameDynamicPool pool(scope_.symbolTable());
  const Stats::StatName table = pool.add(table_name);
  const Stats::StatName operation_name = getBuiltin(operation, unknown_operation_);
  const Stats::StatName partition = pool.add(absl::StrCat("__partition_id=", id_last_7));
  const Stats::StatNameTag tags[] = {{table_tag_, table},
                                     {operation_tag_, operation_name},
                                     {partition_id_tag_, pool.add(id_last_7)}};
  return counter({table_, capacity_}, tags, {table_, table, capacity_, operation_name, partition});
}

size_t DynamoStats::groupIndex(uint64_t status) {
  size_t index = status / 100;
  if (index >= NumGroupEntries) {
    index = 0; // status-code 600 or higher is unknown.
  }
  return index;
}

} // namespace Dynamo
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
