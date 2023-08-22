#pragma once

#include "envoy/formatter/substitution_formatter_base.h"
#include "envoy/http/header_map.h"

namespace Envoy {
namespace Formatter {

/**
 * HTTP specific substitution formatter context for HTTP access logs or formatters.
 */
class HttpFormatterContext {
public:
  /**
   * Constructor that uses the provided request/response headers, response trailers, local reply
   * body, and access log type. Any of the parameters can be nullptr/empty.
   *
   * @param request_headers supplies the request headers.
   * @param response_headers supplies the response headers.
   * @param response_trailers supplies the response trailers.
   * @param local_reply_body supplies the local reply body.
   * @param log_type supplies the access log type.
   */
  HttpFormatterContext(const Http::RequestHeaderMap* request_headers = nullptr,
                       const Http::ResponseHeaderMap* response_headers = nullptr,
                       const Http::ResponseTrailerMap* response_trailers = nullptr,
                       absl::string_view local_reply_body = {},
                       AccessLog::AccessLogType log_type = AccessLog::AccessLogType::NotSet);
  /**
   * Set or overwrite the request headers.
   * @param request_headers supplies the request headers.
   */
  HttpFormatterContext& setRequestHeaders(const Http::RequestHeaderMap& request_headers) {
    request_headers_ = &request_headers;
    return *this;
  }
  /**
   * Set or overwrite the response headers.
   * @param response_headers supplies the response headers.
   */
  HttpFormatterContext& setResponseHeaders(const Http::ResponseHeaderMap& response_headers) {
    response_headers_ = &response_headers;
    return *this;
  }

  /**
   * Set or overwrite the response trailers.
   * @param response_trailers supplies the response trailers.
   */
  HttpFormatterContext& setResponseTrailers(const Http::ResponseTrailerMap& response_trailers) {
    response_trailers_ = &response_trailers;
    return *this;
  }

  /**
   * Set or overwrite the local reply body.
   * @param local_reply_body supplies the local reply body.
   */
  HttpFormatterContext& setLocalReplyBody(absl::string_view local_reply_body) {
    local_reply_body_ = local_reply_body;
    return *this;
  }

  /**
   * Set or overwrite the access log type.
   * @param log_type supplies the access log type.
   */
  HttpFormatterContext& setAccessLogType(AccessLog::AccessLogType log_type) {
    log_type_ = log_type;
    return *this;
  }

  /**
   * @return const Http::RequestHeaderMap& the request headers. Empty request header map if no
   * request headers are available.
   */
  const Http::RequestHeaderMap& requestHeaders() const;

  /**
   * @return const Http::ResponseHeaderMap& the response headers. Empty respnose header map if
   * no response headers are available.
   */
  const Http::ResponseHeaderMap& responseHeaders() const;

  /**
   * @return const Http::ResponseTrailerMap& the response trailers. Empty response trailer map
   * if no response trailers are available.
   */
  const Http::ResponseTrailerMap& responseTrailers() const;

  /**
   * @return absl::string_view the local reply body. Empty if no local reply body.
   */
  absl::string_view localReplyBody() const;

  /**
   * @return AccessLog::AccessLogType the type of access log. NotSet if this is not used for
   * access logging.
   */
  AccessLog::AccessLogType accessLogType() const;

private:
  const Http::RequestHeaderMap* request_headers_{};
  const Http::ResponseHeaderMap* response_headers_{};
  const Http::ResponseTrailerMap* response_trailers_{};
  absl::string_view local_reply_body_{};
  AccessLog::AccessLogType log_type_{AccessLog::AccessLogType::NotSet};
};

/**
 * Interface for substitution formatter.
 * Formatters provide a complete substitution output line for the given headers/trailers/stream.
 */
template <> class FormatterBase<HttpFormatterContext> {
public:
  virtual ~FormatterBase() = default;

  /**
   * Return a formatted substitution line.
   * @param context supplies the formatter context.
   * @param stream_info supplies the stream info.
   * @param local_reply_body supplies the local reply body.
   * @return std::string string containing the complete formatted substitution line.
   */
  virtual std::string format(const Http::RequestHeaderMap& request_headers,
                             const Http::ResponseHeaderMap& response_headers,
                             const Http::ResponseTrailerMap& response_trailers,
                             const StreamInfo::StreamInfo& stream_info,
                             absl::string_view local_reply_body,
                             AccessLog::AccessLogType access_log_type) const PURE;

  /**
   * Return a formatted substitution line.
   * @param context supplies the formatter context.
   * @param stream_info supplies the stream info.
   * @return std::string string containing the complete formatted substitution line.
   */
  virtual std::string formatWithContext(const HttpFormatterContext& context,
                                        const StreamInfo::StreamInfo& stream_info) const {
    return format(context.requestHeaders(), context.responseHeaders(), context.responseTrailers(),
                  stream_info, context.localReplyBody(), context.accessLogType());
  }
};

using Formatter = FormatterBase<HttpFormatterContext>;
using FormatterPtr = std::unique_ptr<Formatter>;
using FormatterConstSharedPtr = std::shared_ptr<const Formatter>;

/**
 * Interface for substitution provider.
 * FormatterProviders extract information from the given headers/trailers/stream.
 */
class FormatterProvider : public FormatterProviderBase<HttpFormatterContext> {
public:
  /**
   * Extract a value from the provided headers/trailers/stream.
   * @param request_headers supplies the request headers.
   * @param response_headers supplies the response headers.
   * @param response_trailers supplies the response trailers.
   * @param stream_info supplies the stream info.
   * @param local_reply_body supplies the local reply body.
   * @return absl::optional<std::string> optional string containing a single value extracted from
   * the given headers/trailers/stream.
   */
  virtual absl::optional<std::string> format(const Http::RequestHeaderMap& request_headers,
                                             const Http::ResponseHeaderMap& response_headers,
                                             const Http::ResponseTrailerMap& response_trailers,
                                             const StreamInfo::StreamInfo& stream_info,
                                             absl::string_view local_reply_body,
                                             AccessLog::AccessLogType access_log_type) const PURE;
  /**
   * Extract a value from the provided headers/trailers/stream, preserving the value's type.
   * @param request_headers supplies the request headers.
   * @param response_headers supplies the response headers.
   * @param response_trailers supplies the response trailers.
   * @param stream_info supplies the stream info.
   * @param local_reply_body supplies the local reply body.
   * @return ProtobufWkt::Value containing a single value extracted from the given
   *         headers/trailers/stream.
   */
  virtual ProtobufWkt::Value formatValue(const Http::RequestHeaderMap& request_headers,
                                         const Http::ResponseHeaderMap& response_headers,
                                         const Http::ResponseTrailerMap& response_trailers,
                                         const StreamInfo::StreamInfo& stream_info,
                                         absl::string_view local_reply_body,
                                         AccessLog::AccessLogType access_log_type) const PURE;

  // TODO(wbpcode): this two methods are used to bridge the old and new formatter interface.
  // We can defer the code change in the old formatter interface to future by this way.
  absl::optional<std::string>
  formatWithContext(const HttpFormatterContext& context,
                    const StreamInfo::StreamInfo& stream_info) const override {
    return format(context.requestHeaders(), context.responseHeaders(), context.responseTrailers(),
                  stream_info, context.localReplyBody(), context.accessLogType());
  }
  ProtobufWkt::Value
  formatValueWithContext(const HttpFormatterContext& context,
                         const StreamInfo::StreamInfo& stream_info) const override {
    return formatValue(context.requestHeaders(), context.responseHeaders(),
                       context.responseTrailers(), stream_info, context.localReplyBody(),
                       context.accessLogType());
  }
};

using FormatterProviderPtr = std::unique_ptr<FormatterProviderBase<HttpFormatterContext>>;
using HttpFormatterProviderPtr = std::unique_ptr<FormatterProvider>;

/**
 * Interface for command parser.
 * CommandParser returns a FormatterProviderPtr after successfully parsing an access log format
 * token, nullptr otherwise.
 */
class CommandParser {
public:
  virtual ~CommandParser() = default;

  /**
   * Return a FormatterProviderPtr if subcommand and max_length
   * are correct for the formatter provider associated
   * with command.
   * @param command - name of the FormatterProvider
   * @param subcommand - command specific data. (optional)
   * @param max_length - length to which the output produced by FormatterProvider
   *   should be truncated to (optional)
   *
   * @return FormattterProviderPtr substitution provider for the parsed command
   */
  virtual HttpFormatterProviderPtr parse(const std::string& command, const std::string& subcommand,
                                         absl::optional<size_t>& max_length) const PURE;
};

using CommandParserPtr = std::unique_ptr<CommandParser>;

/**
 * Implemented by each custom CommandParser and registered via Registry::registerFactory()
 * or the convenience class RegisterFactory.
 */
class CommandParserFactory : public Config::TypedFactory {
public:
  ~CommandParserFactory() override = default;

  /**
   * Creates a particular CommandParser implementation.
   *
   * @param config supplies the configuration for the command parser.
   * @param context supplies the factory context.
   * @return CommandParserPtr the CommandParser which will be used in
   * SubstitutionFormatParser::parse() when evaluating an access log format string.
   */
  virtual CommandParserPtr
  createCommandParserFromProto(const Protobuf::Message& config,
                               Server::Configuration::CommonFactoryContext& context) PURE;

  std::string category() const override { return "envoy.formatter"; }
};

} // namespace Formatter
} // namespace Envoy
