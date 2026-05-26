#include "source/common/stats/tag_utility.h"

#include <regex>

#include "source/common/config/well_known_names.h"
#include "source/common/stats/symbol_table.h"

#include "absl/container/flat_hash_map.h"
#include "absl/strings/match.h"
#include "absl/strings/str_join.h"

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

TagStatNameJoiner::TagStatNameJoiner(StatElementSpan prefix_elements, StatElementSpan stat_elements,
                                     SymbolTable& symbol_table) {
  StatNameVec tag_extracted_name_parts;
  StatNameVec full_name_parts;

  for (const StatElement& elem : prefix_elements) {
    if (elem.name.empty()) {
      tag_extracted_name_parts.emplace_back(elem.value);
      full_name_parts.emplace_back(elem.value);
    } else {
      if (!elem.ignore_name) {
        full_name_parts.emplace_back(elem.name);
      }
      full_name_parts.emplace_back(elem.value);
      effective_tags_.emplace_back(elem.name, elem.value);
    }
  }
  for (const StatElement& elem : stat_elements) {
    if (elem.name.empty()) {
      tag_extracted_name_parts.emplace_back(elem.value);
      full_name_parts.emplace_back(elem.value);
    } else {
      if (!elem.ignore_name) {
        full_name_parts.emplace_back(elem.name);
      }
      full_name_parts.emplace_back(elem.value);
      effective_tags_.emplace_back(elem.name, elem.value);
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

TagStatNameJoiner::TagStatNameJoiner(StatName prefix, StatElementSpan stat_elements,
                                     SymbolTable& symbol_table) {
  StatNameVec tag_extracted_name_parts;
  StatNameVec full_name_parts;

  if (!prefix.empty()) {
    tag_extracted_name_parts.emplace_back(prefix);
    full_name_parts.emplace_back(prefix);
  }
  for (const StatElement& elem : stat_elements) {
    if (elem.name.empty() || elem.ignore_name) {
      // Plain path token, OR a well-known tag (ignore_name=true) the caller
      // wants the legacy TagProducer to regex-extract downstream. We do NOT
      // record it in effective_tags_ — that would short-circuit regex
      // extraction and silently drop any sibling tags it would have picked up.
      tag_extracted_name_parts.emplace_back(elem.value);
      full_name_parts.emplace_back(elem.value);
    } else {
      // Explicit caller-declared tag: honored verbatim, regex skipped.
      full_name_parts.emplace_back(elem.name);
      full_name_parts.emplace_back(elem.value);
      effective_tags_.emplace_back(elem.name, elem.value);
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

namespace {

// Static lookup from the first dot-token of a legacy stat prefix to the
// well-known tag name to attach to the next token. Mirrors a subset of the
// simple `prefix.$.**` tokenized descriptors in well_known_names.cc.
//
// Intentionally small: multi-prefix patterns (e.g. `vhost.*.vcluster.$.**`,
// `auth.clientssl.$.**`) and regex-based descriptors (e.g. `listener.X.**`)
// are not handled here. We expect to extend this map as the element-based
// stats path matures, rather than aim for full parity in one change.
//
// NOTE: This is only used for convenience temporarily and we will migrate the
// stats creation to use element-based API gradually and remove this map eventually.
const absl::flat_hash_map<absl::string_view, absl::string_view>& legacyPrefixToTagName() {
  static const auto* map = []() {
    const auto& tags = Config::TagNames::get();
    return new absl::flat_hash_map<absl::string_view, absl::string_view>{
        {"http", tags.HTTP_CONN_MANAGER_PREFIX},
        {"cluster", tags.CLUSTER_NAME},
        {"tcp", tags.TCP_PREFIX},
        {"udp", tags.UDP_PREFIX},
        {"vhost", tags.VIRTUAL_HOST},
        {"mongo", tags.MONGO_PREFIX},
        {"redis", tags.REDIS_PREFIX},
        {"thrift", tags.THRIFT_PREFIX},
        {"grpc", tags.GOOGLE_GRPC_CLIENT_PREFIX},
    };
  }();
  return *map;
}

} // namespace

void populateWellKnownLegacyStatPrefix(absl::string_view name, StatElementViewVec& view_vec) {
  if (absl::EndsWith(name, ".")) {
    name.remove_suffix(1);
  }
  if (name.empty()) {
    return;
  }

  const size_t first_dot = name.find('.');
  if (first_dot != absl::string_view::npos) {
    const absl::string_view prefix = name.substr(0, first_dot);
    const auto& map = legacyPrefixToTagName();
    if (const auto it = map.find(prefix); it != map.end()) {
      view_vec.emplace_back(StatElementView{.value = prefix});
      const absl::string_view rest = name.substr(first_dot + 1);
      if (!rest.empty()) {
        view_vec.emplace_back(StatElementView{
            .value = rest,
            .name = it->second,
            .ignore_name = true,
        });
      }
      return;
    }
  }

  // No matching well-known prefix (or no dot at all): keep the whole input as
  // a single path element with no tag extraction.
  view_vec.emplace_back(StatElementView{.value = name});
}

StatNameManagedStorage buildScopePrefixStorage(StatElementSpan elements,
                                               SymbolTable& symbol_table) {
  if (elements.empty()) {
    return {"", symbol_table};
  }
  TagUtility::TagStatNameJoiner joiner(elements, {}, symbol_table);
  return {joiner.nameWithTags(), symbol_table};
}

JoinedElementName joinElementValues(StatElementSpan elements, SymbolTable& symbol_table) {
  // Collect non-empty values. Fast paths for 0 and 1 non-empty entries avoid touching the symbol
  // table: 0 returns an empty StatName, 1 aliases the caller's existing StatName.
  StatNameVec parts;
  parts.reserve(elements.size());
  for (const StatElement& elem : elements) {
    if (!elem.value.empty()) {
      parts.emplace_back(elem.value);
    }
  }
  if (parts.empty()) {
    return {StatName{}, nullptr};
  }
  if (parts.size() == 1) {
    return {parts[0], nullptr};
  }
  auto storage = symbol_table.join(parts);
  return {StatName(storage.get()), std::move(storage)};
}

std::string joinElementValues(StatElementViewSpan elements) {
  // Find the count of non-empty values. With 0 we return ""; with exactly 1 we return that value
  // directly without going through absl::StrJoin.
  absl::string_view single;
  size_t non_empty_count = 0;
  for (const StatElementView& elem : elements) {
    if (!elem.value.empty()) {
      single = elem.value;
      ++non_empty_count;
    }
  }
  if (non_empty_count == 0) {
    return "";
  }
  if (non_empty_count == 1) {
    return std::string(single);
  }
  absl::InlinedVector<absl::string_view, 4> parts;
  parts.reserve(non_empty_count);
  for (const StatElementView& elem : elements) {
    if (!elem.value.empty()) {
      parts.emplace_back(elem.value);
    }
  }
  return absl::StrJoin(parts, ".");
}

void mergeTagsToElements(StatElementVec& merged, StatNameTagVectorOptConstRef tags) {
  if (tags.has_value()) {
    for (const auto& tag : tags->get()) {
      merged.emplace_back(StatElement{.value = tag.second, .name = tag.first});
    }
  }
}

} // namespace TagUtility
} // namespace Stats
} // namespace Envoy
