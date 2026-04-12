#include "source/common/stats/tag_utility.h"

#include <regex>

#include "source/common/config/well_known_names.h"
#include "source/common/stats/symbol_table.h"

namespace Envoy {
namespace Stats {
namespace TagUtility {

TagStatNameJoiner::TagStatNameJoiner(StatName prefix, StatName stat_name,
                                     StatNameTagVectorOptConstRef stat_name_tags,
                                     SymbolTable& symbol_table)
    : stat_name_tags_(stat_name_tags) {
  prefix_storage_ = symbol_table.join({prefix, stat_name});
  tag_extracted_name_ = StatName(prefix_storage_.get());

  if (stat_name_tags) {
    full_name_storage_ =
        joinNameAndTags(StatName(prefix_storage_.get()), *stat_name_tags, symbol_table);
    name_with_tags_ = StatName(full_name_storage_.get());
  } else {
    name_with_tags_ = StatName(prefix_storage_.get());
  }
}

TagStatNameJoiner::TagStatNameJoiner(StatElementSpan elements, SymbolTable& symbol_table) {
  // Separate elements into path parts (contribute to tag-extracted name) and tag parts.
  // Path elements (empty name_) contribute to both tag_extracted_name_ and name_with_tags_.
  // Tag elements (non-empty name_) contribute only to name_with_tags_.
  StatNameVec tag_extracted_name_parts;
  StatNameVec full_name_parts;
  for (const StatElement elem : elements) {
    if (elem.name_.empty()) {
      // Path element: contributes to both the tag-extracted name and the full name.
      tag_extracted_name_parts.emplace_back(elem.value_);
      full_name_parts.emplace_back(elem.value_);
    } else {
      // Tag element: contributes only to the full name (name=value) and effective_tags_.
      if (!elem.ignore_name_) {
        full_name_parts.emplace_back(elem.name_);
      }
      full_name_parts.emplace_back(elem.value_);
      effective_tags_.emplace_back(elem.name_, elem.value_);
    }
  }

  prefix_storage_ = symbol_table.join(tag_extracted_name_parts);
  tag_extracted_name_ = StatName(prefix_storage_.get());
  if (!effective_tags_.empty()) {
    full_name_storage_ = symbol_table.join(full_name_parts);
    name_with_tags_ = StatName(full_name_storage_.get());
  } else {
    name_with_tags_ = StatName(prefix_storage_.get());
  }
}

SymbolTable::StoragePtr TagStatNameJoiner::joinNameAndTags(StatName name,
                                                           const StatNameTagVector& tags,
                                                           SymbolTable& symbol_table) {
  StatNameVec stat_names;
  stat_names.reserve(1 + 2 * tags.size());
  stat_names.emplace_back(name);

  for (const auto& tag : tags) {
    stat_names.emplace_back(tag.first);
    stat_names.emplace_back(tag.second);
  }

  return symbol_table.join(stat_names);
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

// For HTTP filter, it may inherit raw stat prefix string from parent scope and we should extract
// the related element as tag correctly.
void populateParentStatPrefix(absl::string_view name, StatElementViewVec& view_vec) {
  if (absl::StartsWith(name, "http.")) {
    view_vec.emplace_back(StatElementView{
        .value_ = "http",
    });
    absl::string_view value = name.substr(5);
    if (!value.empty()) {
      if (absl::EndsWith(value, ".")) {
        value.remove_suffix(1);
      }

      view_vec.emplace_back(StatElementView{
          .value_ = value,
          .name_ = Config::TagNames::get().HTTP_CONN_MANAGER_PREFIX,
          .ignore_name_ = true,
      });
    }
  } else if (absl::StartsWith(name, "cluster.")) {
    view_vec.emplace_back(StatElementView{
        .value_ = "cluster",
    });
    absl::string_view value = name.substr(8);
    if (!value.empty()) {
      if (absl::EndsWith(value, ".")) {
        value.remove_suffix(1);
      }
      view_vec.emplace_back(StatElementView{
          .value_ = value,
          .name_ = Config::TagNames::get().CLUSTER_NAME,
          .ignore_name_ = true,
      });
    }
  } else {
    view_vec.emplace_back(StatElementView{
        .value_ = name,
    });
  }
}

} // namespace TagUtility
} // namespace Stats
} // namespace Envoy
