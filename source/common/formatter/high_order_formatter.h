#pragma once

#include "source/common/formatter/substitution_formatter.h"
#include "re2/re2.h"
#include "source/common/common/base64.h"

namespace Envoy {
namespace Formatter {

/**
 * Regex formatter provider which extracts a sub-group from the output of an embedded formatter.
 */
class RegexFormatterProvider : public FormatterProvider {
public:
  /**
   * @param args supplies the regex and the embedded format string.
   * @param creation_status will be set to an error if the regex formatter cannot be created.
   */
  RegexFormatterProvider(absl::Span<const std::string> args, absl::Status& creation_status);

  // FormatterProvider
  absl::optional<std::string>
  formatWithContext(const HttpFormatterContext& context,
                    const StreamInfo::StreamInfo& stream_info) const override;
  ProtobufWkt::Value
  formatValueWithContext(const HttpFormatterContext& context,
                         const StreamInfo::StreamInfo& stream_info) const override {
    return ValueUtil::optionalStringValue(formatWithContext(context, stream_info));
  }

private:
  std::unique_ptr<re2::RE2> regex_;
  FormatterPtr embedded_formatter_;
};

/**
 * High order command parser which creates a formatter provider based on a command.
 */
class HighOrderCommandParser : public CommandParser {
public:
  FormatterProviderPtr parse(absl::string_view command, absl::string_view command_arg,
                             absl::optional<size_t> max_length) const override;
};

class BuiltInOrderCommandParserFactory : public BuiltInOrderCommandParserFactory {
public:
  CommandParserPtr createCommandParser() const override {
    return std::make_unique<HighOrderCommandParser>();
  }
  std::string name() const override { return "envoy.built_in_formatters.high_order"; }
};

} // namespace Formatter
} // namespace Envoy
