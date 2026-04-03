#pragma once

#include <string>

#include "envoy/common/optref.h"

#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

namespace Envoy {
namespace Stats {

class StatName;

/**
 * General representation of a tag.
 */
struct Tag {
  std::string name_;
  std::string value_;

  bool operator==(const Tag& other) const {
    return other.name_ == name_ && other.value_ == value_;
  };
};

using TagVector = std::vector<Tag>;

template <class T> struct TagBase {
  T name_;
  T value_;
  // If set to true, the tag name will be ignored when concatenating the full stat name.
  bool ignore_name_ = false;
};

using StatNameTag = TagBase<StatName>;
using StatNameTagVector = std::vector<StatNameTag>;
using StatNameTagVectorOptConstRef =
    absl::optional<std::reference_wrapper<const StatNameTagVector>>;

using TagView = TagBase<absl::string_view>;
using TagViewVector = std::vector<TagView>;
using TagViewVectorOptConstRef = Envoy::OptRef<const TagViewVector>;

} // namespace Stats
} // namespace Envoy
