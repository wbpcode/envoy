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
   * Constructs a stat name from a prefix and a span of StatElements.
   *
   * Each element contributes to the stat name as follows:
   *   - Elements with an empty name_ contribute their value_ to
   *     the tag-extracted stat name.
   *   - Elements with a non-empty name_ are treated as tags: their
   *     name_ becomes the tag key and value_ becomes the tag value. These are NOT included
   *     in the tag-extracted name but ARE appended to the full stat name (nameWithTags()).
   *
   * @param elements span of StatElement describing the stat name and its tags.
   * @param symbol_table the SymbolTable used to join stat names.
   */
  TagStatNameJoiner(StatElementSpan elements, SymbolTable& symbol_table);

  /**
   * @return StatName the full stat name, including the tag suffix.
   */
  StatName nameWithTags() const { return name_with_tags_; }

  /**
   * @return StatName the stat name without the tags appended.
   */
  StatName tagExtractedName() const { return tag_extracted_name_; }

  /**
   * @return the effective tags, which is either the tags provided in the legacy constructor or the
   * tags derived from the StatElementSpan constructor.
   */
  StatNameTagSpan effectiveTags() const {
    if (stat_name_tags_) {
      return stat_name_tags_->get();
    } else {
      return effective_tags_;
    }
  }

private:
  // TODO(snowp): This isn't really "tag extracted", but we'll use this for the sake of consistency
  // until we can change the naming convention throughout.
  StatName tag_extracted_name_;
  SymbolTable::StoragePtr prefix_storage_;
  SymbolTable::StoragePtr full_name_storage_;
  StatName name_with_tags_;

  // The tags from the legacy constructor.
  StatNameTagVectorOptConstRef stat_name_tags_;

  // Storage for tags derived from StatElementSpan.
  StatNameTagVec effective_tags_;

  SymbolTable::StoragePtr joinNameAndTags(StatName name, const StatNameTagVector& stat_name_tags,
                                          SymbolTable& symbol_table);
};

bool isTagNameValid(absl::string_view name);

bool isTagValueValid(absl::string_view value);

void populateParentStatPrefix(absl::string_view name, StatElementViewVec& view_vec);

} // namespace TagUtility
} // namespace Stats
} // namespace Envoy
