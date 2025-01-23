#include "source/common/formatter/substitution_formatter.h"
#include "source/common/formatter/high_order_formatter.h"

namespace Envoy {
namespace Formatter {

RegexFormatterProvider::RegexFormatterProvider(absl::Span<const std::string> args,
                                               absl::Status& creation_status) {
  if (args.size() != 2) {
    creation_status = absl::InvalidArgumentError("regex formatter requires 2 arguments");
    return;
  }

  regex_ = std::make_unique<re2::RE2>(args[0]);
  if (!regex_->ok()) {
    creation_status = absl::InvalidArgumentError("regex formatter has invalid regex");
    return;
  }

  if (regex_->NumberOfCapturingGroups() != 1) {
    creation_status =
        absl::InvalidArgumentError("regex formatter requires exactly 1 capturing group");
    return;
  }

  auto formater_or = FormatterImpl::create(args[1]);
  SET_AND_RETURN_IF_NOT_OK(formater_or.status(), creation_status);
  embedded_formatter_ = std::move(formater_or.value());
}

absl::optional<std::string>
RegexFormatterProvider::formatWithContext(const HttpFormatterContext& context,
                                          const StreamInfo::StreamInfo& stream_info) const {
  const std::string output = embedded_formatter_->formatWithContext(context, stream_info);
  absl::string_view value{};
  if (re2::RE2::FullMatch(output, *regex_, &value)) {
    return std::string(value);
  }
  return absl::nullopt;
}

FormatterProviderPtr CommandParser::parse(absl::string_view command, absl::string_view command_arg,
                                          absl::optional<size_t> max_length) const {
  using ProviderCreationFn = std::function<FormatterProviderPtr(
      absl::Span<const std::string>, absl::optional<size_t>, absl::Status&)>;
  static auto SupportedCommands = absl::flat_hash_map<std::string, ProviderCreationFn>{
      {"REGEX",
       [](absl::Span<const std::string> args, absl::optional<size_t>,
          absl::Status& creation_status) {
         return std::make_unique<RegexFormatterProvider>(args, creation_status);
       }},
  };

  const auto iter = SupportedCommands.find(command);
  if (iter == SupportedCommands.end()) {
    return nullptr;
  }

  absl::InlinedVector<absl::string_view, 4> args = absl::StrSplit(command_arg, ':');
  absl::InlinedVector<std::string, 4> decoded_args;
  for (absl::string_view arg : args) {
    decoded_args.push_back(Base64::decode(arg));
  }

  absl::Status creation_status;
  FormatterProviderPtr provider = iter->second(decoded_args, max_length, creation_status);
  THROW_IF_NOT_OK_REF(creation_status);
  return provider;
}

REGISTER_FACTORY(BuiltInHighOrderCommandParserFactory,
                 BuiltInCommandParserFactoryBase<HttpFormatterContext>);

} // namespace Formatter
} // namespace Envoy
