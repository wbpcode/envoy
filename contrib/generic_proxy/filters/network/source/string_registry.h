#pragma once

#include <memory>

#include "source/common/common/macros.h"
#include "source/common/common/utility.h"

#include "absl/container/node_hash_map.h"

#include "absl/status/statusor.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace GenericProxy {

/**
 * A registry for strings that assigns each added string a unique ID (uint32).
 * Then the ID could be used to represent the string in the code.
 * TODO(wbpcode): move this to /source/common/common/ if it is useful for other
 * parts of Envoy.
 */
class StringRegistry {
public:
  using StringId = uint32_t;

  /**
   * Add a string to the registry and return its ID. If the string already exists,
   * return the existing ID by default.
   * @param str the string to add.
   * @param ERROR_IF_EXISTS non-type template parameter. If true, crash Envoy if
   * the string already exists. Default is false.
   * @return the ID of the string.
   *
   */
  template <bool ERROR_IF_EXISTS = false> StringId add(absl::string_view str) {
    const auto it = string_map_.find(str);

    // If adding a string repeatedly is not allowed, crash Envoy if the string
    // already exists.
    if (ERROR_IF_EXISTS) {
      RELEASE_ASSERT(it == string_map_.end(), fmt::format("'{}' already exists", str));
    }

    // Return the existing string ID if the string already exists.
    if (it != string_map_.end()) {
      return it->second;
    }

    // Add the string to the map and store the string view in the vector.
    const StringId id = string_vec_.size();
    auto result = string_map_.emplace(str, id);
    ASSERT(result.second);
    string_vec_.emplace_back(absl::string_view(result.first->first));

    return id;
  }

  /**
   * Get the ID of a string.
   * @param str the string to get the ID of.
   * @return the ID of the string if it exists, otherwise absl::nullopt.
   */
  absl::optional<StringId> get(absl::string_view str) const {
    const auto it = string_map_.find(str);
    return it == string_map_.end() ? absl::nullopt : absl::optional<StringId>(it->second);
  }

  /**
   * Get the string of an ID.
   * @param id the ID to get the string of.
   * @return the string of the ID if it exists, otherwise absl::nullopt.
   */
  absl::optional<absl::string_view> get(StringId id) const {
    return id < string_vec_.size() ? absl::optional<absl::string_view>(string_vec_[id])
                                   : absl::nullopt;
  }

private:
  // node_hash_map is used to ensure that the string view is not invalidated when
  // the map is modified.
  using StringMap = absl::node_hash_map<std::string, StringId>;
  using StringVec = std::vector<absl::string_view>;

  StringMap string_map_;
  StringVec string_vec_;
};

} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
