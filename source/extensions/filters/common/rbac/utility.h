#pragma once

#include "envoy/stats/stats_macros.h"

#include "source/common/common/fmt.h"
#include "source/common/singleton/const_singleton.h"
#include "source/common/stats/symbol_table.h"
#include "source/common/stats/utility.h"
#include "source/extensions/filters/common/rbac/engine_impl.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace RBAC {

/**
 * All stats for the enforced rules in RBAC filter. @see stats_macros.h
 */
#define ENFORCE_RBAC_FILTER_STATS(COUNTER)                                                         \
  COUNTER(allowed)                                                                                 \
  COUNTER(denied)

/**
 * All stats for the shadow rules in RBAC filter. @see stats_macros.h
 */
#define SHADOW_RBAC_FILTER_STATS(COUNTER)                                                          \
  COUNTER(shadow_allowed)                                                                          \
  COUNTER(shadow_denied)

/**
 * Wrapper struct for shadow rules in RBAC filter stats. @see stats_macros.h
 */
struct RoleBasedAccessControlFilterStats {
  ENFORCE_RBAC_FILTER_STATS(GENERATE_COUNTER_STRUCT)
  SHADOW_RBAC_FILTER_STATS(GENERATE_COUNTER_STRUCT)

  Stats::Scope& scope_;
  // '<prefix>rbac[.<rules_prefix>].policy' and '<prefix>rbac[.<shadow_rules_prefix>].policy', the
  // prefixes of the per-policy stats: their tag-extracted form, the tags they carry and their flat
  // form. See generateStats().
  const Stats::TaggedStatName per_policy_prefix_;
  const Stats::TaggedStatName per_policy_shadow_prefix_;
  // The tag the policy name is emitted as on the per-policy stats, or empty when the policy name
  // is not tagged and stays a plain segment of the stat name.
  const std::string policy_tag_name_;

  void incPolicyAllowed(absl::string_view name) {
    incPolicyCounter(per_policy_prefix_, name, "allowed");
  }

  void incPolicyDenied(absl::string_view name) {
    incPolicyCounter(per_policy_prefix_, name, "denied");
  }

  void incPolicyShadowAllowed(absl::string_view name) {
    incPolicyCounter(per_policy_shadow_prefix_, name, "shadow_allowed");
  }

  void incPolicyShadowDenied(absl::string_view name) {
    incPolicyCounter(per_policy_shadow_prefix_, name, "shadow_denied");
  }

  /**
   * Increments '<prefix>.<policy>.<stat>'. When policy_tag_name_ is set the policy name is emitted
   * as that tag and the tag-extracted name is '<prefix base>.<stat>'; otherwise the stat carries
   * no tag of its own.
   */
  void incPolicyCounter(const Stats::TaggedStatName& prefix, absl::string_view policy,
                        absl::string_view stat);
};

/**
 * Creates the stats of an RBAC filter, named '<prefix>rbac[.<rules_prefix>].<stat>' for the
 * enforced rules, '<prefix>rbac[.<shadow_rules_prefix>].<stat>' for the shadow rules and
 * '<prefix>rbac[.<rules_prefix>].policy.<policy>.<stat>' per policy.
 *
 * @param prefix the prefix of the stats, e.g. the 'http.<stat_prefix>.' of a connection manager;
 *        when it carries a well-known tag that tag is extracted (see Stats::mergeStatPrefix()).
 *        It is empty when `scope` is already named after the prefix.
 * @param rules_prefix the optional prefix of the enforced rules' stats.
 * @param shadow_rules_prefix the optional prefix of the shadow rules' stats.
 * @param scope the scope to create the stats in.
 * @param rules_prefix_tag_name the tag the rules prefixes are emitted as, or empty when they stay
 *        plain segments of the stat names.
 * @param policy_tag_name the tag the policy names are emitted as, or empty when they stay plain
 *        segments of the stat names.
 */
RoleBasedAccessControlFilterStats
generateStats(const std::string& prefix, const std::string& rules_prefix,
              const std::string& shadow_rules_prefix, Stats::Scope& scope,
              absl::string_view rules_prefix_tag_name = {}, absl::string_view policy_tag_name = {});

template <class ConfigType>
std::unique_ptr<RoleBasedAccessControlEngine>
createEngine(const ConfigType& config, Server::Configuration::ServerFactoryContext& context,
             ProtobufMessage::ValidationVisitor& validation_visitor,
             ActionValidationVisitor& action_validation_visitor) {
  if (config.has_matcher()) {
    if (config.has_rules()) {
      ENVOY_LOG_MISC(warn, "RBAC rules are ignored when matcher is configured");
    }
    return std::make_unique<RoleBasedAccessControlMatcherEngineImpl>(
        config.matcher(), context, action_validation_visitor, EnforcementMode::Enforced);
  }
  if (config.has_rules()) {
    return std::make_unique<RoleBasedAccessControlEngineImpl>(config.rules(), validation_visitor,
                                                              context, EnforcementMode::Enforced);
  }

  return nullptr;
}

template <class ConfigType>
std::unique_ptr<RoleBasedAccessControlEngine>
createShadowEngine(const ConfigType& config, Server::Configuration::ServerFactoryContext& context,
                   ProtobufMessage::ValidationVisitor& validation_visitor,
                   ActionValidationVisitor& action_validation_visitor) {
  if (config.has_shadow_matcher()) {
    if (config.has_shadow_rules()) {
      ENVOY_LOG_MISC(warn, "RBAC shadow rules are ignored when shadow matcher is configured");
    }
    return std::make_unique<RoleBasedAccessControlMatcherEngineImpl>(
        config.shadow_matcher(), context, action_validation_visitor, EnforcementMode::Shadow);
  }
  if (config.has_shadow_rules()) {
    return std::make_unique<RoleBasedAccessControlEngineImpl>(
        config.shadow_rules(), validation_visitor, context, EnforcementMode::Shadow);
  }

  return nullptr;
}

std::string responseDetail(absl::string_view policy_id);

} // namespace RBAC
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
