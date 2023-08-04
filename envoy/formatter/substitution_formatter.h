#pragma once

#include <memory>
#include <string>

#include "envoy/http/header_map.h"
#include "envoy/formatter/substitution_formatter_base.h"

namespace Envoy {
namespace Formatter {

/**
 * Interface for substitution formatter context for HTTP/TCP access logs.
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
};

using Formatter = FormatterBase<HttpFormatterContext>;
using FormatterPtr = std::unique_ptr<Formatter>;
using FormatterConstSharedPtr = std::shared_ptr<const Formatter>;

using FormatterProvider = FormatterProviderBase<HttpFormatterContext>;
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
