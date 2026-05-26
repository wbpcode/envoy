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
   * Combines a sequence of stat elements into a single stat name.
   * @param prefix_elements stat elements that contribute path components and/or tags.
   * @param stat_elements stat elements that contribute path components and/or tags.
   * @param symbol_table the symbol table used to join stat names.
   */
  TagStatNameJoiner(StatElementSpan prefix_elements, StatElementSpan stat_elements,
                    SymbolTable& symbol_table);

  /**
   * Combines a flat StatName prefix with a sequence of stat elements. Used
   * by legacy (non-element-aware) Scopes to implement the element-based
   * getOrCreate* APIs while keeping the regex-based TagProducer pipeline
   * intact.
   *
   * Per-element semantics — note the asymmetry with the
   * (StatElementSpan, StatElementSpan, ...) constructor on `ignore_name`:
   *   - `name` empty                          → plain path token.
   *   - `name` non-empty, `ignore_name=true`  → also treated as a plain path
   *                                             token (NOT recorded as an
   *                                             explicit tag), so the legacy
   *                                             TagProducer can still
   *                                             regex-extract this and any
   *                                             sibling tags from the name.
   *   - `name` non-empty, `ignore_name=false` → explicit caller-declared tag;
   *                                             recorded in effective_tags_,
   *                                             regex extraction is skipped.
   *
   * @param prefix scope-level StatName prefix (empty StatName is skipped).
   * @param stat_elements stat elements; see semantics above.
   * @param symbol_table the symbol table used to join stat names.
   */
  TagStatNameJoiner(StatName prefix, StatElementSpan stat_elements, SymbolTable& symbol_table);

  /**
   * @return StatName the full stat name, including the tag suffix.
   */
  StatName nameWithTags() const { return name_with_tags_; }

  /**
   * @return StatName the stat name without the tags appended.
   */
  StatName tagExtractedName() const { return tag_extracted_name_; }

  /**
   * @return the effective tags for the joined stat name.
   *
   * Tags come from one of two mutually-exclusive sources, depending on which
   * constructor was used:
   * 1. The (prefix, stat_name, stat_name_tags, ...) constructor: tags are
   *    passed in directly. We return a span into the caller-owned vector and
   *    rely on the caller keeping it alive for the joiner's lifetime. Wins
   *    when present.
   * 2. The (prefix_elements, stat_elements, ...) constructor: tags are derived
   *    from any StatElement{value, name} entries with a non-empty name, and
   *    stored in `effective_tags_` owned by this joiner.
   *
   * Returns nullopt only when neither source produced any tags.
   */
  absl::optional<StatNameTagSpan> effectiveTags() const {
    if (stat_name_tags_) {
      return stat_name_tags_->get();
    } else if (!effective_tags_.empty()) {
      return effective_tags_;
    } else {
      return {};
    }
  }

private:
  // TODO(snowp): This isn't really "tag extracted", but we'll use this for the sake of consistency
  // until we can change the naming convention throughout.
  StatName tag_extracted_name_;
  SymbolTable::StoragePtr prefix_storage_;
  SymbolTable::StoragePtr full_name_storage_;
  StatName name_with_tags_;

  // The tags are provided to the constructor. The TagStatNameJoiner must not outlive the
  // tags; therefore, it is safe to keep a reference here.
  StatNameTagVectorOptConstRef stat_name_tags_;

  // Storage for tags derived from StatElementSpan.
  StatNameTagVec effective_tags_;

  SymbolTable::StoragePtr joinNameAndTags(StatName name, const StatNameTagVector& stat_name_tags,
                                          SymbolTable& symbol_table);
};

bool isTagNameValid(absl::string_view name);

bool isTagValueValid(absl::string_view value);

void populateWellKnownLegacyStatPrefix(absl::string_view name, StatElementViewVec& view_vec);

StatNameManagedStorage buildScopePrefixStorage(StatElementSpan elements, SymbolTable& symbol_table);

/**
 * Result of joinElementValues: the joined StatName, plus optional backing storage.
 *
 * When the input has 0 or 1 non-empty values no join is performed and `storage` is null. The
 * `name` is either default-constructed (0 values) or aliases the caller-owned StatName (1 value);
 * the caller MUST keep that source alive for the lifetime of `name`. When 2+ values are joined,
 * `storage` owns the joined bytes and `name` references it.
 */
struct JoinedElementName {
  StatName name;
  SymbolTable::StoragePtr storage;
};

/**
 * Join only the `value` fields of `elements` into a single StatName, dropping any tag name and
 * `ignore_name` metadata. Empty values are skipped. 0 and 1 non-empty values are returned without
 * touching the symbol table. Intended as the legacy-scope fallback for the element-based API:
 * scopes that do not understand tagged elements treat the inputs as plain path components.
 */
JoinedElementName joinElementValues(StatElementSpan elements, SymbolTable& symbol_table);

/**
 * String-view counterpart to joinElementValues: joins only `value` fields with '.'. Empty values
 * are skipped. 0 or 1 non-empty values are returned without going through the join path.
 */
std::string joinElementValues(StatElementViewSpan elements);

template <class ElementSpanType>
void populatePoolAndElementVec(StatElementSpan prefix, ElementSpanType names, StatNamePool* pool,
                               StatElementVec& out) {
  out.reserve(prefix.size() + names.size());
  auto intern_stat_name = [pool](auto name) -> StatName {
    return name.empty() ? StatName{} : pool->add(name);
  };
  for (const auto& elem : prefix) {
    out.emplace_back(intern_stat_name(elem.value), intern_stat_name(elem.name), elem.ignore_name);
  }
  for (const auto& elem : names) {
    out.emplace_back(intern_stat_name(elem.value), intern_stat_name(elem.name), elem.ignore_name);
  }
}

void mergeTagsToElements(StatElementVec& merged, StatNameTagVectorOptConstRef tags);

} // namespace TagUtility
} // namespace Stats
} // namespace Envoy
