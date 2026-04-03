#include "source/common/stats/tag_utility.h"

#include <cstddef>
#include <regex>

#include "source/common/config/well_known_names.h"
#include "source/common/stats/symbol_table.h"

namespace Envoy {
namespace Stats {
namespace TagUtility {

TagStatNameJoiner::TagStatNameJoiner(StatName prefix, StatName stat_name,
                                     StatNameTagVectorOptConstRef stat_name_tags,
                                     SymbolTable& symbol_table)
    : TagStatNameJoiner(prefix, absl::nullopt, stat_name, stat_name_tags, symbol_table) {}

TagStatNameJoiner::TagStatNameJoiner(StatName prefix, StatNameTagVectorOptConstRef scope_tags,
                                     StatName stat_name,
                                     StatNameTagVectorOptConstRef stat_name_tags,
                                     SymbolTable& symbol_table) {
  // tag_extracted_name_ = prefix + stat_name (no tags at all).
  prefix_storage_ = symbol_table.join({prefix, stat_name});
  tag_extracted_name_ = StatName(prefix_storage_.get());

  const size_t scope_tags_size = scope_tags.has_value() ? scope_tags->get().size() : 0;
  const size_t stat_tags_size = stat_name_tags.has_value() ? stat_name_tags->get().size() : 0;
  const size_t total_tags_size = scope_tags_size + stat_tags_size;

  // No tags and skip joining directly.
  if (total_tags_size == 0) {
    name_with_tags_ = tag_extracted_name_;
    return;
  }

  if (scope_tags_size == 0) {
    effective_tags_ref_ = stat_name_tags;
  } else if (stat_tags_size == 0) {
    effective_tags_ref_ = scope_tags;
  } else {
    // If we have both scope and stat tags, we need to merge them. We maintain the order of tags as
    // scope tags followed by stat tags.
    effective_tags_.reserve(total_tags_size);
    effective_tags_ref_ = effective_tags_;
  }

  // Build: prefix + scope_tag_segs + stat_name + stat_tag_segs.
  StatNameVec parts;
  parts.reserve(2 + 2 * total_tags_size);
  parts.emplace_back(prefix);

  if (scope_tags_size > 0) {
    for (const auto& [key, value, ignore_name] : scope_tags->get()) {
      if (!ignore_name) {
        parts.emplace_back(key);
      }
      parts.emplace_back(value);

      // If we have both scope and stat tags, we need to merge them.
      if (stat_tags_size > 0) {
        effective_tags_.push_back({key, value, ignore_name});
      }
    }
  }

  parts.emplace_back(stat_name);

  if (stat_tags_size > 0) {
    for (const auto& [key, value, ignore_name] : stat_name_tags->get()) {
      if (!ignore_name) {
        parts.emplace_back(key);
      }
      parts.emplace_back(value);

      // If we have both scope and stat tags, we need to merge them.
      if (scope_tags_size > 0) {
        effective_tags_.push_back({key, value, ignore_name});
      }
    }
  }

  full_name_storage_ = symbol_table.join(parts);
  name_with_tags_ = StatName(full_name_storage_.get());
}

bool isTagValueValid(absl::string_view name) {
  return Config::doesTagNameValueMatchInvalidCharRegex(name);
}

bool isTagNameValid(absl::string_view value) {
  for (const auto& token : value) {
    if (!absl::ascii_isalnum(token)) {
      return false;
    }
  }
  return true;
}

} // namespace TagUtility
} // namespace Stats
} // namespace Envoy
