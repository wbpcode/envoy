#pragma once

#include <string>

#include "absl/container/flat_hash_map.h"
#include "absl/strings/string_view.h"
#include "fmt/format.h"

namespace Envoy {
namespace Json {

/**
 * Sanitizes a string so it is suitable for JSON. The buffer is
 * used if any of the characters in str need to be escaped. Performance
 * is good if there are no characters requiring escaping or utf-8 decode.
 *
 * --------------------------------------------------------------------
 * Benchmark                          Time             CPU   Iterations
 * --------------------------------------------------------------------
 * BM_ProtoEncoderNoEscape         1445 ns         1444 ns       455727
 * BM_NlohmannNoEscape             9.79 ns         9.79 ns     71449511
 * BM_ProtoEncoderWithEscape       1521 ns         1521 ns       462697
 * BM_NlohmannWithEscape            215 ns          215 ns      3264218
 *
 * The returned string is suitable for including in a double-quoted JSON
 * context, but does not include the surrounding double-quotes. The primary
 * reason is performance: most of the time this function can return the
 * passed-in str without needing to perform memory operations, and the main
 * expected usage is in a context where unconditionally adding the double-quotes
 * is fast and easy.
 *
 * @param buffer a string in which an escaped string can be written, if needed.
 *   It is not necessary for callers to clear the buffer first; it be cleared
 *   by this method if needed.
 * @param str the string to be translated
 * @return the translated string_view, valid as long as both buffer and str are
 *   valid.
 */
absl::string_view sanitize(std::string& buffer, absl::string_view str);

/**
 * Strips double-quotes on first and last characters of str. It's a
 * precondition to call this on a string that is surrounded by double-quotes.
 *
 * @param str The string to strip double-quotes from.
 * @return The string without its surrounding double-quotes.
 */
absl::string_view stripDoubleQuotes(absl::string_view str);

/**
 * Helper class to sanitize keys, values, and delimiters to short JSON pieces
 * (strings).
 * NOTE: Use this class carefully. YOUSELF should make sure the JSON pieces
 * are sanitized and constructed correctly.
 * NOTE: Although this class could but is not designed to construct a complete
 * JSON output. It is designed to sanitize and construct partial JSON pieces
 * on demand.
 * NOTE: If a complete JSON output is needed, using Envoy::Json::Streamer first
 * except YOU KNOW WHAT YOU ARE DOING.
 */
class JsonSanitizer {
public:
  static constexpr absl::string_view QuoteValue = R"(")";
  static constexpr absl::string_view TrueValue = R"(true)";
  static constexpr absl::string_view FalseValue = R"(false)";
  static constexpr absl::string_view NullValue = R"(null)";

  // Constructor with initial buffer sizes. This class is designed to be used
  // to sanitize short JSON pieces, so the default buffer sizes are small.
  JsonSanitizer(uint64_t initial_raw_json_buffer_size = 64,
                uint64_t initial_sanitize_buffer_size = 64);

  /**
   * Add delimieter '{' to the raw JSON piece buffer.
   */
  void addMapBegDelimiter();

  /**
   * Add delimieter '}' to the raw JSON piece buffer.
   */
  void addMapEndDelimiter();

  /**
   * Add delimieter '[' to the raw JSON piece buffer.
   */
  void addArrayBegDelimiter();

  /**
   * Add delimieter ']' to the raw JSON piece buffer.
   */
  void addArrayEndDelimiter();

  /**
   * Add delimieter ':' to the raw JSON piece buffer.
   */
  void addKeyValueDelimiter();

  /**
   * Add delimieter ',' to the raw JSON piece buffer.
   */
  void addElementsDelimiter();

  /**
   * Add a string value to the raw JSON piece buffer. The string value will
   * be sanitized per JSON rules.
   *
   * @param value The string value or key to be sanitized and added.
   * @param QUOTE Whether to quote the string value. Default is true. This
   * parameter should be true unless the caller wants to handle the quoting
   * by itself.
   *
   * NOTE: Both key and string values should use this method to sanitize.
   */
  template <bool QUOTE = true> void addString(absl::string_view value) {
    // Sanitize the string value and quote it on demand of the caller.
    addPiece<QUOTE>(sanitize(sanitize_buffer_, value));
  }

  /**
   * Add a number value to the raw JSON piece buffer.

   * @param value The number value to be added.
   * @param QUOTE Whether to quote the number value. Default is false. This
   * parameter should be false unless the caller wants to serialize the
   * number value as a string.
   */
  template <bool QUOTE = false> void addNumber(double value) {
    // TODO(wbpcode): use the Buffer::Util::serializeDouble function to serialize the
    // double value.
    addPiece<QUOTE>(fmt::to_string(value));
  }

  /**
   * Add a bool value to the raw JSON piece buffer.
   *
   * @param value The bool value to be added.
   * @param QUOTE Whether to quote the bool value. Default is false. This
   * parameter should be false unless the caller wants to serialize the
   * bool value as a string.
   */
  template <bool QUOTE = false> void addBool(bool value) {
    addPiece<QUOTE>(value ? TrueValue : FalseValue);
  }

  /**
   * Add a null value to the raw JSON piece buffer.
   */
  void addNull() { addPiece<false>(NullValue); }

  /**
   * Add a raw string piece to the buffer. Please make sure the string piece
   * is sanitized before calling this method.
   * The string piece may represent a JSON key, string value, number value,
   * delimiter, or even a complete/partial JSON piece.
   *
   * @param sanitized_piece The sanitized string piece to be added.
   * @param QUOTE is used to control whether to quote the input string piece.
   * Default is false.
   *
   * NOTE: This method should be used carefully. The caller should make sure
   * the input string piece is sanitized and constructed correctly and the
   * quote parameter is set correctly.
   */
  template <bool QUOTE = false> void addPiece(std::string_view sanitized_piece) {
    if constexpr (QUOTE) {
      absl::StrAppend(&raw_json_buffer_, QuoteValue, sanitized_piece, QuoteValue);
    } else {
      raw_json_buffer_.append(sanitized_piece);
    }
  }

  /**
   * Return the raw JSON pieces buffer. The buffer will be cleared after this
   * call.
   */
  std::string getAndCleanBuffer();

  /**
   * Clear the raw JSON pieces buffer.
   */
  void clearBuffer() { raw_json_buffer_.clear(); }

private:
  std::string raw_json_buffer_;
  std::string sanitize_buffer_;
};

} // namespace Json
} // namespace Envoy
