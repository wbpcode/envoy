#include "source/extensions/filters/common/rbac/utility.h"

#include <string>

#include "source/common/config/well_known_names.h"
#include "source/common/stats/prefix_utility.h"

#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_replace.h"
#include "absl/strings/strip.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace RBAC {

namespace {

// A group prefix of "policy" is never extracted as a tag: the legacy extraction cannot tell it
// apart from the per-policy "policy" literal and keeps it in the name.
bool isGroupTagged(absl::string_view group_tag_value) {
  return !group_tag_value.empty() && group_tag_value != "policy";
}

// The HTTP filter's downstream prefix, "http.<stat_prefix>.".
bool isHttpPrefix(absl::string_view prefix) { return absl::StartsWith(prefix, "http."); }

// The network filter's raw stat prefix: a single segment, matching the single-segment legacy
// extraction rules. A multi-segment prefix is a parent flat prefix of another subsystem (e.g.
// "cluster.<name>." when the HTTP filter runs upstream), whose stats only carry the parent's own
// tag.
bool isNetworkPrefix(absl::string_view prefix) {
  prefix = absl::StripSuffix(prefix, ".");
  return !prefix.empty() && !absl::StrContains(prefix, '.');
}

// Builds the tagged prefix of one group ("" or the rules/shadow rules prefix) of RBAC filter
// stats, or, with per_policy, of the group's per-policy stats ("policy" appended). The flat
// prefix is "<prefix>.rbac.[<group_prefix>.][policy]", and it mirrors the legacy extraction rules
// exactly:
// - HTTP filter ("http.<stat_prefix>." prefix): the connection manager prefix (extracted by
//   mergeStatPrefix()) and the group prefix (RBAC_HTTP_PREFIX) are tags.
// - Network filter (single-segment prefix): the stat prefix (RBAC_PREFIX) and the group prefix
//   (RBAC_RULES_PREFIX) are tags.
// - Any other parent prefix (e.g. "cluster.<name>." for upstream HTTP filters): only the parent
//   prefix's own tag; the group prefix stays in the tag-extracted name.
Stats::TaggedStatName makeTaggedPrefix(const std::string& prefix, const std::string& group_prefix,
                                       Stats::Scope& scope, bool per_policy = false) {
  std::string flat = Envoy::statPrefixJoin(Envoy::statPrefixJoin(prefix, "rbac."), group_prefix);
  if (per_policy) {
    flat = Envoy::statPrefixJoin(flat, "policy.");
  }
  const absl::string_view own_base = per_policy ? "rbac.policy" : "rbac";
  const std::string group_tag_value{absl::StripSuffix(group_prefix, ".")};
  const bool group_tagged = isGroupTagged(group_tag_value);
  if (isHttpPrefix(prefix)) {
    const absl::string_view own_flat = absl::string_view(flat).substr(prefix.size());
    if (!group_tagged) {
      return Stats::mergeStatPrefix(scope.symbolTable(), prefix, own_flat);
    }
    return Stats::mergeStatPrefix(
        scope.symbolTable(), prefix, own_base,
        {Stats::TagStringView{Envoy::Config::TagNames::get().RBAC_HTTP_PREFIX, group_tag_value}},
        own_flat);
  }
  if (isNetworkPrefix(prefix)) {
    const std::string prefix_tag_value{absl::StripSuffix(prefix, ".")};
    // The length of the prefix including its separating dot.
    const size_t prefix_size = prefix.size() + (absl::EndsWith(prefix, ".") ? 0 : 1);
    if (!group_tagged) {
      return {scope.symbolTable(),
              absl::string_view(flat).substr(prefix_size),
              {Stats::TagStringView{Envoy::Config::TagNames::get().RBAC_PREFIX, prefix_tag_value}},
              flat};
    }
    return {
        scope.symbolTable(),
        own_base,
        {Stats::TagStringView{Envoy::Config::TagNames::get().RBAC_PREFIX, prefix_tag_value},
         Stats::TagStringView{Envoy::Config::TagNames::get().RBAC_RULES_PREFIX, group_tag_value}},
        flat};
  }
  if (prefix.empty()) {
    return {scope.symbolTable(), flat, {}, {}};
  }
  return Stats::mergeStatPrefix(scope.symbolTable(), prefix,
                                absl::string_view(flat).substr(prefix.size()));
}

} // namespace

RoleBasedAccessControlFilterStats generateStats(const std::string& prefix,
                                                const std::string& rules_prefix,
                                                const std::string& shadow_rules_prefix,
                                                Stats::Scope& scope) {
  const Stats::TaggedStatName rules_tagged_prefix = makeTaggedPrefix(prefix, rules_prefix, scope);
  const Stats::TaggedStatName shadow_rules_tagged_prefix =
      makeTaggedPrefix(prefix, shadow_rules_prefix, scope);
  Stats::LiteScopeHelper rules_helper(scope, rules_tagged_prefix.baseName(),
                                      rules_tagged_prefix.tags(), rules_tagged_prefix.name());
  Stats::LiteScopeHelper shadow_rules_helper(scope, shadow_rules_tagged_prefix.baseName(),
                                             shadow_rules_tagged_prefix.tags(),
                                             shadow_rules_tagged_prefix.name());
  return {
      ENFORCE_RBAC_FILTER_STATS(POOL_COUNTER(rules_helper))
          SHADOW_RBAC_FILTER_STATS(POOL_COUNTER(shadow_rules_helper)) scope,
      makeTaggedPrefix(prefix, rules_prefix, scope, /*per_policy=*/true),
      makeTaggedPrefix(prefix, shadow_rules_prefix, scope, /*per_policy=*/true),
  };
}

void RoleBasedAccessControlFilterStats::incCounter(const Stats::TaggedStatName& prefix,
                                                   absl::string_view policy,
                                                   absl::string_view leaf) {
  Stats::SymbolTable& symbol_table = scope_.symbolTable();
  Stats::StatNameDynamicPool pool(symbol_table);
  const Stats::StatName policy_name = pool.add(policy);
  const Stats::StatName leaf_name = pool.add(leaf);
  const Stats::SymbolTable::StoragePtr flat =
      symbol_table.join({prefix.name(), policy_name, leaf_name});

  if (prefix.tags().empty()) {
    // Untagged group: the policy name stays in the name, and the flat and tag-extracted names
    // are identical.
    scope_.counterFromStatName(Stats::StatName(flat.get())).inc();
    return;
  }

  const Stats::SymbolTable::StoragePtr base = symbol_table.join({prefix.baseName(), leaf_name});
  Stats::StatNameTagVec tags(prefix.tags().begin(), prefix.tags().end());
  tags.emplace_back(pool.add(Envoy::Config::TagNames::get().RBAC_POLICY_NAME), policy_name);
  scope_.counterFromTaggedName(Stats::StatName(base.get()), tags, Stats::StatName(flat.get()))
      .inc();
}

std::string responseDetail(absl::string_view policy_id) {
  // Replace whitespaces in policy_id with '_' to avoid breaking the access log (inconsistent number
  // of segments between log entries when the separator is whitespace).
  const std::string sanitized = StringUtil::replaceAllEmptySpace(policy_id);
  return fmt::format("rbac_access_denied_matched_policy[{}]", sanitized);
}

} // namespace RBAC
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
