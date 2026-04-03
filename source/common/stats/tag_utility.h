#pragma once

#include "envoy/stats/tag.h"

#include "source/common/stats/symbol_table.h"

namespace Envoy {
namespace Stats {
namespace TagUtility {

/**
 * Combines a stat name with an optional set of tag to create the final stat name to use. The
 * resulting StatNames will be valid through the lifetime of this object and all provided stat
 * names.
 */
class TagStatNameJoiner {
public:
  /**
   * Combines a prefix, stat name and tags into a single stat name.
   * @param prefix StaName the stat prefix to use.
   * @param name StaName the stat name to use.
   * @param stat_name_tags optionally StatNameTagVector the stat name tags to add to the stat name.
   */
  TagStatNameJoiner(StatName prefix, StatName stat_name,
                    StatNameTagVectorOptConstRef stat_name_tags, SymbolTable& symbol_table);

  /**
   * Combines a prefix, optional scope-level tags, stat name and optional stat-level tags into a
   * single stat name with the format:
   *   <prefix>[.<scope_tag_segs>].<stat_name>[.<stat_tag_segs>]
   *
   * For each tag, if ignore_name_=false both name_ and value_ are appended as segments;
   * if ignore_name_=true only value_ is appended.
   *
   * @param prefix StatName the scope prefix.
   * @param scope_tags optional scope-level tags inserted after the prefix and before stat_name.
   * @param stat_name StatName the stat name.
   * @param stat_tags optional stat-level tags appended after the stat name.
   * @param symbol_table SymbolTable used for joining.
   */
  TagStatNameJoiner(StatName prefix, StatNameTagVectorOptConstRef scope_tags, StatName stat_name,
                    StatNameTagVectorOptConstRef stat_tags, SymbolTable& symbol_table);

  /**
   * @return StatName the full stat name, including all tag segments.
   */
  StatName nameWithTags() const { return name_with_tags_; }

  /**
   * @return StatName the stat name without any tags (prefix + stat_name only).
   */
  StatName tagExtractedName() const { return tag_extracted_name_; }

  /**
   * Returns the effective tag vector for stat metadata at creation time which will
   * merge the provided scope and stat-level tags.
   *
   * This is the single source of truth for what tag vector to store in a stat.
   * NOTE: the StatNames in the returned vector are not owned by this object and the lifetime
   * must be managed by the caller who creates this object.
   */
  StatNameTagVectorOptConstRef effectiveTags() const { return effective_tags_ref_; }

private:
  // TODO(snowp): This isn't really "tag extracted", but we'll use this for the sake of consistency
  // until we can change the naming convention throughout.
  StatName tag_extracted_name_;
  SymbolTable::StoragePtr prefix_storage_;
  SymbolTable::StoragePtr full_name_storage_;
  StatName name_with_tags_;
  // Merged scope + stat tags for storage in stat metadata; empty if no tags at all.
  StatNameTagVector effective_tags_;
  StatNameTagVectorOptConstRef effective_tags_ref_;
};

bool isTagNameValid(absl::string_view name);

bool isTagValueValid(absl::string_view value);

} // namespace TagUtility
} // namespace Stats
} // namespace Envoy
