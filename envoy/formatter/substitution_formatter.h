#pragma once

#include "envoy/http/header_map.h"

#include "envoy/formatter/substitution_formatter_base.h"

namespace Envoy {
namespace Formatter {

/**
 * Interface for substitution formatter context for HTTP access logs.
 */
class HttpFormatterContext {
public:
  virtual ~HttpFormatterContext() = default;

  /**
   * @return const Http::RequestHeaderMap& the request headers. Empty request header map if no
   * request headers are available.
   */
  virtual const Http::RequestHeaderMap& requestHeaders() const PURE;

  /**
   * @return const Http::ResponseHeaderMap& the response headers. Empty respnose header map if
   * no response headers are available.
   */
  virtual const Http::ResponseHeaderMap& responseHeaders() const PURE;

  /**
   * @return const Http::ResponseTrailerMap& the response trailers. Empty response trailer map
   * if no response trailers are available.
   */
  virtual const Http::ResponseTrailerMap& responseTrailers() const PURE;

  /**
   * @return absl::string_view the local reply body. Empty if no local reply body.
   */
  virtual absl::string_view localReplyBody() const PURE;

  /**
   * @return AccessLog::AccessLogType the type of access log. NotSet if this is not used for
   * access logging.
   */
  virtual AccessLog::AccessLogType accessLogType() const PURE;

  static constexpr absl::string_view category() { return "http"; }
};

// Alias of FormatterBase<HttpFormatterContext> for backward compatibility.
using Formatter = FormatterBase<HttpFormatterContext>;
using FormatterPtr = std::unique_ptr<Formatter>;

class FormatterProvider : public FormatterProviderBase<HttpFormatterContext> {
public:
  // FormatterProviderBase<HttpFormatterContext>
  absl::optional<std::string> format(const HttpFormatterContext& context,
                                     const StreamInfo::StreamInfo& info) const override {
    return format(context.requestHeaders(), context.responseHeaders(), context.responseTrailers(),
                  info, context.localReplyBody(), context.accessLogType());
  }
  ProtobufWkt::Value formatValue(const HttpFormatterContext& context,
                                 const StreamInfo::StreamInfo& info) const override {
    return formatValue(context.requestHeaders(), context.responseHeaders(),
                       context.responseTrailers(), info, context.localReplyBody(),
                       context.accessLogType());
  }

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
};
using FormatterProviderPtr = std::unique_ptr<FormatterProvider>;

using CommandParser = CommandParserBase<HttpFormatterContext>;
using CommandParserPtr = std::unique_ptr<CommandParser>;
using CommandParserSharedPtr = std::shared_ptr<CommandParser>;

/**
 * Implemented by each custom CommandParser and registered via Registry::registerFactory()
 * or the convenience class RegisterFactory.
 */
class CommandParserFactory : public CommandParserFactoryBase<HttpFormatterContext> {
public:
  // Use "envoy.formatter" as category name of HTTP formatter for backward compatibility.
  std::string category() const override { return "envoy.formatter"; }
};

} // namespace Formatter
} // namespace Envoy
