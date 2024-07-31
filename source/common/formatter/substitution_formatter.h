#pragma once

#include <bitset>
#include <cstddef>
#include <functional>
#include <list>
#include <regex>
#include <string>
#include <vector>

#include "envoy/common/time.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/formatter/substitution_formatter.h"
#include "envoy/stream_info/stream_info.h"

#include "google/protobuf/struct.pb.h"
#include "source/common/common/utility.h"
#include "source/common/formatter/http_formatter_context.h"
#include "source/common/json/json_loader.h"
#include "source/common/json/json_sanitizer.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Formatter {

/**
 * FormatterProvider for string literals. It ignores headers and stream info and returns string by
 * which it was initialized.
 */
template <class FormatterContext>
class PlainStringFormatterBase : public FormatterProviderBase<FormatterContext> {
public:
  PlainStringFormatterBase(const std::string& str) { str_.set_string_value(str); }

  // FormatterProviderBase
  absl::optional<std::string> formatWithContext(const FormatterContext&,
                                                const StreamInfo::StreamInfo&) const override {
    return str_.string_value();
  }
  ProtobufWkt::Value formatValueWithContext(const FormatterContext&,
                                            const StreamInfo::StreamInfo&) const override {
    return str_;
  }

private:
  ProtobufWkt::Value str_;
};

/**
 * FormatterProvider for numbers.
 */
template <class FormatterContext>
class PlainNumberFormatterBase : public FormatterProviderBase<FormatterContext> {
public:
  PlainNumberFormatterBase(double num) { num_.set_number_value(num); }

  // FormatterProviderBase
  absl::optional<std::string> formatWithContext(const FormatterContext&,
                                                const StreamInfo::StreamInfo&) const override {
    std::string str = absl::StrFormat("%g", num_.number_value());
    return str;
  }
  ProtobufWkt::Value formatValueWithContext(const FormatterContext&,
                                            const StreamInfo::StreamInfo&) const override {
    return num_;
  }

private:
  ProtobufWkt::Value num_;
};

/**
 * FormatterProvider based on StreamInfo fields.
 */
template <class FormatterContext>
class StreamInfoFormatterWrapper : public FormatterProviderBase<FormatterContext> {
public:
  StreamInfoFormatterWrapper(StreamInfoFormatterProviderPtr formatter)
      : formatter_(std::move(formatter)) {}

  // FormatterProvider
  absl::optional<std::string>
  formatWithContext(const FormatterContext&,
                    const StreamInfo::StreamInfo& stream_info) const override {
    return formatter_->format(stream_info);
  }
  ProtobufWkt::Value
  formatValueWithContext(const FormatterContext&,
                         const StreamInfo::StreamInfo& stream_info) const override {
    return formatter_->formatValue(stream_info);
  }

protected:
  StreamInfoFormatterProviderPtr formatter_;
};

/**
 * Access log format parser.
 */
class SubstitutionFormatParser {
public:
  template <class FormatterContext = HttpFormatterContext>
  static std::vector<FormatterProviderBasePtr<FormatterContext>>
  parse(absl::string_view format,
        const std::vector<CommandParserBasePtr<FormatterContext>>& command_parsers = {}) {
    std::string current_token;
    std::vector<FormatterProviderBasePtr<FormatterContext>> formatters;

    for (size_t pos = 0; pos < format.size(); ++pos) {
      if (format[pos] != '%') {
        current_token += format[pos];
        continue;
      }

      // escape '%%'
      if (format.size() > pos + 1) {
        if (format[pos + 1] == '%') {
          current_token += '%';
          pos++;
          continue;
        }
      }

      if (!current_token.empty()) {
        formatters.emplace_back(FormatterProviderBasePtr<FormatterContext>{
            new PlainStringFormatterBase<FormatterContext>(current_token)});
        current_token = "";
      }

      std::smatch m;
      const std::string search_space = std::string(format.substr(pos));
      if (!std::regex_search(search_space, m, commandWithArgsRegex())) {
        throwEnvoyExceptionOrPanic(
            fmt::format("Incorrect configuration: {}. Couldn't find valid command at position {}",
                        format, pos));
      }

      const std::string match = m.str(0);
      // command is at at index 1.
      const std::string command = m.str(1);
      // subcommand is at index 2.
      const std::string subcommand = m.str(2);
      // optional length is at index 3. If present, validate that it is valid integer.
      absl::optional<size_t> max_length;
      if (m.str(3).length() != 0) {
        size_t length_value;
        if (!absl::SimpleAtoi(m.str(3), &length_value)) {
          throwEnvoyExceptionOrPanic(absl::StrCat("Length must be an integer, given: ", m.str(3)));
        }
        max_length = length_value;
      }
      std::vector<std::string> path;

      const size_t command_end_position = pos + m.str(0).length() - 1;

      bool added = false;

      // The order of the following parsers is because the historical behavior. And we keep it
      // for backward compatibility.

      // First, try the built-in command parsers.
      for (const auto& cmd :
           BuiltInCommandParserFactoryHelper<FormatterContext>::commandParsers()) {
        auto formatter = cmd->parse(command, subcommand, max_length);
        if (formatter) {
          formatters.push_back(std::move(formatter));
          added = true;
          break;
        }
      }

      // Next, try the command parsers provided by the user.
      if (!added) {
        for (const auto& cmd : command_parsers) {
          auto formatter = cmd->parse(command, subcommand, max_length);
          if (formatter) {
            formatters.push_back(std::move(formatter));
            added = true;
            break;
          }
        }
      }

      // Finally, try the command parsers that are built-in and context-independent.
      if (!added) {
        for (const auto& cmd : BuiltInStreamInfoCommandParserFactoryHelper::commandParsers()) {
          auto formatter = cmd->parse(command, subcommand, max_length);
          if (formatter) {
            formatters.push_back(std::make_unique<StreamInfoFormatterWrapper<FormatterContext>>(
                std::move(formatter)));
            added = true;
            break;
          }
        }
      }

      if (!added) {
        throwEnvoyExceptionOrPanic(fmt::format("Not supported field in StreamInfo: {}", command));
      }

      pos = command_end_position;
    }

    if (!current_token.empty() || format.empty()) {
      // Create a PlainStringFormatter with the final string literal. If the format string
      // was empty, this creates a PlainStringFormatter with an empty string.
      formatters.emplace_back(FormatterProviderBasePtr<FormatterContext>{
          new PlainStringFormatterBase<FormatterContext>(current_token)});
    }

    return formatters;
  }

private:
  static const std::regex& commandWithArgsRegex();
};

inline constexpr absl::string_view DefaultUnspecifiedValueStringView = "-";

/**
 * Composite formatter implementation.
 */
template <class FormatterContext> class FormatterBaseImpl : public FormatterBase<FormatterContext> {
public:
  using CommandParsers = std::vector<CommandParserBasePtr<FormatterContext>>;

  FormatterBaseImpl(absl::string_view format, bool omit_empty_values = false)
      : empty_value_string_(omit_empty_values ? absl::string_view{}
                                              : DefaultUnspecifiedValueStringView) {
    providers_ = SubstitutionFormatParser::parse<FormatterContext>(format);
  }
  FormatterBaseImpl(absl::string_view format, bool omit_empty_values,
                    const CommandParsers& command_parsers)
      : empty_value_string_(omit_empty_values ? absl::string_view{}
                                              : DefaultUnspecifiedValueStringView) {
    providers_ = SubstitutionFormatParser::parse<FormatterContext>(format, command_parsers);
  }

  // FormatterBase
  std::string formatWithContext(const FormatterContext& context,
                                const StreamInfo::StreamInfo& stream_info) const override {
    std::string log_line;
    log_line.reserve(256);

    for (const auto& provider : providers_) {
      const auto bit = provider->formatWithContext(context, stream_info);
      log_line += bit.value_or(empty_value_string_);
    }

    return log_line;
  }

private:
  const std::string empty_value_string_;
  std::vector<FormatterProviderBasePtr<FormatterContext>> providers_;
};

// Helper class to parse the Json format configuration.
class JsonFormatBuilder {
public:
  static constexpr absl::string_view BoolFalse = "false";
  static constexpr absl::string_view BoolTrue = "true";
  static constexpr absl::string_view NullValue = "null";

  struct JsonString {
    std::string value_;
  };
  struct TmplString {
    std::string value_;
  };
  using FormatterEelements = std::vector<absl::variant<JsonString, TmplString>>;

  /**
   * Constructor of JsonFormatBuilder.
   * @param sort_properties whether to sort the properties in the JSON. If false
   * the default order of the proto struct will be used.
   * @param keep_value_type whether to keep the value type in JSON. If false
   * then all values will be converted to string.
   */
  explicit JsonFormatBuilder(bool sort_properties, bool keep_value_type)
      : sort_properties_(sort_properties), keep_value_type_(keep_value_type) {}

  /**
   * Convert a proto struct format configuration to a list of raw JSON string and
   * substitution template string.
   * Given a proto struct format configuration, it will be parsed to a list of raw
   * string and substitution template string.
   *
   * For example given the following proto struct format configuration:
   *
   *   json_format:
   *     text: "text"
   *     tmpl: "%START_TIME%"
   *     number: 2
   *     bool: true
   *     list:
   *       - "list_raw_value"
   *       - false
   *       - "%EMIT_TIME%"
   *     nested:
   *       text: "nested_text"
   *
   * It will be parsed to the following list:
   *
   *   - '{"text":"text","tmpl":'
   *   - '%START_TIME%'
   *   - ',"number":2,"bool":true,"list":["list_raw_value",false,'
   *   - '%EMIT_TIME%'
   *   - '],"nested":{"text":"nested_text"}}'
   *
   * Finally the substitution template string will be parsed to a list of substitution
   * formatters. Then join the raw string and output of substitution formatters in order
   * to get the final output.
   *
   * @param struct_format the proto struct format configuration.
   */
  FormatterEelements fromStruct(const ProtobufWkt::Struct& struct_format) const;

private:
  void formatValueToFormatterEelements(const ProtobufWkt::Struct& value) const;
  void formatValueToFormatterEelements(const ProtobufWkt::Value& value) const;

  const bool sort_properties_{};
  const bool keep_value_type_{};

  mutable std::string sanitize_buffer_; // Buffer for sanitizing strings.
  mutable std::string join_raw_string_; // Buffer for joining raw strings.
  mutable FormatterEelements output_;   // Final output.
};

template <class FormatterContext>
class NewJsonFormatterImplBase : public FormatterBase<FormatterContext> {
public:
  using CommandParsers = std::vector<CommandParserBasePtr<FormatterContext>>;
  using Formatters = std::vector<FormatterProviderBasePtr<FormatterContext>>;

  NewJsonFormatterImplBase(const ProtobufWkt::Struct& struct_format, bool keep_value_type,
                           bool omit_empty_values, bool sort_properties,
                           const CommandParsers& commands = {})
      : omit_empty_values_(omit_empty_values), keep_value_type_(keep_value_type),
        empty_value_(omit_empty_values_ ? std::string()
                                        : std::string(DefaultUnspecifiedValueStringView)) {
    JsonFormatBuilder builder(sort_properties, keep_value_type);
    auto elements = builder.fromStruct(struct_format);
    for (auto& element : elements) {
      if (absl::holds_alternative<JsonFormatBuilder::TmplString>(element)) {
        elements_.emplace_back(SubstitutionFormatParser::parse<FormatterContext>(
            absl::get<JsonFormatBuilder::TmplString>(element).value_, commands));
      } else {
        elements_.emplace_back(absl::get<JsonFormatBuilder::JsonString>(element).value_);
      }
    }
  }

  std::string formatWithContext(const FormatterContext& context,
                                const StreamInfo::StreamInfo& info) const {
    std::string log_line;
    log_line.reserve(512);

    std::string sanitize_buffer;

    for (const auto& element : elements_) {
      // 1. Handle the raw string element.
      if (absl::holds_alternative<std::string>(element)) {
        log_line.append(absl::get<std::string>(element));
        continue;
      }
      ASSERT(absl::holds_alternative<Formatters>(element));

      const auto& formatters = absl::get<Formatters>(element);
      ASSERT(!formatters.empty());

      // 2. Handle the formatter element with multiple providers or case
      //    that value type needs not to be kept.
      if (formatters.size() > 1 || !keep_value_type_) {
        // Multiple providers forces string output.
        log_line.push_back('"');
        for (const auto& provider : formatters) {
          const auto bit = provider->formatWithContext(context, info);
          if (!bit.has_value()) {
            log_line.append(empty_value_);
            continue;
          }
          log_line.append(Json::sanitize(sanitize_buffer, bit.value()));
        }
        log_line.push_back('"');

        continue;
      }

      // 4. Handle the formatter element with a single provider and value
      //    type needs to be kept.
      auto value = formatters[0]->formatValueWithContext(context, info);
      switch (value.kind_case()) {
      case ProtobufWkt::Value::KIND_NOT_SET:
      case ProtobufWkt::Value::kNullValue:
        log_line.append(JsonFormatBuilder::NullValue);
        break;
      case ProtobufWkt::Value::kNumberValue:
        log_line.append(fmt::to_string(value.number_value()));
        break;
      case ProtobufWkt::Value::kStringValue:
        log_line.push_back('"');
        log_line.append(Json::sanitize(sanitize_buffer, value.string_value()));
        log_line.push_back('"');
        break;
      case ProtobufWkt::Value::kBoolValue:
        log_line.append(value.bool_value() ? JsonFormatBuilder::BoolTrue
                                           : JsonFormatBuilder::BoolFalse);
        break;
      case ProtobufWkt::Value::kStructValue:
      case ProtobufWkt::Value::kListValue:
        // Convert the struct or list to string. This may result in a performance
        // degradation. But We rarely hit this case.
        // Basically only metadata or filter state formatter may hit this case.
#ifdef ENVOY_ENABLE_YAML
        absl::StatusOr<std::string> json_or =
            MessageUtil::getJsonStringFromMessage(value, false, true);
        if (json_or.ok()) {
          log_line.append(json_or.value());
        } else {
          log_line.push_back('"');
          log_line.append(Json::sanitize(sanitize_buffer, json_or.status().message()));
          log_line.push_back('"');
        }
#else
        IS_ENVOY_BUG("Json support compiled out");
        log_line.append(R"EOF("Json support compiled out")EOF");
#endif
        break;
      }
    }

    log_line.push_back('\n');
    return log_line;
  }

private:
  const bool omit_empty_values_{};
  const bool keep_value_type_{};
  const std::string empty_value_;

  std::vector<absl::variant<std::string, Formatters>> elements_;
};

using NewJsonFormatterImpl = NewJsonFormatterImplBase<HttpFormatterContext>;

// Helper classes for StructFormatter::StructFormatMapVisitor.
template <class... Ts> struct StructFormatMapVisitorHelper : Ts... {
  using Ts::operator()...;
};
template <class... Ts> StructFormatMapVisitorHelper(Ts...) -> StructFormatMapVisitorHelper<Ts...>;

/**
 * An formatter for structured log formats, which returns a Struct proto that
 * can be converted easily into multiple formats.
 */
template <class FormatterContext> class StructFormatterBase {
public:
  using CommandParsers = std::vector<CommandParserBasePtr<FormatterContext>>;
  using PlainNumber = PlainNumberFormatterBase<FormatterContext>;
  using PlainString = PlainStringFormatterBase<FormatterContext>;

  StructFormatterBase(const ProtobufWkt::Struct& format_mapping, bool preserve_types,
                      bool omit_empty_values, const CommandParsers& commands = {})
      : omit_empty_values_(omit_empty_values), preserve_types_(preserve_types),
        empty_value_(omit_empty_values_ ? std::string()
                                        : std::string(DefaultUnspecifiedValueStringView)),
        struct_output_format_(FormatBuilder(commands).toFormatMapValue(format_mapping)) {}

  ProtobufWkt::Struct formatWithContext(const FormatterContext& context,
                                        const StreamInfo::StreamInfo& info) const {
    StructFormatMapVisitor visitor{
        [&](const std::vector<FormatterProviderBasePtr<FormatterContext>>& providers) {
          return providersCallback(providers, context, info);
        },
        [&, this](const StructFormatterBase::StructFormatMapWrapper& format_map) {
          return structFormatMapCallback(format_map, visitor);
        },
        [&, this](const StructFormatterBase::StructFormatListWrapper& format_list) {
          return structFormatListCallback(format_list, visitor);
        },
    };
    return structFormatMapCallback(struct_output_format_, visitor).struct_value();
  }

private:
  struct StructFormatMapWrapper;
  struct StructFormatListWrapper;
  using StructFormatValue =
      absl::variant<const std::vector<FormatterProviderBasePtr<FormatterContext>>,
                    const StructFormatMapWrapper, const StructFormatListWrapper>;
  // Although not required for Struct/JSON, it is nice to have the order of
  // properties preserved between the format and the log entry, thus std::map.
  using StructFormatMap = std::map<std::string, StructFormatValue>;
  using StructFormatMapPtr = std::unique_ptr<StructFormatMap>;
  struct StructFormatMapWrapper {
    StructFormatMapPtr value_;
  };

  using StructFormatList = std::list<StructFormatValue>;
  using StructFormatListPtr = std::unique_ptr<StructFormatList>;
  struct StructFormatListWrapper {
    StructFormatListPtr value_;
  };

  using StructFormatMapVisitor = StructFormatMapVisitorHelper<
      const std::function<ProtobufWkt::Value(
          const std::vector<FormatterProviderBasePtr<FormatterContext>>&)>,
      const std::function<ProtobufWkt::Value(const StructFormatterBase::StructFormatMapWrapper&)>,
      const std::function<ProtobufWkt::Value(const StructFormatterBase::StructFormatListWrapper&)>>;

  // Methods for building the format map.
  class FormatBuilder {
  public:
    explicit FormatBuilder(const CommandParsers& commands) : commands_(commands) {}
    std::vector<FormatterProviderBasePtr<FormatterContext>>
    toFormatStringValue(const std::string& string_format) const {
      return SubstitutionFormatParser::parse<FormatterContext>(string_format, commands_);
    }
    std::vector<FormatterProviderBasePtr<FormatterContext>>
    toFormatNumberValue(double value) const {
      std::vector<FormatterProviderBasePtr<FormatterContext>> formatters;
      formatters.emplace_back(FormatterProviderBasePtr<FormatterContext>{new PlainNumber(value)});
      return formatters;
    }
    StructFormatMapWrapper toFormatMapValue(const ProtobufWkt::Struct& struct_format) const {
      auto output = std::make_unique<StructFormatMap>();
      for (const auto& pair : struct_format.fields()) {
        switch (pair.second.kind_case()) {
        case ProtobufWkt::Value::kStringValue:
          output->emplace(pair.first, toFormatStringValue(pair.second.string_value()));
          break;

        case ProtobufWkt::Value::kStructValue:
          output->emplace(pair.first, toFormatMapValue(pair.second.struct_value()));
          break;

        case ProtobufWkt::Value::kListValue:
          output->emplace(pair.first, toFormatListValue(pair.second.list_value()));
          break;

        case ProtobufWkt::Value::kNumberValue:
          output->emplace(pair.first, toFormatNumberValue(pair.second.number_value()));
          break;
        default:
          throwEnvoyExceptionOrPanic(
              "Only string values, nested structs, list values and number values are "
              "supported in structured access log format.");
        }
      }
      return {std::move(output)};
    }
    StructFormatListWrapper
    toFormatListValue(const ProtobufWkt::ListValue& list_value_format) const {
      auto output = std::make_unique<StructFormatList>();
      for (const auto& value : list_value_format.values()) {
        switch (value.kind_case()) {
        case ProtobufWkt::Value::kStringValue:
          output->emplace_back(toFormatStringValue(value.string_value()));
          break;

        case ProtobufWkt::Value::kStructValue:
          output->emplace_back(toFormatMapValue(value.struct_value()));
          break;

        case ProtobufWkt::Value::kListValue:
          output->emplace_back(toFormatListValue(value.list_value()));
          break;

        case ProtobufWkt::Value::kNumberValue:
          output->emplace_back(toFormatNumberValue(value.number_value()));
          break;

        default:
          throwEnvoyExceptionOrPanic(
              "Only string values, nested structs, list values and number values are "
              "supported in structured access log format.");
        }
      }
      return {std::move(output)};
    }

  private:
    const CommandParsers& commands_;
  };

  // Methods for doing the actual formatting.
  ProtobufWkt::Value
  providersCallback(const std::vector<FormatterProviderBasePtr<FormatterContext>>& providers,
                    const FormatterContext& context,
                    const StreamInfo::StreamInfo& stream_info) const {
    ASSERT(!providers.empty());
    if (providers.size() == 1) {
      const auto& provider = providers.front();
      if (preserve_types_) {
        return provider->formatValueWithContext(context, stream_info);
      }

      if (omit_empty_values_) {
        return ValueUtil::optionalStringValue(provider->formatWithContext(context, stream_info));
      }

      const auto str = provider->formatWithContext(context, stream_info);
      return ValueUtil::stringValue(str.value_or(empty_value_));
    }
    // Multiple providers forces string output.
    std::string str;
    for (const auto& provider : providers) {
      const auto bit = provider->formatWithContext(context, stream_info);
      str += bit.value_or(empty_value_);
    }
    return ValueUtil::stringValue(str);
  }
  ProtobufWkt::Value
  structFormatMapCallback(const StructFormatterBase::StructFormatMapWrapper& format_map,
                          const StructFormatMapVisitor& visitor) const {
    ProtobufWkt::Struct output;
    auto* fields = output.mutable_fields();
    for (const auto& pair : *format_map.value_) {
      ProtobufWkt::Value value = absl::visit(visitor, pair.second);
      if (omit_empty_values_ && value.kind_case() == ProtobufWkt::Value::kNullValue) {
        continue;
      }
      (*fields)[pair.first] = value;
    }
    if (omit_empty_values_ && output.fields().empty()) {
      return ValueUtil::nullValue();
    }
    return ValueUtil::structValue(output);
  }
  ProtobufWkt::Value
  structFormatListCallback(const StructFormatterBase::StructFormatListWrapper& format_list,
                           const StructFormatMapVisitor& visitor) const {
    std::vector<ProtobufWkt::Value> output;
    for (const auto& val : *format_list.value_) {
      ProtobufWkt::Value value = absl::visit(visitor, val);
      if (omit_empty_values_ && value.kind_case() == ProtobufWkt::Value::kNullValue) {
        continue;
      }
      output.push_back(value);
    }
    return ValueUtil::listValue(output);
  }

  const bool omit_empty_values_;
  const bool preserve_types_;
  const std::string empty_value_;

  const StructFormatMapWrapper struct_output_format_;
};

template <class FormatterContext>
using StructFormatterBasePtr = std::unique_ptr<StructFormatterBase<FormatterContext>>;

template <class FormatterContext>
class JsonFormatterBaseImpl : public FormatterBase<FormatterContext> {
public:
  using CommandParsers = std::vector<CommandParserBasePtr<FormatterContext>>;

  JsonFormatterBaseImpl(const ProtobufWkt::Struct& format_mapping, bool preserve_types,
                        bool omit_empty_values, bool sort_properties,
                        const CommandParsers& commands = {})
      : struct_formatter_(format_mapping, preserve_types, omit_empty_values, commands),
        sort_properties_(sort_properties) {}

  // FormatterBase
  std::string formatWithContext(const FormatterContext& context,
                                const StreamInfo::StreamInfo& info) const override {
    const ProtobufWkt::Struct output_struct = struct_formatter_.formatWithContext(context, info);

    std::string log_line = "";
#ifdef ENVOY_ENABLE_YAML
    if (sort_properties_) {
      log_line = Json::Factory::loadFromProtobufStruct(output_struct)->asJsonString();
    } else {
      log_line = MessageUtil::getJsonStringFromMessageOrError(output_struct, false, true);
    }
#else
    UNREFERENCED_PARAMETER(sort_properties_);
    IS_ENVOY_BUG("Json support compiled out");
#endif
    return absl::StrCat(log_line, "\n");
  }

private:
  const StructFormatterBase<FormatterContext> struct_formatter_;
  const bool sort_properties_;
};

using StructFormatter = StructFormatterBase<HttpFormatterContext>;
using StructFormatterPtr = std::unique_ptr<StructFormatter>;

// Aliases for backwards compatibility.
using FormatterImpl = FormatterBaseImpl<HttpFormatterContext>;
using JsonFormatterImpl = JsonFormatterBaseImpl<HttpFormatterContext>;

} // namespace Formatter
} // namespace Envoy
