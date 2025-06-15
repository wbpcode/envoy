#include "source/common/formatter/substitution_formatter.h"

namespace Envoy {
namespace Formatter {

const re2::RE2& commandWithArgsRegex() {
  // The following regex is used to check validity of the formatter command and to
  // extract groups.
  // The formatter command has the following format:
  //    % COMMAND(SUBCOMMAND):LENGTH%
  // % signs at the beginning and end are used by parser to find next COMMAND.
  // COMMAND must always be present and must consist of characters: "A-Z", "0-9" or "_".
  // SUBCOMMAND presence depends on the COMMAND. Format is flexible but cannot contain ")".:
  // - for some commands SUBCOMMAND is not allowed (for example %PROTOCOL%)
  // - for some commands SUBCOMMAND is required (for example %REQ(:AUTHORITY)%, just %REQ% will
  // cause error)
  // - for some commands SUBCOMMAND is optional (for example %START_TIME% and
  // %START_TIME(%f.%1f.%2f.%3f)% are both correct).
  // LENGTH presence depends on the command. Some
  // commands allow LENGTH to be specified, so not. Regex is used to validate the syntax and also
  // to extract values for COMMAND, SUBCOMMAND and LENGTH.
  //
  // Below is explanation of capturing and non-capturing groups. Non-capturing groups are used
  // to specify that certain part of the formatter command is optional and should contain specific
  // characters. Capturing groups are used to extract the values when regex is matched against
  // formatter command string.
  //
  // clang-format off
  // Non-capturing group specifying optional :LENGTH ----------------------
  //                                                                       |
  // Non-capturing group specifying optional (SUBCOMMAND)---               |
  //                                                        |              |
  // Non-capturing group specifying mandatory COMMAND       |              |
  //  which uses only A-Z, 0-9 and _ characters             |              |
  //  Group is used only to specify allowed characters.     |              |
  //                                      |                 |              |
  //                                      |                 |              |
  //                              _________________  _____________ _____________
  //                              |               |  |           | |           |
  CONSTRUCT_ON_FIRST_USE(re2::RE2,
                         R"EOF(^%((?:[A-Z]|[0-9]|_)+)(?:\((.*?)\))?(?::([0-9]+))?%)EOF");
  //                             |__________________|     |___|        |______|
  //                                       |                |              |
  // Capturing group specifying COMMAND ---                 |              |
  // The index of this group is 1.                          |              |
  //                                                        |              |
  // Capturing group for SUBCOMMAND. If present, it will ---               |
  // contain SUBCOMMAND without "(" and ")". The index                     |
  // of SUBCOMMAND group is 2.                                             |
  //                                                                       |
  // Capturing group for LENGTH. If present, it will ----------------------
  // contain just number without ":". The index of
  // LENGTH group is 3.
  // clang-format on
}

// Helper class to write value to output buffer in JSON style.
class JsonStringSerializer {
public:
  using ElementType = JsonFormatterImpl::ElementType;
  struct FormatElement {
    ElementType type_;

    // Pre-sanitized JSON piece or a format template string that contains
    // substitution commands.
    std::string value_;
    // Whether the value is a template string.
    // If true, the value is a format template string that contains substitution commands.
    // If false, the value is a pre-sanitized JSON piece.
    bool is_template_;
  };
  using FormatElements = std::vector<FormatElement>;

  JsonStringSerializer() { elements_.reserve(64); }

  void addMapBeginDelimiter() { elements_.emplace_back(ElementType::Delim, "{"); }
  void addMapEndDelimiter() { elements_.emplace_back(ElementType::Delim, "}"); }
  void addArrayBeginDelimiter() { elements_.emplace_back(ElementType::Delim, "["); }
  void addArrayEndDelimiter() { elements_.emplace_back(ElementType::Delim, "]"); }
  void addElementsDelimiter() { elements_.emplace_back(ElementType::Delim, ","); }

  // Serializes a key.
  void addKey(absl::string_view key) {
    std::string sanitized_key = sanitize(R"(")", key, R"(":)");
    elements_.emplace_back(ElementType::Key, std::move(sanitized_key));
  }

  // Serializes a value.
  void addString(absl::string_view value) {
    // If the value contains '%' then it may be substitution formatter and
    // we should not sanitize it and only store the raw string first.
    // Then the raw string will be parsed by the JsonFormatterImpl.
    if (absl::StrContains(value, '%')) {
      elements_.emplace_back(ElementType::Value, std::string(value), true);
      return;
    }

    std::string sanitized_value = sanitize(R"(")", value, R"(")");
    elements_.emplace_back(ElementType::Value, std::move(sanitized_value));
  }
  void addNumber(double d) {
    std::string value;
    if (std::isnan(d)) {
      value = std::string(Json::Constants::Null);
    } else {
      value = fmt::to_string(d);
    }
    elements_.emplace_back(ElementType::Value, std::move(value));
  }
  void addBool(bool b) {
    elements_.emplace_back(ElementType::Value,
                           std::string(b ? Json::Constants::True : Json::Constants::False));
  }
  void addNull() { elements_.emplace_back(ElementType::Value, std::string(Json::Constants::Null)); }

  // Low-level methods that be used to provide a low-level control to buffer.
  std::string sanitize(absl::string_view prefix, absl::string_view value,
                       absl::string_view suffix) {
    return absl::StrCat(prefix, Json::sanitize(sanitize_buffer_, value), suffix);
  }

  std::string sanitize_buffer_;
  FormatElements elements_;
};

// Helper class to parse the Json format configuration. The class will be used to parse
// the JSON format configuration and convert it to a list of raw JSON pieces and
// substitution format template strings. See comments below for more details.
class JsonFormatBuilder {
public:
  /**
   * Constructor of JsonFormatBuilder.
   */
  JsonFormatBuilder() = default;

  /**
   * Convert a proto struct format configuration to an array of raw JSON elements and
   * substitution format template strings.
   *
   * The keys, raw values, delimiters will be serialized as JSON string elements (raw
   * JSON strings) directly when loading the configuration.
   * The substitution format template strings will be kept as template string elements and
   * will be parsed to formatter providers by the JsonFormatter.
   *
   * NOTE: This class is used to parse the configuration of the proto struct format
   * and should only be used in the context of parsing the configuration.
   *
   * For example given the following proto struct format configuration:
   *
   *   json_format:
   *     key: "value"
   *     format: "%START_TIME%"
   *     number: 2
   *     nested:
   *       nested_key: "nested_value"
   *
   * It will be parsed to the following elements array:
   *
   *   - {                   # Dedimiter.
   *   - "key":              # Key.
   *   - "value"             # Value.
   *   - ,                   # Delimiter.
   *   - "format":           # Key.
   *   - %START_TIME%        # Value with substitution formatter.
   *   - ,                   # Delimiter.
   *   - "number":           # Key.
   *   - 2                   # Value.
   *   - "nested":           # Key.
   *   - {                   # Delimiter.
   *   - "nested_key":       # Key.
   *   - "nested_value"      # Value.
   *   - }                   # Delimiter.
   *   - }                   # Delimiter.
   *
   * @param struct_format the proto struct format configuration.
   */
  JsonStringSerializer::FormatElements fromStruct(const ProtobufWkt::Struct& struct_format) {
    // This call will iterate through the map tree and serialize the key/values as JSON.
    // If a string value that contains a substitution commands is found, the current
    // JSON piece and the substitution command will be pushed into the output list.
    // After that, the iteration will continue until the whole tree is traversed.
    formatValueToFormatElements(struct_format.fields());
    return std::move(serializer_.elements_);
  }

private:
  using ProtoDict = Protobuf::Map<std::string, ProtobufWkt::Value>;
  using ProtoList = Protobuf::RepeatedPtrField<ProtobufWkt::Value>;

  void formatValueToFormatElements(const ProtoDict& dict_value);
  void formatValueToFormatElements(const ProtobufWkt::Value& value);
  void formatValueToFormatElements(const ProtoList& list_value);

  JsonStringSerializer serializer_; // JSON serializer.
};

void JsonFormatBuilder::formatValueToFormatElements(const ProtobufWkt::Value& value) {
  switch (value.kind_case()) {
  case ProtobufWkt::Value::KIND_NOT_SET:
  case ProtobufWkt::Value::kNullValue:
    serializer_.addNull();
    break;
  case ProtobufWkt::Value::kNumberValue:
    serializer_.addNumber(value.number_value());
    break;
  case ProtobufWkt::Value::kStringValue:
    serializer_.addString(value.string_value());
    break;
  case ProtobufWkt::Value::kBoolValue:
    serializer_.addBool(value.bool_value());
    break;
  case ProtobufWkt::Value::kStructValue: {
    formatValueToFormatElements(value.struct_value().fields());
    break;
  case ProtobufWkt::Value::kListValue:
    formatValueToFormatElements(value.list_value().values());
    break;
  }
  }
}

void JsonFormatBuilder::formatValueToFormatElements(const ProtoList& list_value) {
  serializer_.addArrayBeginDelimiter(); // Delimiter to start list.
  for (int i = 0; i < list_value.size(); ++i) {
    if (i > 0) {
      serializer_.addElementsDelimiter(); // Delimiter to separate list elements.
    }
    formatValueToFormatElements(list_value[i]);
  }
  serializer_.addArrayEndDelimiter(); // Delimiter to end list.
}

void JsonFormatBuilder::formatValueToFormatElements(const ProtoDict& dict_value) {
  std::vector<std::pair<absl::string_view, ProtoDict::const_iterator>> sorted_fields;
  sorted_fields.reserve(dict_value.size());

  for (auto it = dict_value.begin(); it != dict_value.end(); ++it) {
    sorted_fields.push_back({it->first, it});
  }

  // Sort the keys to make the output deterministic.
  std::sort(sorted_fields.begin(), sorted_fields.end(),
            [](const auto& a, const auto& b) { return a.first < b.first; });

  serializer_.addMapBeginDelimiter(); // Delimiter to start map.
  for (size_t i = 0; i < sorted_fields.size(); ++i) {
    if (i > 0) {
      serializer_.addElementsDelimiter(); // Delimiter to separate map elements.
    }
    // Add the key.
    serializer_.addKey(sorted_fields[i].first);
    formatValueToFormatElements(sorted_fields[i].second->second);
  }
  serializer_.addMapEndDelimiter(); // Delimiter to end map.
}

absl::StatusOr<std::vector<FormatterProviderPtr>>
SubstitutionFormatParser::parse(absl::string_view format,
                                const std::vector<CommandParserPtr>& command_parsers) {
  std::string current_token;
  current_token.reserve(32);
  std::vector<FormatterProviderPtr> formatters;

  for (size_t pos = 0; pos < format.size();) {
    if (format[pos] != '%') {
      current_token.push_back(format[pos]);
      pos++;
      continue;
    }

    // escape '%%'
    if (format.size() > pos + 1) {
      if (format[pos + 1] == '%') {
        current_token.push_back('%');
        pos += 2;
        continue;
      }
    }

    if (!current_token.empty()) {
      formatters.emplace_back(FormatterProviderPtr{new PlainStringFormatter(current_token)});
      current_token.clear();
    }

    absl::string_view sub_format = format.substr(pos);
    const size_t sub_format_size = sub_format.size();

    absl::string_view command, command_arg;
    absl::optional<size_t> max_len;

    if (!re2::RE2::Consume(&sub_format, commandWithArgsRegex(), &command, &command_arg, &max_len)) {
      return absl::InvalidArgumentError(fmt::format(
          "Incorrect configuration: {}. Couldn't find valid command at position {}", format, pos));
    }

    bool added = false;

    // First try the command parsers provided by the user. This allows the user to override
    // built-in command parsers.
    for (const auto& cmd : command_parsers) {
      auto formatter = cmd->parse(command, command_arg, max_len);
      if (formatter) {
        formatters.push_back(std::move(formatter));
        added = true;
        break;
      }
    }

    // Next, try the built-in command parsers.
    if (!added) {
      for (const auto& cmd : BuiltInCommandParserFactoryHelper::commandParsers()) {
        auto formatter = cmd->parse(command, command_arg, max_len);
        if (formatter) {
          formatters.push_back(std::move(formatter));
          added = true;
          break;
        }
      }
    }

    if (!added) {
      return absl::InvalidArgumentError(
          fmt::format("Not supported field in StreamInfo: {}", command));
    }

    pos += (sub_format_size - sub_format.size());
  }

  if (!current_token.empty() || format.empty()) {
    // Create a PlainStringFormatter with the final string literal. If the format string
    // was empty, this creates a PlainStringFormatter with an empty string.
    formatters.emplace_back(FormatterProviderPtr{new PlainStringFormatter(current_token)});
  }

  return formatters;
}

absl::StatusOr<std::unique_ptr<FormatterImpl>>
FormatterImpl::create(absl::string_view format, bool omit_empty_values,
                      const CommandParsers& command_parsers) {
  absl::Status creation_status = absl::OkStatus();
  auto ret = std::unique_ptr<FormatterImpl>(
      new FormatterImpl(creation_status, format, omit_empty_values, command_parsers));
  RETURN_IF_NOT_OK_REF(creation_status);
  return ret;
}

std::string FormatterImpl::formatWithContext(const Context& context,
                                             const StreamInfo::StreamInfo& stream_info) const {
  std::string log_line;
  log_line.reserve(256);

  for (const auto& provider : providers_) {
    const absl::optional<std::string> bit = provider->formatWithContext(context, stream_info);
    // Add the formatted value if there is one. Otherwise add a default value
    // of "-" if omit_empty_values_ is not set.
    if (bit.has_value()) {
      log_line += bit.value();
    } else if (!omit_empty_values_) {
      log_line += DefaultUnspecifiedValueStringView;
    }
  }

  return log_line;
}

JsonFormatterImpl::JsonFormatterImpl(const ProtobufWkt::Struct& struct_format,
                                     bool omit_empty_values, const CommandParsers& commands)
    : omit_empty_values_(omit_empty_values) {

  std::vector<ParsedFormatElement> parsed_elements;

  for (auto& element : JsonFormatBuilder().fromStruct(struct_format)) {
    if (element.is_template_) {
      ASSERT(element.type_ == ElementType::Value);
      Formatters formatters = THROW_OR_RETURN_VALUE(
          SubstitutionFormatParser::parse(element.value_, commands), Formatters);
      parsed_elements.emplace_back(ElementType::Value, std::string{}, std::move(formatters));
      ASSERT(!parsed_elements.back().format_.empty());
    } else {
      parsed_elements.emplace_back(element.type_, std::move(element.value_));
      ASSERT(!parsed_elements.back().string_.empty());
    }
  }

  if (omit_empty_values) {
    parsed_elements_ = std::move(parsed_elements);

    for (const auto& e : parsed_elements_) {
      std::cout << e.string_ << std::endl;
    }

    return;
  }

  // If omit empty values is not set to true and we can concat all continuous keys/values/
  // delimiters to single one element to speed up the logging at runtime.
  for (ParsedFormatElement& element : parsed_elements) {
    if (!element.format_.empty()) {
      parsed_elements_.emplace_back(std::move(element));
      continue;
    }

    if (parsed_elements_.empty() || parsed_elements_.back().type_ != ElementType::Pieces) {
      parsed_elements_.emplace_back(std::move(element));
      parsed_elements_.back().type_ = ElementType::Pieces;
      continue;
    }

    parsed_elements_.back().string_ += element.string_;
  }

  for (const auto& e : parsed_elements_) {
    std::cout << e.string_ << std::endl;
  }
}

void JsonFormatterImpl::formatSubstitution(const Formatters& formatters, const Context& context,
                                           const StreamInfo::StreamInfo& info,
                                           std::string& log_line, std::string& sanitize) const {
  ASSERT(!formatters.empty());

  // 1. Handle the caseh where single formatter provider is provided.

  if (formatters.size() == 1) {
    Json::Utility::appendValueToString(formatters[0]->formatValueWithContext(context, info),
                                       log_line);
    return;
  }

  // 2. Handle the case where multiple formatter providers are provided.

  log_line.push_back('"');

  for (const FormatterProviderPtr& formatter : formatters) {
    const absl::optional<std::string> value = formatter->formatWithContext(context, info);
    if (!value.has_value()) {
      // Add the empty value. This needn't be sanitized.
      log_line.append(omit_empty_values_ ? EMPTY_STRING : DefaultUnspecifiedValueStringView);
      continue;
    }
    // Sanitize the string value and add it to the buffer. The string value will not be quoted
    // since we handle the quoting by ourselves at the outer level.
    log_line.append(Json::sanitize(sanitize, value.value()));
  }

  log_line.push_back('"'); // End the JSON string.
}

bool JsonFormatterImpl::formatKeyValuePair(const ParsedFormatElement& key,
                                           const ParsedFormatElement& val, const Context& context,
                                           const StreamInfo::StreamInfo& info,
                                           std::string& log_line, std::string& sanitize) const {

  // Note: this only happens when the omit_empty_values is set to true because at other
  // cases, keys/values/delimiters maybe concated and we cannot handle them in a pair.
  ASSERT(val.type_ == ElementType::Value);

  if (!val.string_.empty()) {
    if (val.string_ == Json::Constants::Null) {
      // Skip the pair which has null value.
      return true;
    }
    log_line.append(key.string_).append(val.string_);
    return false;
  }

  if (val.format_.size() != 1) {
    formatSubstitution(val.format_, context, info, log_line.append(key.string_), sanitize);
    return false;
  }

  const auto value = val.format_[0]->formatValueWithContext(context, info);
  if (value.kind_case() == ProtobufWkt::Value::KIND_NOT_SET ||
      value.kind_case() == ProtobufWkt::Value::kNullValue) {
    // Skip the pair which has null value.
    return true;
  }

  Json::Utility::appendValueToString(value, log_line.append(key.string_));
  return false;
}

void JsonFormatterImpl::formatOmitEmtpy(const Context& context, const StreamInfo::StreamInfo& info,
                                        std::string& log_line, std::string& sanitize) const {
  ASSERT(omit_empty_values_);

  for (size_t i = 0; i < parsed_elements_.size(); i++) {
    const auto& curr = parsed_elements_[i];

    if (curr.type_ == ElementType::Key) {
      // The key will never be the last element and will be followed by at least a value
      // and a delimiter.
      if (const auto& value = parsed_elements_[i + 1]; value.type_ == ElementType::Value) {
        // Handle the simple key/value pair.

        i++; // The value will be consumed anyway.
        if (formatKeyValuePair(curr, value, context, info, log_line, sanitize)) {
          const auto& delim = parsed_elements_[i + 1];
          ASSERT(delim.type_ == ElementType::Delim);
          if (delim.string_ == ",") {
            i++; // Because the pair is skipped then also skip the comma delimiter.
          }
        }
      } else {
        // The next elment is delimeter means the value of the key is map or list. For
        // this type, we will not handle it in pair but one by one.

        log_line.append(curr.string_);
      }
    } else {
      if (!curr.string_.empty()) {
        // Handle the string element. This should be value that not be part of simple
        // key/value pair or delimiter.
        ASSERT(curr.type_ == ElementType::Value || curr.type_ == ElementType::Delim);
        log_line.append(curr.string_);
      } else {
        // Handle the format element.  This should be value that not be part of simple
        // key/value pair.
        ASSERT(curr.type_ == ElementType::Value);
        ASSERT(!curr.format_.empty());
        formatSubstitution(curr.format_, context, info, log_line, sanitize);
      }
    }
  }
}

void JsonFormatterImpl::formatKeepEmpty(const Context& context, const StreamInfo::StreamInfo& info,
                                        std::string& log_line, std::string& sanitize) const {
  ASSERT(!omit_empty_values_);

  for (const ParsedFormatElement& element : parsed_elements_) {
    if (!element.string_.empty()) {
      // 1. Handle the string element.
      ASSERT(element.type_ == ElementType::Pieces);
      log_line.append(element.string_);
    } else {
      // 2. Handle the format element. Note: all non format value will be merged to pieces and
      // here will only handle the format value.
      ASSERT(element.type_ == ElementType::Value);
      ASSERT(!element.format_.empty());
      formatSubstitution(element.format_, context, info, log_line, sanitize);
    }
  }
}

std::string JsonFormatterImpl::formatWithContext(const Context& context,
                                                 const StreamInfo::StreamInfo& info) const {
  std::string log_line;
  log_line.reserve(2048);
  std::string sanitize;

  omit_empty_values_ ? formatOmitEmtpy(context, info, log_line, sanitize)
                     : formatKeepEmpty(context, info, log_line, sanitize);

  log_line.push_back('\n');
  return log_line;
}

} // namespace Formatter
} // namespace Envoy
