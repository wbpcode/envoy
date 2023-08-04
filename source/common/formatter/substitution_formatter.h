#pragma once

#include <bitset>
#include <functional>
#include <list>
#include <string>
#include <vector>
#include <regex>

#include "envoy/common/time.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/formatter/substitution_formatter.h"
#include "envoy/stream_info/stream_info.h"

#include "source/common/common/utility.h"

#include "absl/container/flat_hash_map.h"
#include "absl/types/optional.h"
#include "absl/strings/str_format.h"

namespace Envoy {
namespace Formatter {

class CommandSyntaxChecker {
public:
  using CommandSyntaxFlags = std::bitset<4>;
  static constexpr CommandSyntaxFlags COMMAND_ONLY = 0;
  static constexpr CommandSyntaxFlags PARAMS_REQUIRED = 1 << 0;
  static constexpr CommandSyntaxFlags PARAMS_OPTIONAL = 1 << 1;
  static constexpr CommandSyntaxFlags LENGTH_ALLOWED = 1 << 2;

  static void verifySyntax(CommandSyntaxChecker::CommandSyntaxFlags flags,
                           const std::string& command, const std::string& subcommand,
                           const absl::optional<size_t>& length);
};

inline constexpr absl::string_view DefaultUnspecifiedValueStringView = "-";

/**
 * FormatterProvider for string literals. It ignores headers and stream info and returns string by
 * which it was initialized.
 */
template <class FormatterContext>
class PlainStringFormatterProviderBase : public FormatterProviderBase<FormatterContext> {
public:
  PlainStringFormatterProviderBase(absl::string_view str) { str_.set_string_value(str); }

  // FormatterProvider
  absl::optional<std::string> format(const FormatterContext&,
                                     const StreamInfo::StreamInfo&) const override {
    return str_.string_value();
  }
  ProtobufWkt::Value formatValue(const FormatterContext&,
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
class PlainNumberFormatterProviderBase : public FormatterProviderBase<FormatterContext> {
public:
  PlainNumberFormatterProviderBase(double num) { num_.set_number_value(num); }

  // FormatterProvider
  absl::optional<std::string> format(const FormatterContext&,
                                     const StreamInfo::StreamInfo&) const override {
    std::string str = absl::StrFormat("%g", num_.number_value());
    return str;
  }
  ProtobufWkt::Value formatValue(const FormatterContext&,
                                 const StreamInfo::StreamInfo&) const override {
    return num_;
  }

private:
  ProtobufWkt::Value num_;
};

template <class FormatterContext>
class StringValueFormatterProviderBase : public FormatterProviderBase<FormatterContext> {
public:
  using ValueExtractor = std::function<absl::optional<std::string>(const FormatterContext&,
                                                                   const StreamInfo::StreamInfo&)>;

  StringValueFormatterProviderBase(ValueExtractor f,
                                   absl::optional<size_t> max_length = absl::nullopt)
      : value_extractor_(f), max_length_(max_length) {}

  // GenericProxyFormatterProvider
  absl::optional<std::string> format(const FormatterContext& context,
                                     const StreamInfo::StreamInfo& stream_info) const override {
    auto optional_str = value_extractor_(context, stream_info);
    if (!optional_str) {
      return absl::nullopt;
    }
    if (max_length_.has_value()) {
      if (optional_str->length() > max_length_.value()) {
        optional_str->resize(max_length_.value());
      }
    }
    return optional_str;
  }
  ProtobufWkt::Value formatValue(const FormatterContext& context,
                                 const StreamInfo::StreamInfo& stream_info) const override {
    return ValueUtil::optionalStringValue(format(context, stream_info));
  }

private:
  ValueExtractor value_extractor_;
  absl::optional<size_t> max_length_;
};

/**
 * Context independent types and utility of stream info formatter.
 */
class ContextIndependent {
public:
  class FieldExtractor {
  public:
    virtual ~FieldExtractor() = default;

    virtual absl::optional<std::string> extract(const StreamInfo::StreamInfo&) const PURE;
    virtual ProtobufWkt::Value extractValue(const StreamInfo::StreamInfo&) const PURE;
  };
  using FieldExtractorPtr = std::unique_ptr<FieldExtractor>;
  using FieldExtractorCreateFunc =
      std::function<FieldExtractorPtr(const std::string&, const absl::optional<size_t>&)>;

  enum class StreamInfoAddressFieldExtractionType { WithPort, WithoutPort, JustPort };

  using FieldExtractorLookupTbl =
      absl::flat_hash_map<absl::string_view,
                          std::pair<CommandSyntaxChecker::CommandSyntaxFlags,
                                    ContextIndependent::FieldExtractorCreateFunc>>;

  static const FieldExtractorLookupTbl& getKnownContextIndependentFieldExtractors();
};

template <class FormatterContext>
class ContextIndependentFormatterProviderBase : public FormatterProviderBase<FormatterContext> {
public:
  ContextIndependentFormatterProviderBase(const std::string& command,
                                          const std::string& sub_command,
                                          absl::optional<size_t> max_length) {
    const auto& extractors = ContextIndependent::getKnownContextIndependentFieldExtractors();

    auto it = extractors.find(command);

    if (it == extractors.end()) {
      throw EnvoyException(fmt::format("Not supported field in StreamInfo: {}", command));
    }

    // Check flags for the command.
    Envoy::Formatter::CommandSyntaxChecker::verifySyntax((*it).second.first, command, sub_command,
                                                         max_length);

    // Create a pointer to the formatter by calling a function
    // associated with formatter's name.
    field_extractor_ = (*it).second.second(sub_command, max_length);
  }

  // FormatterProvider
  absl::optional<std::string> format(const FormatterContext&,
                                     const StreamInfo::StreamInfo& info) const override {
    return field_extractor_->extract(info);
  }
  ProtobufWkt::Value formatValue(const FormatterContext&,
                                 const StreamInfo::StreamInfo& info) const override {
    return field_extractor_->extractValue(info);
  }

private:
  ContextIndependent::FieldExtractorPtr field_extractor_;
};

/**
 * Access log format parser.
 */
class SubstitutionFormatParser {
public:
  template <class FormatterContext>
  static std::vector<FormatterProviderBasePtr<FormatterContext>>
  parse(const std::string& format, const CommandParsersBase<FormatterContext>& command_parsers) {
    std::string current_token;
    std::vector<FormatterProviderBasePtr<FormatterContext>> formatters;

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
  // Non-capturing group specifying optional :LENGTH -------------------------------------
  //                                                                                      |
  // Non-capturing group specifying optional (SUBCOMMAND)------------------               |
  //                                                                       |              |
  // Non-capturing group specifying mandatory COMMAND                      |              |
  //  which uses only A-Z, 0-9 and _ characters     -----                  |              |
  //  Group is used only to specify allowed characters.  |                 |              |
  //                                                     |                 |              |
  //                                                     |                 |              |
  //                                             _________________  _______________  _____________
  //                                             |               |  |             |  |           |
  const std::regex command_w_args_regex(R"EOF(^%((?:[A-Z]|[0-9]|_)+)(?:\(([^\)]*)\))?(?::([0-9]+))?%)EOF");
  //                                            |__________________|     |______|        |______|
  //                                                     |                   |              |
  // Capturing group specifying COMMAND -----------------                    |              |
  // The index of this group is 1.                                           |              |
  //                                                                         |              |
  // Capturing group for SUBCOMMAND. If present, it will --------------------               |
  // contain SUBCOMMAND without "(" and ")". The index                                      |
  // of SUBCOMMAND group is 2.                                                              |
  //                                                                                        |
  // Capturing group for LENGTH. If present, it will ----------------------------------------
  // contain just number without ":". The index of
  // LENGTH group is 3.
    // clang-format on

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
            new PlainStringFormatterProviderBase<FormatterContext>(current_token)});
        current_token = "";
      }

      std::smatch m;
      const std::string search_space = std::string(format.substr(pos));
      if (!std::regex_search(search_space, m, command_w_args_regex)) {
        throw EnvoyException(
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
          throw EnvoyException(absl::StrCat("Length must be an integer, given: ", m.str(3)));
        }
        max_length = length_value;
      }
      std::vector<std::string> path;

      const size_t command_end_position = pos + m.str(0).length() - 1;

      bool added = false;
      for (const auto& cmd : command_parsers) {
        auto formatter = cmd->parse(command, subcommand, max_length);
        if (formatter) {
          formatters.push_back(std::move(formatter));
          added = true;
          break;
        }
      }

      if (!added) {
        // Finally, try the context independent formatters.
        formatters.emplace_back(FormatterProviderBasePtr<FormatterContext>{
            new ContextIndependentFormatterProviderBase<FormatterContext>(command, subcommand,
                                                                          max_length)});
      }

      pos = command_end_position;
    }

    if (!current_token.empty() || format.empty()) {
      // Create a PlainStringFormatterProvider with the final string literal. If the format string
      // was empty, this creates a PlainStringFormatterProvider with an empty string.
      formatters.emplace_back(FormatterProviderBasePtr<FormatterContext>{
          new PlainStringFormatterProviderBase<FormatterContext>(current_token)});
    }

    return formatters;
  }

  template <class FormatterContext>
  static std::vector<FormatterProviderBasePtr<FormatterContext>> parse(const std::string& format) {
    const CommandParsersBase<FormatterContext>& command_parsers =
        BuiltInCommandParsersBase<FormatterContext>::commandParsers();
    return parse(format, command_parsers);
  }

  /**
   * Parse a header subcommand of the form: X?Y .
   * Will populate a main_header and an optional alternative header if specified.
   * See doc:
   * https://envoyproxy.io/docs/envoy/latest/configuration/observability/access_log/access_log#format-rules
   */
  static void parseSubcommandHeaders(const std::string& subcommand, std::string& main_header,
                                     std::string& alternative_header);

  /* Variadic function template to parse the
     subcommand and assign found tokens to sequence of params.
     subcommand must be a sequence
     of tokens separated by the same separator, like: header1:header2 or header1?header2?header3.
     params must be a sequence of std::string& with optional container storing std::string. Here are
     examples of params:
     - std::string& token1
     - std::string& token1, std::string& token2
     - std::string& token1, std::string& token2, std::vector<std::string>& remaining

     If command contains more tokens than number of passed params, unassigned tokens will be
     ignored. If command contains less tokens than number of passed params, some params will be left
     untouched.
  */
  template <typename... Tokens>
  static void parseSubcommand(const std::string& subcommand, const char separator,
                              Tokens&&... params) {
    std::vector<absl::string_view> tokens;
    tokens = absl::StrSplit(subcommand, separator);
    std::vector<absl::string_view>::iterator it = tokens.begin();
    (
        [&](auto& param) {
          if (it != tokens.end()) {
            if constexpr (std::is_same_v<typename std::remove_reference<decltype(param)>::type,
                                         std::string>) {
              // Compile time handler for std::string.
              param = std::string(*it);
              it++;
            } else {
              // Compile time handler for container type. It will catch all remaining tokens and
              // move iterator to the end.
              do {
                param.push_back(std::string(*it));
                it++;
              } while (it != tokens.end());
            }
          }
        }(params),
        ...);
  }

  /**
   * Return a FormatterProviderPtr if a built-in command is found. This method
   * handles mapping the command name to an appropriate formatter.
   *
   * @param command - formatter's name.
   * @param subcommand - optional formatter specific data.
   * @param length - optional max length of output produced by the formatter.
   * @return FormattterProviderPtr substitution provider for the command or nullptr
   */
  static FormatterProviderPtr parseBuiltinCommand(const std::string& command,
                                                  const std::string& subcommand,
                                                  absl::optional<size_t>& length);

private:
  using FormatterProviderCreateFunc =
      std::function<FormatterProviderPtr(const std::string&, absl::optional<size_t>&)>;

  using FormatterProviderLookupTbl =
      absl::flat_hash_map<absl::string_view, std::pair<CommandSyntaxChecker::CommandSyntaxFlags,
                                                       FormatterProviderCreateFunc>>;
  static const FormatterProviderLookupTbl& getKnownFormatters();
};

template <class FormatterContext> class FormatterImplBase : public FormatterBase<FormatterContext> {
public:
  FormatterImplBase(absl::string_view format, bool omit_empty_values,
                    const CommandParsersBase<FormatterContext>& command_parsers)
      : empty_value_string_(omit_empty_values ? absl::string_view{}
                                              : DefaultUnspecifiedValueStringView) {
    providers_ = SubstitutionFormatParser::parse<FormatterContext>(format, command_parsers);
  }

  // Formatter
  std::string format(const FormatterContext& context,
                     const StreamInfo::StreamInfo& info) const override {
    std::string log_line;
    log_line.reserve(256);

    for (const auto& provider : providers_) {
      const auto bit = provider->format(context, info);
      log_line += bit.value_or(empty_value_string_);
    }

    return log_line;
  }

private:
  const std::string empty_value_string_;
  std::vector<FormatterProviderBasePtr<FormatterContext>> providers_;
};

// Helper classes for StructFormatter::StructFormatMapVisitor.
template <class... Ts> struct StructFormatMapVisitorHelper : Ts... { using Ts::operator()...; };
template <class... Ts> StructFormatMapVisitorHelper(Ts...) -> StructFormatMapVisitorHelper<Ts...>;

/**
 * An formatter for structured log formats, which returns a Struct proto that
 * can be converted easily into multiple formats.
 */
template <class FormatterContext> class StructFormatterBase {
public:
  StructFormatterBase(const ProtobufWkt::Struct& format_mapping, bool preserve_types,
                      bool omit_empty_values, const CommandParsersBase<FormatterContext>& commands)
      : omit_empty_values_(omit_empty_values), preserve_types_(preserve_types),
        empty_value_(omit_empty_values_ ? std::string()
                                        : std::string(DefaultUnspecifiedValueStringView)),
        struct_output_format_(FormatBuilder(commands).toFormatMapValue(format_mapping)) {}

  ProtobufWkt::Struct format(const FormatterContext& context,
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
    explicit FormatBuilder(const CommandParsersBase<FormatterContext>& commands)
        : commands_(commands) {}
    explicit FormatBuilder() : commands_(absl::nullopt) {}
    std::vector<FormatterProviderBasePtr<FormatterContext>>
    toFormatStringValue(const std::string& string_format) const {
      return SubstitutionFormatParser::parse<FormatterContext>(string_format, commands_);
    }
    std::vector<FormatterProviderBasePtr<FormatterContext>>
    toFormatNumberValue(double value) const {
      std::vector<FormatterProviderBasePtr<FormatterContext>> formatters;
      formatters.emplace_back(FormatterProviderBasePtr<FormatterContext>{
          new PlainNumberFormatterProviderBase<FormatterContext>(value)});
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
          throw EnvoyException(
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
          throw EnvoyException(
              "Only string values, nested structs, list values and number values are "
              "supported in structured access log format.");
        }
      }
      return {std::move(output)};
    }

  private:
    const CommandParsersBase<FormatterContext>& commands_;
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
        return provider->formatValue(context, stream_info);
      }

      if (omit_empty_values_) {
        return ValueUtil::optionalStringValue(provider->format(context, stream_info));
      }

      const auto str = provider->format(context, stream_info);
      return ValueUtil::stringValue(str.value_or(empty_value_));
    }
    // Multiple providers forces string output.
    std::string str;
    for (const auto& provider : providers) {
      const auto bit = provider->format(context, stream_info);
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
using StructFormatterPtr = std::unique_ptr<StructFormatterBase<FormatterContext>>;

template <class FormatterContext>
class JsonFormatterImplBase : public FormatterBase<FormatterContext> {
public:
  JsonFormatterImplBase(const ProtobufWkt::Struct& format_mapping, bool preserve_types,
                        bool omit_empty_values,
                        const CommandParsersBase<FormatterContext>& commands)
      : struct_formatter_(format_mapping, preserve_types, omit_empty_values, commands) {}

  // Formatter::format
  std::string format(const FormatterContext& context,
                     const StreamInfo::StreamInfo& info) const override {
    const ProtobufWkt::Struct output_struct = struct_formatter_.format(context, info);

#ifdef ENVOY_ENABLE_YAML
    const std::string log_line =
        MessageUtil::getJsonStringFromMessageOrError(output_struct, false, true);
#else
    IS_ENVOY_BUG("Json support compiled out");
    const std::string log_line = "";
#endif
    return absl::StrCat(log_line, "\n");
  }

private:
  const StructFormatterBase<FormatterContext> struct_formatter_;
};

/**
 * Util class for access log format.
 */
class SubstitutionFormatUtils {
public:
  static FormatterPtr defaultSubstitutionFormatter();
  // Optional references are not supported, but this method has large performance
  // impact, so using reference_wrapper.
  static const absl::optional<std::reference_wrapper<const std::string>>
  protocolToString(const absl::optional<Http::Protocol>& protocol);
  static const std::string&
  protocolToStringOrDefault(const absl::optional<Http::Protocol>& protocol);
  static const absl::optional<std::string> getHostname();
  static const std::string getHostnameOrDefault();

private:
  SubstitutionFormatUtils();

  static const std::string DEFAULT_FORMAT;
};

/**
 * Composite formatter implementation for HTTP/TCP access logs.
 */
using FormatterImpl = FormatterImplBase<HttpFormatterContext>;
using JsonFormatterImpl = JsonFormatterImplBase<HttpFormatterContext>;

/**
 * FormatterProvider for local_reply_body. It returns the string from `local_reply_body` argument.
 */
class LocalReplyBodyFormatter : public FormatterProvider {
public:
  LocalReplyBodyFormatter() = default;

  // Formatter::format
  absl::optional<std::string> format(const Http::RequestHeaderMap&, const Http::ResponseHeaderMap&,
                                     const Http::ResponseTrailerMap&, const StreamInfo::StreamInfo&,
                                     absl::string_view local_reply_body,
                                     AccessLog::AccessLogType) const override;
  ProtobufWkt::Value formatValue(const Http::RequestHeaderMap&, const Http::ResponseHeaderMap&,
                                 const Http::ResponseTrailerMap&, const StreamInfo::StreamInfo&,
                                 absl::string_view local_reply_body,
                                 AccessLog::AccessLogType) const override;
};

/**
 * FormatterProvider for access log type. It returns the string from `access_log_type` argument.
 */
class AccessLogTypeFormatter : public FormatterProvider {
public:
  AccessLogTypeFormatter() = default;

  // Formatter::format
  absl::optional<std::string> format(const Http::RequestHeaderMap&, const Http::ResponseHeaderMap&,
                                     const Http::ResponseTrailerMap&, const StreamInfo::StreamInfo&,
                                     absl::string_view local_reply_body,
                                     AccessLog::AccessLogType access_log_type) const override;
  ProtobufWkt::Value formatValue(const Http::RequestHeaderMap&, const Http::ResponseHeaderMap&,
                                 const Http::ResponseTrailerMap&, const StreamInfo::StreamInfo&,
                                 absl::string_view local_reply_body,
                                 AccessLog::AccessLogType access_log_type) const override;
};

class HeaderFormatter {
public:
  HeaderFormatter(const std::string& main_header, const std::string& alternative_header,
                  absl::optional<size_t> max_length);

protected:
  absl::optional<std::string> format(const Http::HeaderMap& headers) const;
  ProtobufWkt::Value formatValue(const Http::HeaderMap& headers) const;

private:
  const Http::HeaderEntry* findHeader(const Http::HeaderMap& headers) const;

  Http::LowerCaseString main_header_;
  Http::LowerCaseString alternative_header_;
  absl::optional<size_t> max_length_;
};

/**
 * FormatterProvider for headers byte size.
 */
class HeadersByteSizeFormatter : public FormatterProvider {
public:
  // TODO(taoxuy): Add RequestTrailers here.
  enum class HeaderType { RequestHeaders, ResponseHeaders, ResponseTrailers };

  HeadersByteSizeFormatter(const HeaderType header_type);

  absl::optional<std::string> format(const Http::RequestHeaderMap& request_headers,
                                     const Http::ResponseHeaderMap& response_headers,
                                     const Http::ResponseTrailerMap& response_trailers,
                                     const StreamInfo::StreamInfo&, absl::string_view,
                                     AccessLog::AccessLogType) const override;
  ProtobufWkt::Value formatValue(const Http::RequestHeaderMap& request_headers,
                                 const Http::ResponseHeaderMap& response_headers,
                                 const Http::ResponseTrailerMap& response_trailers,
                                 const StreamInfo::StreamInfo&, absl::string_view,
                                 AccessLog::AccessLogType) const override;

private:
  uint64_t extractHeadersByteSize(const Http::RequestHeaderMap& request_headers,
                                  const Http::ResponseHeaderMap& response_headers,
                                  const Http::ResponseTrailerMap& response_trailers) const;
  HeaderType header_type_;
};

/**
 * FormatterProvider for request headers.
 */
class RequestHeaderFormatter : public FormatterProvider, HeaderFormatter {
public:
  RequestHeaderFormatter(const std::string& main_header, const std::string& alternative_header,
                         absl::optional<size_t> max_length);

  // FormatterProvider
  absl::optional<std::string> format(const Http::RequestHeaderMap& request_headers,
                                     const Http::ResponseHeaderMap&,
                                     const Http::ResponseTrailerMap&, const StreamInfo::StreamInfo&,
                                     absl::string_view, AccessLog::AccessLogType) const override;
  ProtobufWkt::Value formatValue(const Http::RequestHeaderMap&, const Http::ResponseHeaderMap&,
                                 const Http::ResponseTrailerMap&, const StreamInfo::StreamInfo&,
                                 absl::string_view, AccessLog::AccessLogType) const override;
};

/**
 * FormatterProvider for response headers.
 */
class ResponseHeaderFormatter : public FormatterProvider, HeaderFormatter {
public:
  ResponseHeaderFormatter(const std::string& main_header, const std::string& alternative_header,
                          absl::optional<size_t> max_length);

  // FormatterProvider
  absl::optional<std::string> format(const Http::RequestHeaderMap&,
                                     const Http::ResponseHeaderMap& response_headers,
                                     const Http::ResponseTrailerMap&, const StreamInfo::StreamInfo&,
                                     absl::string_view, AccessLog::AccessLogType) const override;
  ProtobufWkt::Value formatValue(const Http::RequestHeaderMap&, const Http::ResponseHeaderMap&,
                                 const Http::ResponseTrailerMap&, const StreamInfo::StreamInfo&,
                                 absl::string_view, AccessLog::AccessLogType) const override;
};

/**
 * FormatterProvider for response trailers.
 */
class ResponseTrailerFormatter : public FormatterProvider, HeaderFormatter {
public:
  ResponseTrailerFormatter(const std::string& main_header, const std::string& alternative_header,
                           absl::optional<size_t> max_length);

  // FormatterProvider
  absl::optional<std::string> format(const Http::RequestHeaderMap&, const Http::ResponseHeaderMap&,
                                     const Http::ResponseTrailerMap& response_trailers,
                                     const StreamInfo::StreamInfo&, absl::string_view,
                                     AccessLog::AccessLogType) const override;
  ProtobufWkt::Value formatValue(const Http::RequestHeaderMap&, const Http::ResponseHeaderMap&,
                                 const Http::ResponseTrailerMap&, const StreamInfo::StreamInfo&,
                                 absl::string_view, AccessLog::AccessLogType) const override;
};

class GrpcStatusFormatter : public FormatterProvider, HeaderFormatter {
public:
  enum Format {
    CamelString,
    SnakeString,
    Number,
  };

  GrpcStatusFormatter(const std::string& main_header, const std::string& alternative_header,
                      absl::optional<size_t> max_length, Format format);

  // FormatterProvider
  absl::optional<std::string> format(const Http::RequestHeaderMap&,
                                     const Http::ResponseHeaderMap& response_headers,
                                     const Http::ResponseTrailerMap& response_trailers,
                                     const StreamInfo::StreamInfo&, absl::string_view,
                                     AccessLog::AccessLogType) const override;
  ProtobufWkt::Value formatValue(const Http::RequestHeaderMap&, const Http::ResponseHeaderMap&,
                                 const Http::ResponseTrailerMap&, const StreamInfo::StreamInfo&,
                                 absl::string_view, AccessLog::AccessLogType) const override;

  static Format parseFormat(absl::string_view format);

private:
  const Format format_;
};

/**
 * FormatterProvider for request headers from StreamInfo (rather than the request_headers param).
 * Purely for testing.
 */
class StreamInfoRequestHeaderFormatter : public FormatterProvider, HeaderFormatter {
public:
  StreamInfoRequestHeaderFormatter(const std::string& main_header,
                                   const std::string& alternative_header,
                                   absl::optional<size_t> max_length);

  // FormatterProvider
  absl::optional<std::string> format(const Http::RequestHeaderMap& request_headers,
                                     const Http::ResponseHeaderMap&,
                                     const Http::ResponseTrailerMap&, const StreamInfo::StreamInfo&,
                                     absl::string_view, AccessLog::AccessLogType) const override;
  ProtobufWkt::Value formatValue(const Http::RequestHeaderMap&, const Http::ResponseHeaderMap&,
                                 const Http::ResponseTrailerMap&, const StreamInfo::StreamInfo&,
                                 absl::string_view, AccessLog::AccessLogType) const override;
};

} // namespace Formatter
} // namespace Envoy
