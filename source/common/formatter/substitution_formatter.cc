#include "source/common/formatter/substitution_formatter.h"

namespace Envoy {
namespace Formatter {

const std::regex& SubstitutionFormatParser::commandWithArgsRegex() {
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
  //                              _________________  _______________  _____________
  //                              |               |  |             |  |           |
  CONSTRUCT_ON_FIRST_USE(std::regex,
                         R"EOF(^%((?:[A-Z]|[0-9]|_)+)(?:\((.*?)\))?(?::([0-9]+))?%)EOF");
  //                             |__________________|     |______|        |______|
  //                                      |                   |              |
  // Capturing group specifying COMMAND --                    |              |
  // The index of this group is 1.                            |              |
  //                                                          |              |
  // Capturing group for SUBCOMMAND. If present, it will -----               |
  // contain SUBCOMMAND without "(" and ")". The index                       |
  // of SUBCOMMAND group is 2.                                               |
  //                                                                         |
  // Capturing group for LENGTH. If present, it will -------------------------
  // contain just number without ":". The index of
  // LENGTH group is 3.
  // clang-format on
}

JsonFormatBuilder::FormatterEelements
JsonFormatBuilder::fromStruct(const ProtobufWkt::Struct& struct_format) const {
  sanitize_buffer_.clear();
  join_raw_string_.clear();
  output_.clear();

  formatValueToFormatterEelements(struct_format);
  if (!join_raw_string_.empty()) {
    output_.push_back(JsonString{std::move(join_raw_string_)});
  }
  return std::move(output_);
};

void JsonFormatBuilder::formatValueToFormatterEelements(const ProtobufWkt::Value& value) const {
  switch (value.kind_case()) {
  case ProtobufWkt::Value::KIND_NOT_SET:
  case ProtobufWkt::Value::kNullValue:
    join_raw_string_.append(NullValue);
    break;
  case ProtobufWkt::Value::kNumberValue:
    if (keep_value_type_) {
      // Keep the number type in JSON.
      join_raw_string_.append(fmt::to_string(value.number_value()));
    } else {
      // Convert the number to string.
      join_raw_string_.append(fmt::format(R"("{}")", value.number_value()));
    }
    break;
  case ProtobufWkt::Value::kStringValue: {
    absl::string_view string_format = value.string_value();
    if (string_format.empty() || !absl::StrContains(string_format, '%')) {
      join_raw_string_.append(
          fmt::format(R"("{}")", Json::sanitize(sanitize_buffer_, string_format)));
    } else {
      // The string contains a formatter, we need to push the current raw string
      // into the output list first.
      if (!join_raw_string_.empty()) {
        output_.push_back(JsonString{std::move(join_raw_string_)});
      }

      // Now a formatter is coming, we need to push the current raw string into
      // the output list.
      output_.push_back(TmplString{std::string(string_format)});
    }
    break;
  }
  case ProtobufWkt::Value::kBoolValue:
    if (keep_value_type_) {
      // Keep the bool type in JSON.
      join_raw_string_.append(fmt::to_string(value.bool_value()));
    } else {
      // Convert the bool to string.
      join_raw_string_.append(fmt::format(R"("{}")", value.bool_value()));
    }
    break;
  case ProtobufWkt::Value::kStructValue: {
    formatValueToFormatterEelements(value.struct_value());
    break;
  case ProtobufWkt::Value::kListValue:
    join_raw_string_.push_back('[');
    for (int i = 0; i < value.list_value().values_size(); ++i) {
      if (i > 0) {
        join_raw_string_.push_back(',');
      }
      formatValueToFormatterEelements(value.list_value().values(i));
    }
    join_raw_string_.push_back(']');
    break;
  }
  }
}

void JsonFormatBuilder::formatValueToFormatterEelements(const ProtobufWkt::Struct& value) const {
  const auto& struct_fields = value.fields();
  std::vector<absl::string_view> sorted_keys;
  sorted_keys.reserve(struct_fields.size());
  for (const auto& field : struct_fields) {
    sorted_keys.push_back(field.first);
  }
  if (sort_properties_) {
    std::sort(sorted_keys.begin(), sorted_keys.end());
  }

  join_raw_string_.push_back('{');
  for (size_t index = 0; index < sorted_keys.size(); ++index) {
    if (index > 0) {
      join_raw_string_.push_back(',');
    }

    auto sanitized = Json::sanitize(sanitize_buffer_, sorted_keys[index]);
    join_raw_string_.append(fmt::format(R"("{}":)", sanitized));
    formatValueToFormatterEelements(struct_fields.at(sorted_keys[index]));
  }
  join_raw_string_.push_back('}');
}

} // namespace Formatter
} // namespace Envoy
