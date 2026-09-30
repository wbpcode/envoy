#include "source/extensions/filters/common/rbac/utility.h"

#include <string>

#include "source/common/stats/prefix_utility.h"

#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_replace.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace RBAC {

void RoleBasedAccessControlFilterStats::incPolicyCounter(const Stats::TaggedStatName& prefix,
                                                         absl::string_view policy,
                                                         absl::string_view stat) {
  Stats::StatNameDynamicPool pool(scope_.symbolTable());
  const Stats::StatName policy_name = pool.add(policy);
  const Stats::StatName stat_name = pool.add(stat);
  if (policy_tag_name_.empty()) {
    Stats::Utility::counterFromStatNames(scope_, {prefix.name(), policy_name, stat_name}).inc();
    return;
  }
  Stats::StatNameTagVec tags(prefix.tags().begin(), prefix.tags().end());
  tags.emplace_back(pool.add(policy_tag_name_), policy_name);
  const Stats::SymbolTable::StoragePtr tagged_prefix =
      scope_.symbolTable().join({prefix.name(), policy_name});
  Stats::Utility::counterFromTaggedPrefix(scope_, prefix.baseName(), tags,
                                          Stats::StatName(tagged_prefix.get()), stat_name)
      .inc();
}

RoleBasedAccessControlFilterStats
generateStats(const std::string& prefix, const std::string& rules_prefix,
              const std::string& shadow_rules_prefix, Stats::Scope& scope,
              absl::string_view rules_prefix_tag_name, absl::string_view policy_tag_name) {
  // The parent prefix ends with a dot so that mergeStatPrefix() can extract its tag and join it
  // with the names below directly.
  const std::string parent_prefix =
      prefix.empty() || absl::EndsWith(prefix, ".") ? prefix : absl::StrCat(prefix, ".");

  // '<prefix>rbac[.<rules>]<suffix>', with the rules prefix as a tag when it is tagged.
  auto own_prefix = [&](const std::string& rules,
                        absl::string_view suffix) -> Stats::TaggedStatName {
    if (rules.empty() || rules_prefix_tag_name.empty()) {
      return Stats::mergeStatPrefix(
          scope.symbolTable(), parent_prefix,
          absl::StrCat("rbac", rules.empty() ? "" : absl::StrCat(".", rules), suffix));
    }
    const Stats::TagStringView tag{rules_prefix_tag_name, rules};
    return Stats::mergeStatPrefix(scope.symbolTable(), parent_prefix, absl::StrCat("rbac", suffix),
                                  Stats::TagStringViewSpan(&tag, 1),
                                  absl::StrCat("rbac.", rules, suffix));
  };

  const Stats::TaggedStatName rules_stats_prefix = own_prefix(rules_prefix, "");
  const Stats::TaggedStatName shadow_rules_stats_prefix = own_prefix(shadow_rules_prefix, "");
  return {
      ENFORCE_RBAC_FILTER_STATS(POOL_COUNTER_TAGGED(scope, rules_stats_prefix))
          SHADOW_RBAC_FILTER_STATS(POOL_COUNTER_TAGGED(scope, shadow_rules_stats_prefix)) scope,
      own_prefix(rules_prefix, ".policy"),
      own_prefix(shadow_rules_prefix, ".policy"),
      std::string(policy_tag_name),
  };
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
