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

#include "source/common/common/utility.h"
#include "source/common/formatter/http_formatter_context.h"
#include "source/common/json/json_loader.h"

#if !defined(ENVOY_DISABLE_EXCEPTIONS)
#include "source/common/json/json_streamer.h"
#endif

#include "absl/types/optional.h"
#include "re2/re2.h"

namespace Envoy {
namespace Formatter {

/**
 * FormatterProvider for string literals. It ignores headers and stream info and returns string by
 * which it was initialized.
 */
template <class FormatterContext>
class PlainStringFormatterBase : public FormatterProviderBase<FormatterContext> {
public:
  PlainStringFormatterBase(absl::string_view str) { str_.set_string_value(str); }

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
    current_token.reserve(32);
    std::vector<FormatterProviderBasePtr<FormatterContext>> formatters;

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
        formatters.emplace_back(FormatterProviderBasePtr<FormatterContext>{
            new PlainStringFormatterBase<FormatterContext>(current_token)});
        current_token.clear();
      }

      absl::string_view sub_format = format.substr(pos);
      const size_t sub_format_size = sub_format.size();

      absl::string_view command, command_arg;
      absl::optional<size_t> max_len;

      if (!re2::RE2::Consume(&sub_format, commandWithArgsRegex(), &command, &command_arg,
                             &max_len)) {
        throwEnvoyExceptionOrPanic(
            fmt::format("Incorrect configuration: {}. Couldn't find valid command at position {}",
                        format, pos));
      }

      bool added = false;

      // The order of the following parsers is because the historical behavior. And we keep it
      // for backward compatibility.

      // First, try the built-in command parsers.
      for (const auto& cmd :
           BuiltInCommandParserFactoryHelper<FormatterContext>::commandParsers()) {
        auto formatter = cmd->parse(command, command_arg, max_len);
        if (formatter) {
          formatters.push_back(std::move(formatter));
          added = true;
          break;
        }
      }

      // Next, try the command parsers provided by the user.
      if (!added) {
        for (const auto& cmd : command_parsers) {
          auto formatter = cmd->parse(command, command_arg, max_len);
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
          auto formatter = cmd->parse(command, command_arg, max_len);
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

      pos += (sub_format_size - sub_format.size());
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
  static const re2::RE2& commandWithArgsRegex();
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

#if !defined(ENVOY_DISABLE_EXCEPTIONS)

using JsonSerializer = Json::Serializer<Json::StringOutput>;

// Helper class to parse the Json format configuration.
class JsonFormatBuilder {
public:
  struct JsonString {
    std::string value_;
  };
  struct TmplString {
    std::string value_;
  };
  using FormatElement = absl::variant<JsonString, TmplString>;
  using FormatElements = std::vector<FormatElement>;

  /**
   * Constructor of JsonFormatBuilder.
   * @param keep_value_type whether to keep the value type in JSON. If false
   * then all values will be converted to string.
   */
  explicit JsonFormatBuilder(bool keep_value_type) : keep_value_type_(keep_value_type) {}

  /**
   * Convert a proto struct format configuration to an array of raw JSON pieces and
   * substitution format template strings.
   *
   * The keys, raw values, delimiters will be serialized as JSON string pieces (raw
   * JSON strings) directly when loading the configuration.
   * The substitution format template strings will be kept as template string pieces and
   * will be parsed to formatter providers by the JsonFormatter.
   *
   * NOTE: This class is used to parse the configuration of the proto struct format
   * and should only be used in the context of parsing the configuration.
   *
   * For example given the following proto struct format configuration:
   *
   *   json_format:
   *     text: "text"
   *     template: "%START_TIME%"
   *     number: 2
   *     bool: true
   *     list:
   *       - "list_raw_value"
   *       - false
   *       - "%EMIT_TIME%"
   *     nested:
   *       text: "nested_text"
   *
   * It will be parsed to the following pieces:
   *
   *   - '{"text":"text","template":'                               # Raw JSON piece.
   *   - '%START_TIME%'                                             # Format template piece.
   *   - ',"number":2,"bool":true,"list":["list_raw_value",false,'  # Raw JSON piece.
   *   - '%EMIT_TIME%'                                              # Format template piece.
   *   - '],"nested":{"text":"nested_text"}}'                       # Raw JSON piece.
   *
   * Finally, join the raw JSON pieces and output of substitution formatters in order
   * to construct the final JSON output.
   *
   * @param struct_format the proto struct format configuration.
   */
  FormatElements fromStruct(const ProtobufWkt::Struct& struct_format);

private:
  using ProtoDict = Protobuf::Map<std::string, ProtobufWkt::Value>;
  using ProtoList = Protobuf::RepeatedPtrField<ProtobufWkt::Value>;

  void formatValueToFormatElements(const ProtoDict& dict_value);
  void formatValueToFormatElements(const ProtobufWkt::Value& value);
  void formatValueToFormatElements(const ProtoList& list_value);

  const bool keep_value_type_{};

  Json::StringOutput writer_;          // JSON writer buffer.
  JsonSerializer serializer_{writer_}; // JSON serializer.
  FormatElements output_;              // Output configuration.
};

template <class FormatterContext>
class JsonFormatterImplBase : public FormatterBase<FormatterContext> {
public:
  using CommandParsers = std::vector<CommandParserBasePtr<FormatterContext>>;
  using Formatter = FormatterProviderBasePtr<FormatterContext>;
  using Formatters = std::vector<Formatter>;

  JsonFormatterImplBase(const ProtobufWkt::Struct& struct_format, bool keep_value_type,
                        bool omit_empty_values, const CommandParsers& commands = {})
      : omit_empty_values_(omit_empty_values), keep_value_type_(keep_value_type),
        empty_value_(omit_empty_values_ ? std::string()
                                        : std::string(DefaultUnspecifiedValueStringView)) {

    JsonFormatBuilder::FormatElements elements =
        JsonFormatBuilder(keep_value_type).fromStruct(struct_format);

    for (JsonFormatBuilder::FormatElement& element : elements) {
      if (absl::holds_alternative<JsonFormatBuilder::TmplString>(element)) {
        parsed_elements_.emplace_back(SubstitutionFormatParser::parse<FormatterContext>(
            absl::get<JsonFormatBuilder::TmplString>(element).value_, commands));
      } else {
        parsed_elements_.emplace_back(
            std::move(absl::get<JsonFormatBuilder::JsonString>(element).value_));
      }
    }
  }

  std::string formatWithContext(const FormatterContext& context,
                                const StreamInfo::StreamInfo& info) const override {
    Json::StringOutput buffer;
    JsonSerializer serializer(buffer);

    for (const ParsedFormatElement& element : parsed_elements_) {
      // 1. Handle the raw string element.
      if (absl::holds_alternative<std::string>(element)) {
        // The raw string element will be added to the buffer directly.
        // It is sanitized when loading the configuration.
        buffer.buffer_.append(absl::get<std::string>(element));
        continue;
      }

      ASSERT(absl::holds_alternative<Formatters>(element));
      const Formatters& formatters = absl::get<Formatters>(element);
      ASSERT(!formatters.empty());

      // 2. Handle the formatter element with multiple providers or case
      //    that value type needs not to be kept.
      if (formatters.size() > 1 || !keep_value_type_) {
        stringValueToLogLine(formatters, context, info, buffer, serializer);
        continue;
      }

      // 4. Handle the formatter element with a single provider and value
      //    type needs to be kept.
      typedValueToLogLine(formatters, context, info, buffer, serializer);
    }

    buffer.buffer_.push_back('\n');
    return std::move(buffer.buffer_);
  }

private:
  void stringValueToLogLine(const Formatters& formatters, const FormatterContext& context,
                            const StreamInfo::StreamInfo& info, Json::StringOutput& buffer,
                            JsonSerializer& serializer) const {

    buffer.buffer_.push_back('"'); // Start the JSON string.
    for (const Formatter& formatter : formatters) {
      const absl::optional<std::string> value = formatter->formatWithContext(context, info);
      if (!value.has_value()) {
        // Add the empty value. This needn't be sanitized.
        buffer.buffer_.append(empty_value_);
        continue;
      }
      // Sanitize the string value and add it to the buffer. The string value will not be quoted
      // since we handle the quoting by ourselves at the outer level.
      serializer.addString(value.value(), {}, {});
    }
    buffer.buffer_.push_back('"'); // End the JSON string.
  }

  void typedValueToLogLine(const Formatters& formatters, const FormatterContext& context,
                           const StreamInfo::StreamInfo& info, Json::StringOutput& buffer,
                           JsonSerializer& serializer) const {

    const ProtobufWkt::Value value = formatters[0]->formatValueWithContext(context, info);

    switch (value.kind_case()) {
    case ProtobufWkt::Value::KIND_NOT_SET:
    case ProtobufWkt::Value::kNullValue:
      serializer.addNull();
      break;
    case ProtobufWkt::Value::kNumberValue:
      serializer.addNumber(value.number_value());
      break;
    case ProtobufWkt::Value::kStringValue:
      serializer.addString(value.string_value());
      break;
    case ProtobufWkt::Value::kBoolValue:
      serializer.addBool(value.bool_value());
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
        // We assume the output of getJsonStringFromMessage is a valid JSON string piece.
        buffer.buffer_.append(json_or.value());
      } else {
        serializer.addString(json_or.status().ToString());
      }
#else
      IS_ENVOY_BUG("Json support compiled out");
      buffer.buffer_.append(R"EOF("Json support compiled out")EOF");
#endif
      break;
    }
  }

  const bool omit_empty_values_{};
  const bool keep_value_type_{};
  const std::string empty_value_;

  using ParsedFormatElement = absl::variant<std::string, Formatters>;
  std::vector<ParsedFormatElement> parsed_elements_;
};

#endif

// Helper classes for StructFormatter::StructFormatMapVisitor.
template <class... Ts> struct StructFormatMapVisitorHelper : Ts... { using Ts::operator()...; };
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
class LegacyJsonFormatterBaseImpl : public FormatterBase<FormatterContext> {
public:
  using CommandParsers = std::vector<CommandParserBasePtr<FormatterContext>>;

  LegacyJsonFormatterBaseImpl(const ProtobufWkt::Struct& format_mapping, bool preserve_types,
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
using LegacyJsonFormatterImpl = LegacyJsonFormatterBaseImpl<HttpFormatterContext>;
using JsonFormatterImpl = JsonFormatterImplBase<HttpFormatterContext>;

} // namespace Formatter
} // namespace Envoy
