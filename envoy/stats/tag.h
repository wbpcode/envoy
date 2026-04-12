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
  T value_{};

  // The optional name of the element. If the name is provided, this element will be treated
  // as a tag and the above value will not contribute to the tag-extracted name.
  T name_{};

  // By default, both the name and value of the element will be used to generate the full stat
  // name. If this boolean is set to true, the name of the element will be skipped and only
  // the value will be used to generate the full stat name.
  // This only make sense when the name of the element is provided.
  bool ignore_name_{false};
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
