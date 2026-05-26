#pragma once

#include <string>

#include "envoy/common/optref.h"

#include "absl/container/inlined_vector.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "absl/types/span.h"

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

using StatNameTag = std::pair<StatName, StatName>;
using StatNameTagVector = std::vector<StatNameTag>;
using StatNameTagVectorOptConstRef =
    absl::optional<std::reference_wrapper<const StatNameTagVector>>;

template <class T> class StatElementBase {
public:
  // The value of the element.
  T value{};

  // The optional name of the element. If provided, this element is treated as a tag and the value
  // does not contribute to the tag-extracted name.
  T name{};

  // By default, both the name and value contribute to the full stat name. When set, the name is
  // omitted and only the value contributes. This only applies to tag elements.
  bool ignore_name{false};
};

using StatElement = StatElementBase<StatName>;
using StatElementView = StatElementBase<absl::string_view>;

template <size_t N> using StatElementVecBase = absl::InlinedVector<StatElement, N>;
template <size_t N> using StatElementViewVecBase = absl::InlinedVector<StatElementView, N>;
using StatElementVec = StatElementVecBase<8>;
using StatElementViewVec = StatElementViewVecBase<4>;
using StatElementSpan = absl::Span<const StatElement>;
using StatElementViewSpan = absl::Span<const StatElementView>;

using StatNameTagSpan = absl::Span<const StatNameTag>;
using StatNameTagVec = absl::InlinedVector<StatNameTag, 8>;

} // namespace Stats
} // namespace Envoy
