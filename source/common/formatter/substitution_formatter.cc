#include "source/common/formatter/substitution_formatter.h"

#include <algorithm>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <regex>
#include <string>
#include <vector>

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/upstream/upstream.h"

#include "source/common/api/os_sys_calls_impl.h"
#include "source/common/common/assert.h"
#include "source/common/common/empty_string.h"
#include "source/common/common/fmt.h"
#include "source/common/common/thread.h"
#include "source/common/common/utility.h"
#include "source/common/config/metadata.h"
#include "source/common/grpc/common.h"
#include "source/common/grpc/status.h"
#include "source/common/http/utility.h"
#include "source/common/protobuf/message_validator_impl.h"
#include "source/common/protobuf/utility.h"
#include "source/common/runtime/runtime_features.h"
#include "source/common/stream_info/utility.h"

#include "absl/strings/str_format.h"
#include "absl/strings/str_replace.h"
#include "absl/strings/str_split.h"
#include "fmt/format.h"

using Envoy::Config::Metadata;

namespace Envoy {
namespace Formatter {

static const std::string DefaultUnspecifiedValueString = "-";

namespace {

const ProtobufWkt::Value& unspecifiedValue() { return ValueUtil::nullValue(); }

void truncate(std::string& str, absl::optional<uint32_t> max_length) {
  if (!max_length) {
    return;
  }

  str = str.substr(0, max_length.value());
}

// Matches newline pattern in a system time format string (e.g. start time)
const std::regex& getSystemTimeFormatNewlinePattern() {
  CONSTRUCT_ON_FIRST_USE(std::regex, "%[-_0^#]*[1-9]*(E|O)?n");
}

} // namespace

const std::string SubstitutionFormatUtils::DEFAULT_FORMAT =
    "[%START_TIME%] \"%REQ(:METHOD)% %REQ(X-ENVOY-ORIGINAL-PATH?:PATH)% %PROTOCOL%\" "
    "%RESPONSE_CODE% %RESPONSE_FLAGS% %BYTES_RECEIVED% %BYTES_SENT% %DURATION% "
    "%RESP(X-ENVOY-UPSTREAM-SERVICE-TIME)% "
    "\"%REQ(X-FORWARDED-FOR)%\" \"%REQ(USER-AGENT)%\" \"%REQ(X-REQUEST-ID)%\" "
    "\"%REQ(:AUTHORITY)%\" \"%UPSTREAM_HOST%\"\n";

FormatterPtr SubstitutionFormatUtils::defaultSubstitutionFormatter() {
  return std::make_unique<Envoy::Formatter::FormatterImpl>(DEFAULT_FORMAT, false);
}

const absl::optional<std::reference_wrapper<const std::string>>
SubstitutionFormatUtils::protocolToString(const absl::optional<Http::Protocol>& protocol) {
  if (protocol) {
    return Http::Utility::getProtocolString(protocol.value());
  }
  return absl::nullopt;
}

const std::string&
SubstitutionFormatUtils::protocolToStringOrDefault(const absl::optional<Http::Protocol>& protocol) {
  if (protocol) {
    return Http::Utility::getProtocolString(protocol.value());
  }
  return DefaultUnspecifiedValueString;
}

const absl::optional<std::string> SubstitutionFormatUtils::getHostname() {
#ifdef HOST_NAME_MAX
  const size_t len = HOST_NAME_MAX;
#else
  // This is notably the case in OSX.
  const size_t len = 255;
#endif
  char name[len];
  Api::OsSysCalls& os_sys_calls = Api::OsSysCallsSingleton::get();
  const Api::SysCallIntResult result = os_sys_calls.gethostname(name, len);

  absl::optional<std::string> hostname;
  if (result.return_value_ == 0) {
    hostname = name;
  }

  return hostname;
}

const std::string SubstitutionFormatUtils::getHostnameOrDefault() {
  absl::optional<std::string> hostname = getHostname();
  if (hostname.has_value()) {
    return hostname.value();
  }
  return DefaultUnspecifiedValueString;
}

void SubstitutionFormatParser::parseSubcommandHeaders(const std::string& subcommand,
                                                      std::string& main_header,
                                                      std::string& alternative_header) {
  // subs is used only to check if there are more than 2 headers separated by '?'.
  std::vector<std::string> subs;
  alternative_header = "";
  parseSubcommand(subcommand, '?', main_header, alternative_header, subs);
  if (!subs.empty()) {
    throw EnvoyException(
        // Header format rules support only one alternative header.
        // docs/root/configuration/observability/access_log/access_log.rst#format-rules
        absl::StrCat("More than 1 alternative header specified in token: ", subcommand));
  }

  // The main and alternative header should not contain invalid characters {NUL, LR, CF}.
  if (!Envoy::Http::validHeaderString(main_header) ||
      !Envoy::Http::validHeaderString(alternative_header)) {
    throw EnvoyException("Invalid header configuration. Format string contains null or newline.");
  }
}

const SubstitutionFormatParser::FormatterProviderLookupTbl&
SubstitutionFormatParser::getKnownFormatters() {
  CONSTRUCT_ON_FIRST_USE(
      FormatterProviderLookupTbl,
      {{"REQ",
        {CommandSyntaxChecker::PARAMS_REQUIRED | CommandSyntaxChecker::LENGTH_ALLOWED,
         [](const std::string& format, absl::optional<size_t>& max_length) {
           std::string main_header, alternative_header;

           parseSubcommandHeaders(format, main_header, alternative_header);

           return std::make_unique<RequestHeaderFormatter>(main_header, alternative_header,
                                                           max_length);
         }}},
       {"RESP",
        {CommandSyntaxChecker::PARAMS_REQUIRED | CommandSyntaxChecker::LENGTH_ALLOWED,
         [](const std::string& format, absl::optional<size_t>& max_length) {
           std::string main_header, alternative_header;

           parseSubcommandHeaders(format, main_header, alternative_header);

           return std::make_unique<ResponseHeaderFormatter>(main_header, alternative_header,
                                                            max_length);
         }}},
       {"TRAILER",
        {CommandSyntaxChecker::PARAMS_REQUIRED | CommandSyntaxChecker::LENGTH_ALLOWED,
         [](const std::string& format, absl::optional<size_t>& max_length) {
           std::string main_header, alternative_header;

           parseSubcommandHeaders(format, main_header, alternative_header);

           return std::make_unique<ResponseTrailerFormatter>(main_header, alternative_header,
                                                             max_length);
         }}},
       {"LOCAL_REPLY_BODY",
        {CommandSyntaxChecker::COMMAND_ONLY,
         [](const std::string&, absl::optional<size_t>&) {
           return std::make_unique<LocalReplyBodyFormatter>();
         }}},
       {"ACCESS_LOG_TYPE",
        {CommandSyntaxChecker::COMMAND_ONLY,
         [](const std::string&, absl::optional<size_t>&) {
           return std::make_unique<AccessLogTypeFormatter>();
         }}},
       {"GRPC_STATUS",
        {CommandSyntaxChecker::PARAMS_OPTIONAL,
         [](const std::string& format, const absl::optional<size_t>&) {
           return std::make_unique<GrpcStatusFormatter>("grpc-status", "", absl::optional<size_t>(),
                                                        GrpcStatusFormatter::parseFormat(format));
         }}},
       {"GRPC_STATUS_NUMBER",
        {CommandSyntaxChecker::COMMAND_ONLY,
         [](const std::string&, const absl::optional<size_t>&) {
           return std::make_unique<GrpcStatusFormatter>("grpc-status", "", absl::optional<size_t>(),
                                                        GrpcStatusFormatter::Number);
         }}},
       {"REQUEST_HEADERS_BYTES",
        {CommandSyntaxChecker::COMMAND_ONLY,
         [](const std::string&, absl::optional<size_t>&) {
           return std::make_unique<HeadersByteSizeFormatter>(
               HeadersByteSizeFormatter::HeaderType::RequestHeaders);
         }}},
       {"RESPONSE_HEADERS_BYTES",
        {CommandSyntaxChecker::COMMAND_ONLY,
         [](const std::string&, absl::optional<size_t>&) {
           return std::make_unique<HeadersByteSizeFormatter>(
               HeadersByteSizeFormatter::HeaderType::ResponseHeaders);
         }}},
       {"RESPONSE_TRAILERS_BYTES",
        {CommandSyntaxChecker::COMMAND_ONLY,
         [](const std::string&, absl::optional<size_t>&) {
           return std::make_unique<HeadersByteSizeFormatter>(
               HeadersByteSizeFormatter::HeaderType::ResponseTrailers);
         }}},
       {"START_TIME",
        {CommandSyntaxChecker::PARAMS_OPTIONAL,
         [](const std::string& format, const absl::optional<size_t>&) {
           return std::make_unique<SystemTimeFormatter>(
               format,
               std::make_unique<SystemTimeFormatter::TimeFieldExtractor>(
                   [](const StreamInfo::StreamInfo& stream_info) -> absl::optional<SystemTime> {
                     return stream_info.startTime();
                   }));
         }}},

       {"DYNAMIC_METADATA",
        {CommandSyntaxChecker::PARAMS_REQUIRED,
         [](const std::string& format, const absl::optional<size_t>& max_length) {
           std::string filter_namespace;
           std::vector<std::string> path;

           SubstitutionFormatParser::parseSubcommand(format, ':', filter_namespace, path);
           return std::make_unique<DynamicMetadataFormatter>(filter_namespace, path, max_length);
         }}},

       {"CLUSTER_METADATA",
        {CommandSyntaxChecker::PARAMS_REQUIRED,
         [](const std::string& format, const absl::optional<size_t>& max_length) {
           std::string filter_namespace;
           std::vector<std::string> path;

           SubstitutionFormatParser::parseSubcommand(format, ':', filter_namespace, path);
           return std::make_unique<ClusterMetadataFormatter>(filter_namespace, path, max_length);
         }}},
       {"UPSTREAM_METADATA",
        {CommandSyntaxChecker::PARAMS_REQUIRED,
         [](const std::string& format, const absl::optional<size_t>& max_length) {
           std::string filter_namespace;
           std::vector<std::string> path;

           SubstitutionFormatParser::parseSubcommand(format, ':', filter_namespace, path);
           return std::make_unique<UpstreamHostMetadataFormatter>(filter_namespace, path,
                                                                  max_length);
         }}},
       {"FILTER_STATE",
        {CommandSyntaxChecker::PARAMS_OPTIONAL | CommandSyntaxChecker::LENGTH_ALLOWED,
         [](const std::string& format, const absl::optional<size_t>& max_length) {
           return FilterStateFormatter::create(format, max_length, false);
         }}},
       {"UPSTREAM_FILTER_STATE",
        {CommandSyntaxChecker::PARAMS_OPTIONAL | CommandSyntaxChecker::LENGTH_ALLOWED,
         [](const std::string& format, const absl::optional<size_t>& max_length) {
           return FilterStateFormatter::create(format, max_length, true);
         }}},
       {"DOWNSTREAM_PEER_CERT_V_START",
        {CommandSyntaxChecker::PARAMS_OPTIONAL,
         [](const std::string& format, const absl::optional<size_t>&) {
           return std::make_unique<DownstreamPeerCertVStartFormatter>(format);
         }}},
       {"DOWNSTREAM_PEER_CERT_V_END",
        {CommandSyntaxChecker::PARAMS_OPTIONAL,
         [](const std::string& format, const absl::optional<size_t>&) {
           return std::make_unique<DownstreamPeerCertVEndFormatter>(format);
         }}},
       {"UPSTREAM_PEER_CERT_V_START",
        {CommandSyntaxChecker::PARAMS_OPTIONAL,
         [](const std::string& format, const absl::optional<size_t>&) {
           return std::make_unique<UpstreamPeerCertVStartFormatter>(format);
         }}},
       {"UPSTREAM_PEER_CERT_V_END",
        {CommandSyntaxChecker::PARAMS_OPTIONAL,
         [](const std::string& format, const absl::optional<size_t>&) {
           return std::make_unique<UpstreamPeerCertVEndFormatter>(format);
         }}},
       {"ENVIRONMENT",
        {CommandSyntaxChecker::PARAMS_REQUIRED | CommandSyntaxChecker::LENGTH_ALLOWED,
         [](const std::string& key, absl::optional<size_t>& max_length) {
           return std::make_unique<EnvironmentFormatter>(key, max_length);
         }}},
       {"STREAM_INFO_REQ",
        {CommandSyntaxChecker::PARAMS_REQUIRED | CommandSyntaxChecker::LENGTH_ALLOWED,
         [](const std::string& format, absl::optional<size_t>& max_length) {
           std::string main_header, alternative_header;

           SubstitutionFormatParser::parseSubcommandHeaders(format, main_header,
                                                            alternative_header);

           return std::make_unique<RequestHeaderFormatter>(main_header, alternative_header,
                                                           max_length);
         }}}});
}

FormatterProviderPtr SubstitutionFormatParser::parseBuiltinCommand(const std::string& command,
                                                                   const std::string& subcommand,
                                                                   absl::optional<size_t>& length) {
  const FormatterProviderLookupTbl& providers = getKnownFormatters();

  auto it = providers.find(command);

  if (it == providers.end()) {
    return nullptr;
  }

  // Check flags for the command.
  CommandSyntaxChecker::verifySyntax((*it).second.first, command, subcommand, length);

  // Create a pointer to the formatter by calling a function
  // associated with formatter's name.
  return (*it).second.second(subcommand, length);
}

// StreamInfo std::string field extractor.
class StreamInfoStringFieldExtractor : public ContextIndependent::FieldExtractor {
public:
  using FieldExtractor = std::function<absl::optional<std::string>(const StreamInfo::StreamInfo&)>;

  StreamInfoStringFieldExtractor(FieldExtractor f) : field_extractor_(f) {}

  // ContextIndependent::FieldExtractor
  absl::optional<std::string> extract(const StreamInfo::StreamInfo& stream_info) const override {
    return field_extractor_(stream_info);
  }
  ProtobufWkt::Value extractValue(const StreamInfo::StreamInfo& stream_info) const override {
    return ValueUtil::optionalStringValue(field_extractor_(stream_info));
  }

private:
  FieldExtractor field_extractor_;
};

// StreamInfo std::chrono_nanoseconds field extractor.
class StreamInfoDurationFieldExtractor : public ContextIndependent::FieldExtractor {
public:
  using FieldExtractor =
      std::function<absl::optional<std::chrono::nanoseconds>(const StreamInfo::StreamInfo&)>;

  StreamInfoDurationFieldExtractor(FieldExtractor f) : field_extractor_(f) {}

  // ContextIndependent::FieldExtractor
  absl::optional<std::string> extract(const StreamInfo::StreamInfo& stream_info) const override {
    const auto millis = extractMillis(stream_info);
    if (!millis) {
      return absl::nullopt;
    }

    return fmt::format_int(millis.value()).str();
  }
  ProtobufWkt::Value extractValue(const StreamInfo::StreamInfo& stream_info) const override {
    const auto millis = extractMillis(stream_info);
    if (!millis) {
      return unspecifiedValue();
    }

    return ValueUtil::numberValue(millis.value());
  }

private:
  absl::optional<int64_t> extractMillis(const StreamInfo::StreamInfo& stream_info) const {
    const auto time = field_extractor_(stream_info);
    if (time) {
      return std::chrono::duration_cast<std::chrono::milliseconds>(time.value()).count();
    }
    return absl::nullopt;
  }

  FieldExtractor field_extractor_;
};

// StreamInfo uint64_t field extractor.
class StreamInfoUInt64FieldExtractor : public ContextIndependent::FieldExtractor {
public:
  using FieldExtractor = std::function<uint64_t(const StreamInfo::StreamInfo&)>;

  StreamInfoUInt64FieldExtractor(FieldExtractor f) : field_extractor_(f) {}

  // ContextIndependent::FieldExtractor
  absl::optional<std::string> extract(const StreamInfo::StreamInfo& stream_info) const override {
    return fmt::format_int(field_extractor_(stream_info)).str();
  }
  ProtobufWkt::Value extractValue(const StreamInfo::StreamInfo& stream_info) const override {
    return ValueUtil::numberValue(field_extractor_(stream_info));
  }

private:
  FieldExtractor field_extractor_;
};

// StreamInfo Envoy::Network::Address::InstanceConstSharedPtr field extractor.
class StreamInfoAddressFieldExtractor : public ContextIndependent::FieldExtractor {
public:
  using FieldExtractor =
      std::function<Network::Address::InstanceConstSharedPtr(const StreamInfo::StreamInfo&)>;

  static std::unique_ptr<StreamInfoAddressFieldExtractor> withPort(FieldExtractor f) {
    return std::make_unique<StreamInfoAddressFieldExtractor>(
        f, StreamInfoFormatter::StreamInfoAddressFieldExtractionType::WithPort);
  }

  static std::unique_ptr<StreamInfoAddressFieldExtractor> withoutPort(FieldExtractor f) {
    return std::make_unique<StreamInfoAddressFieldExtractor>(
        f, StreamInfoFormatter::StreamInfoAddressFieldExtractionType::WithoutPort);
  }

  static std::unique_ptr<StreamInfoAddressFieldExtractor> justPort(FieldExtractor f) {
    return std::make_unique<StreamInfoAddressFieldExtractor>(
        f, StreamInfoFormatter::StreamInfoAddressFieldExtractionType::JustPort);
  }

  StreamInfoAddressFieldExtractor(
      FieldExtractor f, StreamInfoFormatter::StreamInfoAddressFieldExtractionType extraction_type)
      : field_extractor_(f), extraction_type_(extraction_type) {}

  // ContextIndependent::FieldExtractor
  absl::optional<std::string> extract(const StreamInfo::StreamInfo& stream_info) const override {
    Network::Address::InstanceConstSharedPtr address = field_extractor_(stream_info);
    if (!address) {
      return absl::nullopt;
    }

    return toString(*address);
  }
  ProtobufWkt::Value extractValue(const StreamInfo::StreamInfo& stream_info) const override {
    Network::Address::InstanceConstSharedPtr address = field_extractor_(stream_info);
    if (!address) {
      return unspecifiedValue();
    }

    if (Runtime::runtimeFeatureEnabled("envoy.reloadable_features.format_ports_as_numbers")) {
      if (extraction_type_ == StreamInfoFormatter::StreamInfoAddressFieldExtractionType::JustPort) {
        const auto port = StreamInfo::Utility::extractDownstreamAddressJustPort(*address);
        if (port) {
          return ValueUtil::numberValue(*port);
        }
        return unspecifiedValue();
      }
    }
    return ValueUtil::stringValue(toString(*address));
  }

private:
  std::string toString(const Network::Address::Instance& address) const {
    switch (extraction_type_) {
    case StreamInfoFormatter::StreamInfoAddressFieldExtractionType::WithoutPort:
      return StreamInfo::Utility::formatDownstreamAddressNoPort(address);
    case StreamInfoFormatter::StreamInfoAddressFieldExtractionType::JustPort:
      return StreamInfo::Utility::formatDownstreamAddressJustPort(address);
    case StreamInfoFormatter::StreamInfoAddressFieldExtractionType::WithPort:
    default:
      return address.asString();
    }
  }

  FieldExtractor field_extractor_;
  const StreamInfoFormatter::StreamInfoAddressFieldExtractionType extraction_type_;
};

// Ssl::ConnectionInfo std::string field extractor.
class StreamInfoSslConnectionInfoFieldExtractor : public ContextIndependent::FieldExtractor {
public:
  using FieldExtractor =
      std::function<absl::optional<std::string>(const Ssl::ConnectionInfo& connection_info)>;

  StreamInfoSslConnectionInfoFieldExtractor(FieldExtractor f) : field_extractor_(f) {}

  absl::optional<std::string> extract(const StreamInfo::StreamInfo& stream_info) const override {
    if (stream_info.downstreamAddressProvider().sslConnection() == nullptr) {
      return absl::nullopt;
    }

    const auto value = field_extractor_(*stream_info.downstreamAddressProvider().sslConnection());
    if (value && value->empty()) {
      return absl::nullopt;
    }

    return value;
  }

  ProtobufWkt::Value extractValue(const StreamInfo::StreamInfo& stream_info) const override {
    if (stream_info.downstreamAddressProvider().sslConnection() == nullptr) {
      return unspecifiedValue();
    }

    const auto value = field_extractor_(*stream_info.downstreamAddressProvider().sslConnection());
    if (value && value->empty()) {
      return unspecifiedValue();
    }

    return ValueUtil::optionalStringValue(value);
  }

private:
  FieldExtractor field_extractor_;
};

class StreamInfoUpstreamSslConnectionInfoFieldExtractor
    : public ContextIndependent::FieldExtractor {
public:
  using FieldExtractor =
      std::function<absl::optional<std::string>(const Ssl::ConnectionInfo& connection_info)>;

  StreamInfoUpstreamSslConnectionInfoFieldExtractor(FieldExtractor f) : field_extractor_(f) {}

  absl::optional<std::string> extract(const StreamInfo::StreamInfo& stream_info) const override {
    if (!stream_info.upstreamInfo() ||
        stream_info.upstreamInfo()->upstreamSslConnection() == nullptr) {
      return absl::nullopt;
    }

    const auto value = field_extractor_(*(stream_info.upstreamInfo()->upstreamSslConnection()));
    if (value && value->empty()) {
      return absl::nullopt;
    }

    return value;
  }

  ProtobufWkt::Value extractValue(const StreamInfo::StreamInfo& stream_info) const override {
    if (!stream_info.upstreamInfo() ||
        stream_info.upstreamInfo()->upstreamSslConnection() == nullptr) {
      return unspecifiedValue();
    }

    const auto value = field_extractor_(*(stream_info.upstreamInfo()->upstreamSslConnection()));
    if (value && value->empty()) {
      return unspecifiedValue();
    }

    return ValueUtil::optionalStringValue(value);
  }

private:
  FieldExtractor field_extractor_;
};

class MetadataFieldExtractor : public ContextIndependent::FieldExtractor {
public:
  using GetMetadataFunction =
      std::function<const envoy::config::core::v3::Metadata*(const StreamInfo::StreamInfo&)>;
  MetadataFieldExtractor(const std::string& filter_namespace, const std::vector<std::string>& path,
                         absl::optional<size_t> max_length, GetMetadataFunction get_func)
      : filter_namespace_(filter_namespace), path_(path), max_length_(max_length),
        get_func_(get_func) {}

  absl::optional<std::string> extract(const StreamInfo::StreamInfo& stream_info) const override {

    auto metadata = get_func_(stream_info);
    return (metadata != nullptr) ? formatMetadata(*metadata) : absl::nullopt;
  }

  ProtobufWkt::Value extractValue(const StreamInfo::StreamInfo& stream_info) const override {
    auto metadata = get_func_(stream_info);
    return formatMetadataValue((metadata != nullptr) ? *metadata
                                                     : envoy::config::core::v3::Metadata());
  }

protected:
  absl::optional<std::string>
  formatMetadata(const envoy::config::core::v3::Metadata& metadata) const {
    ProtobufWkt::Value value = formatMetadataValue(metadata);
    if (value.kind_case() == ProtobufWkt::Value::kNullValue) {
      return absl::nullopt;
    }

    std::string str;
    if (value.kind_case() == ProtobufWkt::Value::kStringValue) {
      str = value.string_value();
    } else {
#ifdef ENVOY_ENABLE_YAML
      absl::StatusOr<std::string> json_or_error =
          MessageUtil::getJsonStringFromMessage(value, false, true);
      if (json_or_error.ok()) {
        str = json_or_error.value();
      } else {
        str = json_or_error.status().message();
      }
#else
      IS_ENVOY_BUG("Json support compiled out");
#endif
    }
    truncate(str, max_length_);
    return str;
  }

  ProtobufWkt::Value formatMetadataValue(const envoy::config::core::v3::Metadata& metadata) const {
    if (path_.empty()) {
      const auto filter_it = metadata.filter_metadata().find(filter_namespace_);
      if (filter_it == metadata.filter_metadata().end()) {
        return unspecifiedValue();
      }
      ProtobufWkt::Value output;
      output.mutable_struct_value()->CopyFrom(filter_it->second);
      return output;
    }

    const ProtobufWkt::Value& val = Metadata::metadataValue(&metadata, filter_namespace_, path_);
    if (val.kind_case() == ProtobufWkt::Value::KindCase::KIND_NOT_SET) {
      return unspecifiedValue();
    }

    return val;
  }

private:
  std::string filter_namespace_;
  std::vector<std::string> path_;
  absl::optional<size_t> max_length_;
  GetMetadataFunction get_func_;
};

/**
 * FormatterProvider for DynamicMetadata from StreamInfo.
 */
class DynamicMetadataFormatter : public MetadataFieldExtractor {
public:
  DynamicMetadataFormatter(const std::string& filter_namespace,
                           const std::vector<std::string>& path, absl::optional<size_t> max_length)
      : MetadataFieldExtractor(filter_namespace, path, max_length,
                               [](const StreamInfo::StreamInfo& stream_info) {
                                 return &stream_info.dynamicMetadata();
                               }) {}
};

/**
 * FormatterProvider for ClusterMetadata from StreamInfo.
 */
class ClusterMetadataFormatter : public MetadataFieldExtractor {
public:
  ClusterMetadataFormatter(const std::string& filter_namespace,
                           const std::vector<std::string>& path, absl::optional<size_t> max_length)
      : MetadataFieldExtractor(filter_namespace, path, max_length,
                               [](const StreamInfo::StreamInfo& stream_info)
                                   -> const envoy::config::core::v3::Metadata* {
                                 auto cluster_info = stream_info.upstreamClusterInfo();
                                 if (!cluster_info.has_value() || cluster_info.value() == nullptr) {
                                   return nullptr;
                                 }
                                 return &cluster_info.value()->metadata();
                               }) {}
};

/**
 * FormatterProvider for UpstreamHostMetadata from StreamInfo.
 */
class UpstreamHostMetadataFormatter : public MetadataFieldExtractor {
public:
  UpstreamHostMetadataFormatter(const std::string& filter_namespace,
                                const std::vector<std::string>& path,
                                absl::optional<size_t> max_length)
      : MetadataFieldExtractor(filter_namespace, path, max_length,
                               [](const StreamInfo::StreamInfo& stream_info)
                                   -> const envoy::config::core::v3::Metadata* {
                                 if (!stream_info.upstreamInfo().has_value()) {
                                   return nullptr;
                                 }
                                 Upstream::HostDescriptionConstSharedPtr host =
                                     stream_info.upstreamInfo()->upstreamHost();
                                 if (host == nullptr) {
                                   return nullptr;
                                 }
                                 return host->metadata().get();
                               }) {}
};

/**
 * FieldExtractor for FilterState from StreamInfo.
 */
class FilterStateFieldExtractor : public ContextIndependent::FieldExtractor {
public:
  static std::unique_ptr<FilterStateFieldExtractor>
  create(const std::string& format, const absl::optional<size_t>& max_length, bool is_upstream) {

    std::string key, serialize_type;
    static constexpr absl::string_view PLAIN_SERIALIZATION{"PLAIN"};
    static constexpr absl::string_view TYPED_SERIALIZATION{"TYPED"};

    SubstitutionFormatParser::parseSubcommand(format, ':', key, serialize_type);
    if (key.empty()) {
      throw EnvoyException("Invalid filter state configuration, key cannot be empty.");
    }

    if (serialize_type.empty()) {
      serialize_type = std::string(TYPED_SERIALIZATION);
    }
    if (serialize_type != PLAIN_SERIALIZATION && serialize_type != TYPED_SERIALIZATION) {
      throw EnvoyException("Invalid filter state serialize type, only "
                           "support PLAIN/TYPED.");
    }

    const bool serialize_as_string = serialize_type == PLAIN_SERIALIZATION;

    return std::make_unique<FilterStateFieldExtractor>(key, max_length, serialize_as_string,
                                                       is_upstream);
  }

  FilterStateFieldExtractor(const std::string& key, absl::optional<size_t> max_length,
                            bool serialize_as_string, bool is_upstream)
      : key_(key), max_length_(max_length), serialize_as_string_(serialize_as_string),
        is_upstream_(is_upstream) {}

  // FieldExtractor
  absl::optional<std::string> extract(const StreamInfo::StreamInfo& stream_info) const override {
    const Envoy::StreamInfo::FilterState::Object* state = filterState(stream_info);
    if (!state) {
      return absl::nullopt;
    }

    if (serialize_as_string_) {
      absl::optional<std::string> plain_value = state->serializeAsString();
      if (plain_value.has_value()) {
        truncate(plain_value.value(), max_length_);
        return plain_value.value();
      }
      return absl::nullopt;
    }

    ProtobufTypes::MessagePtr proto = state->serializeAsProto();
    if (proto == nullptr) {
      return absl::nullopt;
    }

    std::string value;
    const auto status = Protobuf::util::MessageToJsonString(*proto, &value);
    if (!status.ok()) {
      // If the message contains an unknown Any (from WASM or Lua), MessageToJsonString will fail.
      // TODO(lizan): add support of unknown Any.
      return absl::nullopt;
    }

    truncate(value, max_length_);
    return value;
  }
  ProtobufWkt::Value extractValue(const StreamInfo::StreamInfo& stream_info) const override {
    const Envoy::StreamInfo::FilterState::Object* state = filterState(stream_info);
    if (!state) {
      return unspecifiedValue();
    }

    if (serialize_as_string_) {
      absl::optional<std::string> plain_value = state->serializeAsString();
      if (plain_value.has_value()) {
        truncate(plain_value.value(), max_length_);
        return ValueUtil::stringValue(plain_value.value());
      }
      return unspecifiedValue();
    }

    ProtobufTypes::MessagePtr proto = state->serializeAsProto();
    if (!proto) {
      return unspecifiedValue();
    }

#ifdef ENVOY_ENABLE_YAML
    ProtobufWkt::Value val;
    if (MessageUtil::jsonConvertValue(*proto, val)) {
      return val;
    }
#endif
    return unspecifiedValue();
  }

private:
  const Envoy::StreamInfo::FilterState::Object*
  filterState(const StreamInfo::StreamInfo& stream_info) const {
    const StreamInfo::FilterState* filter_state = nullptr;
    if (is_upstream_) {
      const OptRef<const StreamInfo::UpstreamInfo> upstream_info = stream_info.upstreamInfo();
      if (upstream_info) {
        filter_state = upstream_info->upstreamFilterState().get();
      }
    } else {
      filter_state = &stream_info.filterState();
    }

    if (filter_state) {
      return filter_state->getDataReadOnly<StreamInfo::FilterState::Object>(key_);
    }

    return nullptr;
  }

  std::string key_;
  absl::optional<size_t> max_length_;

  bool serialize_as_string_;
  const bool is_upstream_;
};

/**
 * Base FieldExtractor for system times from StreamInfo.
 */
class SystemTimeFieldExtractor : public FieldExtractor {
public:
  using TimeFieldExtractor =
      std::function<absl::optional<SystemTime>(const StreamInfo::StreamInfo& stream_info)>;
  using TimeFieldExtractorPtr = std::unique_ptr<TimeFieldExtractor>;

  SystemTimeFieldExtractor(const std::string& format, TimeFieldExtractorPtr f)
      : date_formatter_(format), time_field_extractor_(std::move(f)) {
    // Validate the input specifier here. The formatted string may be destined for a header, and
    // should not contain invalid characters {NUL, LR, CF}.
    if (std::regex_search(format, getSystemTimeFormatNewlinePattern())) {
      throw EnvoyException("Invalid header configuration. Format string contains newline.");
    }
  }

  // FieldExtractor
  absl::optional<std::string> extract(const StreamInfo::StreamInfo& stream_info) const override {
    const auto time_field = (*time_field_extractor_)(stream_info);
    if (!time_field.has_value()) {
      return absl::nullopt;
    }
    if (date_formatter_.formatString().empty()) {
      return AccessLogDateTimeFormatter::fromTime(time_field.value());
    }
    return date_formatter_.fromTime(time_field.value());
  }
  ProtobufWkt::Value extractValue(const StreamInfo::StreamInfo& stream_info) const override {
    return ValueUtil::optionalStringValue(extract(stream_info));
  }

private:
  const Envoy::DateFormatter date_formatter_;
  const TimeFieldExtractorPtr time_field_extractor_;
};

/**
 * SystemTimeFieldExtractor (FormatterProvider) for request start time from StreamInfo.
 */
class StartTimeFieldExtractor : public SystemTimeFieldExtractor {
public:
  // A SystemTime formatter that extracts the startTime from StreamInfo. Must be provided
  // an access log command that starts with `START_TIME`.
  StartTimeFormatter(const std::string& format)
      : SystemTimeFieldExtractor(
            format,
            std::make_unique<SystemTimeFieldExtractor::TimeFieldExtractor>(
                [](const StreamInfo::StreamInfo& stream_info) -> absl::optional<SystemTime> {
                  return stream_info.startTime();
                })) {}
};

/**
 * SystemTimeFieldExtractor (FormatterProvider) for downstream cert start time from the StreamInfo's
 * ConnectionInfo.
 */
class DownstreamPeerCertVStartFieldExtractor : public SystemTimeFieldExtractor {
public:
  DownstreamPeerCertVStartFieldExtractor(const std::string& format)
      : SystemTimeFieldExtractor(
            format,
            std::make_unique<SystemTimeFieldExtractor::TimeFieldExtractor>(
                [](const StreamInfo::StreamInfo& stream_info) -> absl::optional<SystemTime> {
                  const auto connection_info =
                      stream_info.downstreamAddressProvider().sslConnection();
                  return connection_info != nullptr ? connection_info->validFromPeerCertificate()
                                                    : absl::optional<SystemTime>();
                })) {}
};

/**
 * SystemTimeFieldExtractor (FormatterProvider) for downstream cert end time from the StreamInfo's
 * ConnectionInfo.
 */
class DownstreamPeerCertVEndFieldExtractor : public SystemTimeFieldExtractor {
public:
  DownstreamPeerCertVEndFieldExtractor(const std::string& format)
      : SystemTimeFieldExtractor(
            format,
            std::make_unique<SystemTimeFieldExtractor::TimeFieldExtractor>(
                [](const StreamInfo::StreamInfo& stream_info) -> absl::optional<SystemTime> {
                  const auto connection_info =
                      stream_info.downstreamAddressProvider().sslConnection();
                  return connection_info != nullptr ? connection_info->expirationPeerCertificate()
                                                    : absl::optional<SystemTime>();
                })) {}
};

/**
 * SystemTimeFieldExtractor (FormatterProvider) for upstream cert start time from the StreamInfo's
 * upstreamInfo.
 */
class UpstreamPeerCertVStartFieldExtractor : public SystemTimeFieldExtractor {
public:
  UpstreamPeerCertVStartFieldExtractor(const std::string& format)
      : SystemTimeFieldExtractor(
            format,
            std::make_unique<SystemTimeFieldExtractor::TimeFieldExtractor>(
                [](const StreamInfo::StreamInfo& stream_info) -> absl::optional<SystemTime> {
                  return stream_info.upstreamInfo() &&
                                 stream_info.upstreamInfo()->upstreamSslConnection() != nullptr
                             ? stream_info.upstreamInfo()
                                   ->upstreamSslConnection()
                                   ->validFromPeerCertificate()
                             : absl::optional<SystemTime>();
                })) {}
};

/**
 * SystemTimeFieldExtractor for upstream cert end time from the StreamInfo's
 * upstreamInfo.
 */
class UpstreamPeerCertVEndFieldExtractor : public SystemTimeFieldExtractor {
public:
  UpstreamPeerCertVEndFieldExtractor(const std::string& format)
      : SystemTimeFieldExtractor(
            format,
            std::make_unique<SystemTimeFieldExtractor::TimeFieldExtractor>(
                [](const StreamInfo::StreamInfo& stream_info) -> absl::optional<SystemTime> {
                  return stream_info.upstreamInfo() &&
                                 stream_info.upstreamInfo()->upstreamSslConnection() != nullptr
                             ? stream_info.upstreamInfo()
                                   ->upstreamSslConnection()
                                   ->expirationPeerCertificate()
                             : absl::optional<SystemTime>();
                })) {}
};

/**
 * FormatterProvider for environment. If no valid environment value then
 */
class EnvironmentFieldExtractor : public FieldExtractor {
public:
  EnvironmentFieldExtractor(const std::string& key, absl::optional<size_t> max_length) {
    ASSERT(!key.empty());

    const char* env_value = std::getenv(key.c_str());
    if (env_value != nullptr) {
      std::string env_string = env_value;
      truncate(env_string, max_length);
      str_.set_string_value(env_string);
      return;
    }
    str_.set_string_value(DefaultUnspecifiedValueString);
  }

  // FormatterProvider
  absl::optional<std::string> extract(const StreamInfo::StreamInfo&, ) const override {
    return str_.string_value();
  }
  ProtobufWkt::Value extractValue(const StreamInfo::StreamInfo&) const override { return str_; }

private:
  ProtobufWkt::Value str_;
};

const ContextIndependent::FieldExtractorLookupTbl&
ContextIndependent::getKnownContextIndependentFieldExtractors() {
  CONSTRUCT_ON_FIRST_USE(FieldExtractorLookupTbl,
                         {{"REQUEST_DURATION",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return std::make_unique<StreamInfoDurationFieldExtractor>(
                                  [](const StreamInfo::StreamInfo& stream_info) {
                                    StreamInfo::TimingUtility timing(stream_info);
                                    return timing.lastDownstreamRxByteReceived();
                                  });
                            }}},
                          {"REQUEST_TX_DURATION",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return std::make_unique<StreamInfoDurationFieldExtractor>(
                                  [](const StreamInfo::StreamInfo& stream_info) {
                                    StreamInfo::TimingUtility timing(stream_info);
                                    return timing.lastUpstreamTxByteSent();
                                  });
                            }}},
                          {"RESPONSE_DURATION",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return std::make_unique<StreamInfoDurationFieldExtractor>(
                                  [](const StreamInfo::StreamInfo& stream_info) {
                                    StreamInfo::TimingUtility timing(stream_info);
                                    return timing.firstUpstreamRxByteReceived();
                                  });
                            }}},
                          {"RESPONSE_TX_DURATION",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return std::make_unique<StreamInfoDurationFieldExtractor>(
                                  [](const StreamInfo::StreamInfo& stream_info) {
                                    StreamInfo::TimingUtility timing(stream_info);
                                    auto downstream = timing.lastDownstreamTxByteSent();
                                    auto upstream = timing.firstUpstreamRxByteReceived();

                                    absl::optional<std::chrono::nanoseconds> result;
                                    if (downstream && upstream) {
                                      result = downstream.value() - upstream.value();
                                    }

                                    return result;
                                  });
                            }}},
                          {"DOWNSTREAM_HANDSHAKE_DURATION",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return std::make_unique<StreamInfoDurationFieldExtractor>(
                                  [](const StreamInfo::StreamInfo& stream_info) {
                                    StreamInfo::TimingUtility timing(stream_info);
                                    return timing.downstreamHandshakeComplete();
                                  });
                            }}},
                          {"ROUNDTRIP_DURATION",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return std::make_unique<StreamInfoDurationFieldExtractor>(
                                  [](const StreamInfo::StreamInfo& stream_info) {
                                    StreamInfo::TimingUtility timing(stream_info);
                                    return timing.lastDownstreamAckReceived();
                                  });
                            }}},
                          {"BYTES_RECEIVED",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return std::make_unique<StreamInfoUInt64FieldExtractor>(
                                  [](const StreamInfo::StreamInfo& stream_info) {
                                    return stream_info.bytesReceived();
                                  });
                            }}},
                          {"BYTES_RETRANSMITTED",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return std::make_unique<StreamInfoUInt64FieldExtractor>(
                                  [](const StreamInfo::StreamInfo& stream_info) {
                                    return stream_info.bytesRetransmitted();
                                  });
                            }}},
                          {"PACKETS_RETRANSMITTED",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return std::make_unique<StreamInfoUInt64FieldExtractor>(
                                  [](const StreamInfo::StreamInfo& stream_info) {
                                    return stream_info.packetsRetransmitted();
                                  });
                            }}},
                          {"UPSTREAM_WIRE_BYTES_RECEIVED",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return std::make_unique<StreamInfoUInt64FieldExtractor>(
                                  [](const StreamInfo::StreamInfo& stream_info) {
                                    auto bytes_meter = stream_info.getUpstreamBytesMeter();
                                    return bytes_meter ? bytes_meter->wireBytesReceived() : 0;
                                  });
                            }}},
                          {"UPSTREAM_HEADER_BYTES_RECEIVED",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return std::make_unique<StreamInfoUInt64FieldExtractor>(
                                  [](const StreamInfo::StreamInfo& stream_info) {
                                    auto bytes_meter = stream_info.getUpstreamBytesMeter();
                                    return bytes_meter ? bytes_meter->headerBytesReceived() : 0;
                                  });
                            }}},
                          {"DOWNSTREAM_WIRE_BYTES_RECEIVED",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return std::make_unique<StreamInfoUInt64FieldExtractor>(
                                  [](const StreamInfo::StreamInfo& stream_info) {
                                    auto bytes_meter = stream_info.getDownstreamBytesMeter();
                                    return bytes_meter ? bytes_meter->wireBytesReceived() : 0;
                                  });
                            }}},
                          {"DOWNSTREAM_HEADER_BYTES_RECEIVED",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return std::make_unique<StreamInfoUInt64FieldExtractor>(
                                  [](const StreamInfo::StreamInfo& stream_info) {
                                    auto bytes_meter = stream_info.getDownstreamBytesMeter();
                                    return bytes_meter ? bytes_meter->headerBytesReceived() : 0;
                                  });
                            }}},
                          {"PROTOCOL",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return std::make_unique<StreamInfoStringFieldExtractor>(
                                  [](const StreamInfo::StreamInfo& stream_info) {
                                    return SubstitutionFormatUtils::protocolToString(
                                        stream_info.protocol());
                                  });
                            }}},
                          {"UPSTREAM_PROTOCOL",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return std::make_unique<StreamInfoStringFieldExtractor>(
                                  [](const StreamInfo::StreamInfo& stream_info) {
                                    return stream_info.upstreamInfo()
                                               ? SubstitutionFormatUtils::protocolToString(
                                                     stream_info.upstreamInfo()->upstreamProtocol())
                                               : absl::nullopt;
                                  });
                            }}},
                          {"RESPONSE_CODE",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return std::make_unique<StreamInfoUInt64FieldExtractor>(
                                  [](const StreamInfo::StreamInfo& stream_info) {
                                    return stream_info.responseCode().value_or(0);
                                  });
                            }}},
                          {"RESPONSE_CODE_DETAILS",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return std::make_unique<StreamInfoStringFieldExtractor>(
                                  [](const StreamInfo::StreamInfo& stream_info) {
                                    return stream_info.responseCodeDetails();
                                  });
                            }}},
                          {"CONNECTION_TERMINATION_DETAILS",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return std::make_unique<StreamInfoStringFieldExtractor>(
                                  [](const StreamInfo::StreamInfo& stream_info) {
                                    return stream_info.connectionTerminationDetails();
                                  });
                            }}},
                          {"BYTES_SENT",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return std::make_unique<StreamInfoUInt64FieldExtractor>(
                                  [](const StreamInfo::StreamInfo& stream_info) {
                                    return stream_info.bytesSent();
                                  });
                            }}},
                          {"UPSTREAM_WIRE_BYTES_SENT",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return std::make_unique<StreamInfoUInt64FieldExtractor>(
                                  [](const StreamInfo::StreamInfo& stream_info) {
                                    const auto& bytes_meter = stream_info.getUpstreamBytesMeter();
                                    return bytes_meter ? bytes_meter->wireBytesSent() : 0;
                                  });
                            }}},
                          {"UPSTREAM_HEADER_BYTES_SENT",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return std::make_unique<StreamInfoUInt64FieldExtractor>(
                                  [](const StreamInfo::StreamInfo& stream_info) {
                                    const auto& bytes_meter = stream_info.getUpstreamBytesMeter();
                                    return bytes_meter ? bytes_meter->headerBytesSent() : 0;
                                  });
                            }}},
                          {"DOWNSTREAM_WIRE_BYTES_SENT",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return std::make_unique<StreamInfoUInt64FieldExtractor>(
                                  [](const StreamInfo::StreamInfo& stream_info) {
                                    const auto& bytes_meter = stream_info.getDownstreamBytesMeter();
                                    return bytes_meter ? bytes_meter->wireBytesSent() : 0;
                                  });
                            }}},
                          {"DOWNSTREAM_HEADER_BYTES_SENT",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return std::make_unique<StreamInfoUInt64FieldExtractor>(
                                  [](const StreamInfo::StreamInfo& stream_info) {
                                    const auto& bytes_meter = stream_info.getDownstreamBytesMeter();
                                    return bytes_meter ? bytes_meter->headerBytesSent() : 0;
                                  });
                            }}},
                          {"DURATION",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return std::make_unique<StreamInfoDurationFieldExtractor>(
                                  [](const StreamInfo::StreamInfo& stream_info) {
                                    return stream_info.currentDuration();
                                  });
                            }}},
                          {"RESPONSE_FLAGS",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return std::make_unique<StreamInfoStringFieldExtractor>(
                                  [](const StreamInfo::StreamInfo& stream_info) {
                                    return StreamInfo::ResponseFlagUtils::toShortString(
                                        stream_info);
                                  });
                            }}},
                          {"RESPONSE_FLAGS_LONG",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return std::make_unique<StreamInfoStringFieldExtractor>(
                                  [](const StreamInfo::StreamInfo& stream_info) {
                                    return StreamInfo::ResponseFlagUtils::toString(stream_info);
                                  });
                            }}},
                          {"UPSTREAM_HOST",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return StreamInfoAddressFieldExtractor::withPort(
                                  [](const StreamInfo::StreamInfo& stream_info)
                                      -> std::shared_ptr<const Envoy::Network::Address::Instance> {
                                    if (stream_info.upstreamInfo() &&
                                        stream_info.upstreamInfo()->upstreamHost()) {
                                      return stream_info.upstreamInfo()->upstreamHost()->address();
                                    }
                                    return nullptr;
                                  });
                            }}},
                          {"UPSTREAM_CLUSTER",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return std::make_unique<StreamInfoStringFieldExtractor>(
                                  [](const StreamInfo::StreamInfo& stream_info) {
                                    std::string upstream_cluster_name;
                                    if (stream_info.upstreamClusterInfo().has_value() &&
                                        stream_info.upstreamClusterInfo().value() != nullptr) {
                                      upstream_cluster_name = stream_info.upstreamClusterInfo()
                                                                  .value()
                                                                  ->observabilityName();
                                    }

                                    return upstream_cluster_name.empty()
                                               ? absl::nullopt
                                               : absl::make_optional<std::string>(
                                                     upstream_cluster_name);
                                  });
                            }}},
                          {"UPSTREAM_LOCAL_ADDRESS",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return StreamInfoAddressFieldExtractor::withPort(
                                  [](const StreamInfo::StreamInfo& stream_info)
                                      -> std::shared_ptr<const Envoy::Network::Address::Instance> {
                                    if (stream_info.upstreamInfo().has_value()) {
                                      return stream_info.upstreamInfo()
                                          .value()
                                          .get()
                                          .upstreamLocalAddress();
                                    }
                                    return nullptr;
                                  });
                            }}},
                          {"UPSTREAM_LOCAL_ADDRESS_WITHOUT_PORT",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return StreamInfoAddressFieldExtractor::withoutPort(
                                  [](const StreamInfo::StreamInfo& stream_info)
                                      -> std::shared_ptr<const Envoy::Network::Address::Instance> {
                                    if (stream_info.upstreamInfo().has_value()) {
                                      return stream_info.upstreamInfo()
                                          .value()
                                          .get()
                                          .upstreamLocalAddress();
                                    }
                                    return nullptr;
                                  });
                            }}},
                          {"UPSTREAM_LOCAL_PORT",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return StreamInfoAddressFieldExtractor::justPort(
                                  [](const StreamInfo::StreamInfo& stream_info)
                                      -> std::shared_ptr<const Envoy::Network::Address::Instance> {
                                    if (stream_info.upstreamInfo().has_value()) {
                                      return stream_info.upstreamInfo()
                                          .value()
                                          .get()
                                          .upstreamLocalAddress();
                                    }
                                    return nullptr;
                                  });
                            }}},
                          {"UPSTREAM_REMOTE_ADDRESS",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return StreamInfoAddressFieldExtractor::withPort(
                                  [](const StreamInfo::StreamInfo& stream_info)
                                      -> std::shared_ptr<const Envoy::Network::Address::Instance> {
                                    if (stream_info.upstreamInfo() &&
                                        stream_info.upstreamInfo()->upstreamHost()) {
                                      return stream_info.upstreamInfo()->upstreamHost()->address();
                                    }
                                    return nullptr;
                                  });
                            }}},
                          {"UPSTREAM_REMOTE_ADDRESS_WITHOUT_PORT",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return StreamInfoAddressFieldExtractor::withoutPort(
                                  [](const StreamInfo::StreamInfo& stream_info)
                                      -> std::shared_ptr<const Envoy::Network::Address::Instance> {
                                    if (stream_info.upstreamInfo() &&
                                        stream_info.upstreamInfo()->upstreamHost()) {
                                      return stream_info.upstreamInfo()->upstreamHost()->address();
                                    }
                                    return nullptr;
                                  });
                            }}},
                          {"UPSTREAM_REMOTE_PORT",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return StreamInfoAddressFieldExtractor::justPort(
                                  [](const StreamInfo::StreamInfo& stream_info)
                                      -> std::shared_ptr<const Envoy::Network::Address::Instance> {
                                    if (stream_info.upstreamInfo() &&
                                        stream_info.upstreamInfo()->upstreamHost()) {
                                      return stream_info.upstreamInfo()->upstreamHost()->address();
                                    }
                                    return nullptr;
                                  });
                            }}},
                          {"UPSTREAM_REQUEST_ATTEMPT_COUNT",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return std::make_unique<StreamInfoUInt64FieldExtractor>(
                                  [](const StreamInfo::StreamInfo& stream_info) {
                                    return stream_info.attemptCount().value_or(0);
                                  });
                            }}},
                          {"UPSTREAM_TLS_CIPHER",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return std::make_unique<
                                  StreamInfoUpstreamSslConnectionInfoFieldExtractor>(
                                  [](const Ssl::ConnectionInfo& connection_info) {
                                    return connection_info.ciphersuiteString();
                                  });
                            }}},
                          {"UPSTREAM_TLS_VERSION",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return std::make_unique<
                                  StreamInfoUpstreamSslConnectionInfoFieldExtractor>(
                                  [](const Ssl::ConnectionInfo& connection_info) {
                                    return connection_info.tlsVersion();
                                  });
                            }}},
                          {"UPSTREAM_TLS_SESSION_ID",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return std::make_unique<
                                  StreamInfoUpstreamSslConnectionInfoFieldExtractor>(
                                  [](const Ssl::ConnectionInfo& connection_info) {
                                    return connection_info.sessionId();
                                  });
                            }}},
                          {"UPSTREAM_PEER_ISSUER",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return std::make_unique<
                                  StreamInfoUpstreamSslConnectionInfoFieldExtractor>(
                                  [](const Ssl::ConnectionInfo& connection_info) {
                                    return connection_info.issuerPeerCertificate();
                                  });
                            }}},
                          {"UPSTREAM_PEER_CERT",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return std::make_unique<
                                  StreamInfoUpstreamSslConnectionInfoFieldExtractor>(
                                  [](const Ssl::ConnectionInfo& connection_info) {
                                    return connection_info.urlEncodedPemEncodedPeerCertificate();
                                  });
                            }}},
                          {"UPSTREAM_PEER_SUBJECT",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return std::make_unique<
                                  StreamInfoUpstreamSslConnectionInfoFieldExtractor>(
                                  [](const Ssl::ConnectionInfo& connection_info) {
                                    return connection_info.subjectPeerCertificate();
                                  });
                            }}},
                          {"DOWNSTREAM_LOCAL_ADDRESS",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return StreamInfoAddressFieldExtractor::withPort(
                                  [](const StreamInfo::StreamInfo& stream_info) {
                                    return stream_info.downstreamAddressProvider().localAddress();
                                  });
                            }}},
                          {"DOWNSTREAM_LOCAL_ADDRESS_WITHOUT_PORT",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return StreamInfoAddressFieldExtractor::withoutPort(
                                  [](const Envoy::StreamInfo::StreamInfo& stream_info) {
                                    return stream_info.downstreamAddressProvider().localAddress();
                                  });
                            }}},
                          {"DOWNSTREAM_LOCAL_PORT",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return StreamInfoAddressFieldExtractor::justPort(
                                  [](const Envoy::StreamInfo::StreamInfo& stream_info) {
                                    return stream_info.downstreamAddressProvider().localAddress();
                                  });
                            }}},
                          {"DOWNSTREAM_REMOTE_ADDRESS",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return StreamInfoAddressFieldExtractor::withPort(
                                  [](const StreamInfo::StreamInfo& stream_info) {
                                    return stream_info.downstreamAddressProvider().remoteAddress();
                                  });
                            }}},
                          {"DOWNSTREAM_REMOTE_ADDRESS_WITHOUT_PORT",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return StreamInfoAddressFieldExtractor::withoutPort(
                                  [](const StreamInfo::StreamInfo& stream_info) {
                                    return stream_info.downstreamAddressProvider().remoteAddress();
                                  });
                            }}},
                          {"DOWNSTREAM_REMOTE_PORT",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return StreamInfoAddressFieldExtractor::justPort(
                                  [](const Envoy::StreamInfo::StreamInfo& stream_info) {
                                    return stream_info.downstreamAddressProvider().remoteAddress();
                                  });
                            }}},
                          {"DOWNSTREAM_DIRECT_REMOTE_ADDRESS",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return StreamInfoAddressFieldExtractor::withPort(
                                  [](const StreamInfo::StreamInfo& stream_info) {
                                    return stream_info.downstreamAddressProvider()
                                        .directRemoteAddress();
                                  });
                            }}},
                          {"DOWNSTREAM_DIRECT_REMOTE_ADDRESS_WITHOUT_PORT",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return StreamInfoAddressFieldExtractor::withoutPort(
                                  [](const StreamInfo::StreamInfo& stream_info) {
                                    return stream_info.downstreamAddressProvider()
                                        .directRemoteAddress();
                                  });
                            }}},
                          {"DOWNSTREAM_DIRECT_REMOTE_PORT",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return StreamInfoAddressFieldExtractor::justPort(
                                  [](const Envoy::StreamInfo::StreamInfo& stream_info) {
                                    return stream_info.downstreamAddressProvider()
                                        .directRemoteAddress();
                                  });
                            }}},
                          {"CONNECTION_ID",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return std::make_unique<StreamInfoUInt64FieldExtractor>(
                                  [](const StreamInfo::StreamInfo& stream_info) {
                                    return stream_info.downstreamAddressProvider()
                                        .connectionID()
                                        .value_or(0);
                                  });
                            }}},
                          {"REQUESTED_SERVER_NAME",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return std::make_unique<StreamInfoStringFieldExtractor>(
                                  [](const StreamInfo::StreamInfo& stream_info) {
                                    absl::optional<std::string> result;
                                    if (!stream_info.downstreamAddressProvider()
                                             .requestedServerName()
                                             .empty()) {
                                      result = std::string(stream_info.downstreamAddressProvider()
                                                               .requestedServerName());
                                    }
                                    return result;
                                  });
                            }}},
                          {"ROUTE_NAME",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return std::make_unique<StreamInfoStringFieldExtractor>(
                                  [](const StreamInfo::StreamInfo& stream_info) {
                                    absl::optional<std::string> result;
                                    std::string route_name = stream_info.getRouteName();
                                    if (!route_name.empty()) {
                                      result = route_name;
                                    }
                                    return result;
                                  });
                            }}},
                          {"DOWNSTREAM_PEER_URI_SAN",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return std::make_unique<StreamInfoSslConnectionInfoFieldExtractor>(
                                  [](const Ssl::ConnectionInfo& connection_info) {
                                    return absl::StrJoin(connection_info.uriSanPeerCertificate(),
                                                         ",");
                                  });
                            }}},
                          {"DOWNSTREAM_PEER_DNS_SAN",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return std::make_unique<StreamInfoSslConnectionInfoFieldExtractor>(
                                  [](const Ssl::ConnectionInfo& connection_info) {
                                    return absl::StrJoin(connection_info.dnsSansPeerCertificate(),
                                                         ",");
                                  });
                            }}},
                          {"DOWNSTREAM_PEER_IP_SAN",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return std::make_unique<StreamInfoSslConnectionInfoFieldExtractor>(
                                  [](const Ssl::ConnectionInfo& connection_info) {
                                    return absl::StrJoin(connection_info.ipSansPeerCertificate(),
                                                         ",");
                                  });
                            }}},
                          {"DOWNSTREAM_LOCAL_URI_SAN",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return std::make_unique<StreamInfoSslConnectionInfoFieldExtractor>(
                                  [](const Ssl::ConnectionInfo& connection_info) {
                                    return absl::StrJoin(connection_info.uriSanLocalCertificate(),
                                                         ",");
                                  });
                            }}},
                          {"DOWNSTREAM_LOCAL_DNS_SAN",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return std::make_unique<StreamInfoSslConnectionInfoFieldExtractor>(
                                  [](const Ssl::ConnectionInfo& connection_info) {
                                    return absl::StrJoin(connection_info.dnsSansLocalCertificate(),
                                                         ",");
                                  });
                            }}},
                          {"DOWNSTREAM_LOCAL_IP_SAN",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return std::make_unique<StreamInfoSslConnectionInfoFieldExtractor>(
                                  [](const Ssl::ConnectionInfo& connection_info) {
                                    return absl::StrJoin(connection_info.ipSansLocalCertificate(),
                                                         ",");
                                  });
                            }}},
                          {"DOWNSTREAM_PEER_SUBJECT",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return std::make_unique<StreamInfoSslConnectionInfoFieldExtractor>(
                                  [](const Ssl::ConnectionInfo& connection_info) {
                                    return connection_info.subjectPeerCertificate();
                                  });
                            }}},
                          {"DOWNSTREAM_LOCAL_SUBJECT",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return std::make_unique<StreamInfoSslConnectionInfoFieldExtractor>(
                                  [](const Ssl::ConnectionInfo& connection_info) {
                                    return connection_info.subjectLocalCertificate();
                                  });
                            }}},
                          {"DOWNSTREAM_TLS_SESSION_ID",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return std::make_unique<StreamInfoSslConnectionInfoFieldExtractor>(
                                  [](const Ssl::ConnectionInfo& connection_info) {
                                    return connection_info.sessionId();
                                  });
                            }}},
                          {"DOWNSTREAM_TLS_CIPHER",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return std::make_unique<StreamInfoSslConnectionInfoFieldExtractor>(
                                  [](const Ssl::ConnectionInfo& connection_info) {
                                    return connection_info.ciphersuiteString();
                                  });
                            }}},
                          {"DOWNSTREAM_TLS_VERSION",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return std::make_unique<StreamInfoSslConnectionInfoFieldExtractor>(
                                  [](const Ssl::ConnectionInfo& connection_info) {
                                    return connection_info.tlsVersion();
                                  });
                            }}},
                          {"DOWNSTREAM_PEER_FINGERPRINT_256",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return std::make_unique<StreamInfoSslConnectionInfoFieldExtractor>(
                                  [](const Ssl::ConnectionInfo& connection_info) {
                                    return connection_info.sha256PeerCertificateDigest();
                                  });
                            }}},
                          {"DOWNSTREAM_PEER_FINGERPRINT_1",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return std::make_unique<StreamInfoSslConnectionInfoFieldExtractor>(
                                  [](const Ssl::ConnectionInfo& connection_info) {
                                    return connection_info.sha1PeerCertificateDigest();
                                  });
                            }}},
                          {"DOWNSTREAM_PEER_SERIAL",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return std::make_unique<StreamInfoSslConnectionInfoFieldExtractor>(
                                  [](const Ssl::ConnectionInfo& connection_info) {
                                    return connection_info.serialNumberPeerCertificate();
                                  });
                            }}},
                          {"DOWNSTREAM_PEER_ISSUER",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return std::make_unique<StreamInfoSslConnectionInfoFieldExtractor>(
                                  [](const Ssl::ConnectionInfo& connection_info) {
                                    return connection_info.issuerPeerCertificate();
                                  });
                            }}},
                          {"DOWNSTREAM_PEER_CERT",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return std::make_unique<StreamInfoSslConnectionInfoFieldExtractor>(
                                  [](const Ssl::ConnectionInfo& connection_info) {
                                    return connection_info.urlEncodedPemEncodedPeerCertificate();
                                  });
                            }}},
                          {"DOWNSTREAM_TRANSPORT_FAILURE_REASON",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return std::make_unique<StreamInfoStringFieldExtractor>(
                                  [](const StreamInfo::StreamInfo& stream_info) {
                                    absl::optional<std::string> result;
                                    if (!stream_info.downstreamTransportFailureReason().empty()) {
                                      result = absl::StrReplaceAll(
                                          stream_info.downstreamTransportFailureReason(),
                                          {{" ", "_"}});
                                    }
                                    return result;
                                  });
                            }}},
                          {"UPSTREAM_TRANSPORT_FAILURE_REASON",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return std::make_unique<StreamInfoStringFieldExtractor>(
                                  [](const StreamInfo::StreamInfo& stream_info) {
                                    absl::optional<std::string> result;
                                    if (stream_info.upstreamInfo().has_value() &&
                                        !stream_info.upstreamInfo()
                                             .value()
                                             .get()
                                             .upstreamTransportFailureReason()
                                             .empty()) {
                                      result = stream_info.upstreamInfo()
                                                   .value()
                                                   .get()
                                                   .upstreamTransportFailureReason();
                                    }
                                    if (result) {
                                      std::replace(result->begin(), result->end(), ' ', '_');
                                    }
                                    return result;
                                  });
                            }}},
                          {"HOSTNAME",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              absl::optional<std::string> hostname =
                                  SubstitutionFormatUtils::getHostname();
                              return std::make_unique<StreamInfoStringFieldExtractor>(
                                  [hostname](const StreamInfo::StreamInfo&) { return hostname; });
                            }}},
                          {"FILTER_CHAIN_NAME",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return std::make_unique<StreamInfoStringFieldExtractor>(
                                  [](const StreamInfo::StreamInfo& stream_info)
                                      -> absl::optional<std::string> {
                                    if (!stream_info.filterChainName().empty()) {
                                      return stream_info.filterChainName();
                                    }
                                    return absl::nullopt;
                                  });
                            }}},
                          {"VIRTUAL_CLUSTER_NAME",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return std::make_unique<StreamInfoStringFieldExtractor>(
                                  [](const StreamInfo::StreamInfo& stream_info)
                                      -> absl::optional<std::string> {
                                    return stream_info.virtualClusterName();
                                  });
                            }}},
                          {"TLS_JA3_FINGERPRINT",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return std::make_unique<StreamInfoStringFieldExtractor>(
                                  [](const StreamInfo::StreamInfo& stream_info) {
                                    absl::optional<std::string> result;
                                    if (!stream_info.downstreamAddressProvider()
                                             .ja3Hash()
                                             .empty()) {
                                      result = std::string(
                                          stream_info.downstreamAddressProvider().ja3Hash());
                                    }
                                    return result;
                                  });
                            }}},
                          {"STREAM_ID",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return std::make_unique<StreamInfoStringFieldExtractor>(
                                  [](const StreamInfo::StreamInfo& stream_info)
                                      -> absl::optional<std::string> {
                                    auto provider = stream_info.getStreamIdProvider();
                                    if (!provider.has_value()) {
                                      return {};
                                    }
                                    auto id = provider->toStringView();
                                    if (!id.has_value()) {
                                      return {};
                                    }
                                    return absl::make_optional<std::string>(id.value());
                                  });
                            }}}});
}

PlainStringFormatter::PlainStringFormatter(const std::string& str) { str_.set_string_value(str); }

absl::optional<std::string>
PlainStringFormatter::format(const Http::RequestHeaderMap&, const Http::ResponseHeaderMap&,
                             const Http::ResponseTrailerMap&, const StreamInfo::StreamInfo&,
                             absl::string_view, AccessLog::AccessLogType) const {
  return str_.string_value();
}

ProtobufWkt::Value
PlainStringFormatter::formatValue(const Http::RequestHeaderMap&, const Http::ResponseHeaderMap&,
                                  const Http::ResponseTrailerMap&, const StreamInfo::StreamInfo&,
                                  absl::string_view, AccessLog::AccessLogType) const {
  return str_;
}

PlainNumberFormatter::PlainNumberFormatter(double num) { num_.set_number_value(num); }

absl::optional<std::string>
PlainNumberFormatter::format(const Http::RequestHeaderMap&, const Http::ResponseHeaderMap&,
                             const Http::ResponseTrailerMap&, const StreamInfo::StreamInfo&,
                             absl::string_view, AccessLog::AccessLogType) const {
  std::string str = absl::StrFormat("%g", num_.number_value());
  return str;
}

ProtobufWkt::Value
PlainNumberFormatter::formatValue(const Http::RequestHeaderMap&, const Http::ResponseHeaderMap&,
                                  const Http::ResponseTrailerMap&, const StreamInfo::StreamInfo&,
                                  absl::string_view, AccessLog::AccessLogType) const {
  return num_;
}

absl::optional<std::string> LocalReplyBodyFormatter::format(const Http::RequestHeaderMap&,
                                                            const Http::ResponseHeaderMap&,
                                                            const Http::ResponseTrailerMap&,
                                                            const StreamInfo::StreamInfo&,
                                                            absl::string_view local_reply_body,
                                                            AccessLog::AccessLogType) const {
  return std::string(local_reply_body);
}

ProtobufWkt::Value LocalReplyBodyFormatter::formatValue(const Http::RequestHeaderMap&,
                                                        const Http::ResponseHeaderMap&,
                                                        const Http::ResponseTrailerMap&,
                                                        const StreamInfo::StreamInfo&,
                                                        absl::string_view local_reply_body,
                                                        AccessLog::AccessLogType) const {
  return ValueUtil::stringValue(std::string(local_reply_body));
}

absl::optional<std::string>
AccessLogTypeFormatter::format(const Http::RequestHeaderMap&, const Http::ResponseHeaderMap&,
                               const Http::ResponseTrailerMap&, const StreamInfo::StreamInfo&,
                               absl::string_view, AccessLog::AccessLogType access_log_type) const {
  return AccessLogType_Name(access_log_type);
}

ProtobufWkt::Value
AccessLogTypeFormatter::formatValue(const Http::RequestHeaderMap&, const Http::ResponseHeaderMap&,
                                    const Http::ResponseTrailerMap&, const StreamInfo::StreamInfo&,
                                    absl::string_view,
                                    AccessLog::AccessLogType access_log_type) const {
  return ValueUtil::stringValue(AccessLogType_Name(access_log_type));
}

HeaderFormatter::HeaderFormatter(const std::string& main_header,
                                 const std::string& alternative_header,
                                 absl::optional<size_t> max_length)
    : main_header_(main_header), alternative_header_(alternative_header), max_length_(max_length) {}

const Http::HeaderEntry* HeaderFormatter::findHeader(const Http::HeaderMap& headers) const {
  const auto header = headers.get(main_header_);

  if (header.empty() && !alternative_header_.get().empty()) {
    const auto alternate_header = headers.get(alternative_header_);
    // TODO(https://github.com/envoyproxy/envoy/issues/13454): Potentially log all header values.
    return alternate_header.empty() ? nullptr : alternate_header[0];
  }

  return header.empty() ? nullptr : header[0];
}

absl::optional<std::string> HeaderFormatter::format(const Http::HeaderMap& headers) const {
  const Http::HeaderEntry* header = findHeader(headers);
  if (!header) {
    return absl::nullopt;
  }

  std::string val = std::string(header->value().getStringView());
  truncate(val, max_length_);
  return val;
}

ProtobufWkt::Value HeaderFormatter::formatValue(const Http::HeaderMap& headers) const {
  const Http::HeaderEntry* header = findHeader(headers);
  if (!header) {
    return unspecifiedValue();
  }

  std::string val = std::string(header->value().getStringView());
  truncate(val, max_length_);
  return ValueUtil::stringValue(val);
}

ResponseHeaderFormatter::ResponseHeaderFormatter(const std::string& main_header,
                                                 const std::string& alternative_header,
                                                 absl::optional<size_t> max_length)
    : HeaderFormatter(main_header, alternative_header, max_length) {}

absl::optional<std::string>
ResponseHeaderFormatter::format(const Http::RequestHeaderMap&,
                                const Http::ResponseHeaderMap& response_headers,
                                const Http::ResponseTrailerMap&, const StreamInfo::StreamInfo&,
                                absl::string_view, AccessLog::AccessLogType) const {
  return HeaderFormatter::format(response_headers);
}

ProtobufWkt::Value
ResponseHeaderFormatter::formatValue(const Http::RequestHeaderMap&,
                                     const Http::ResponseHeaderMap& response_headers,
                                     const Http::ResponseTrailerMap&, const StreamInfo::StreamInfo&,
                                     absl::string_view, AccessLog::AccessLogType) const {
  return HeaderFormatter::formatValue(response_headers);
}

RequestHeaderFormatter::RequestHeaderFormatter(const std::string& main_header,
                                               const std::string& alternative_header,
                                               absl::optional<size_t> max_length)
    : HeaderFormatter(main_header, alternative_header, max_length) {}

absl::optional<std::string>
RequestHeaderFormatter::format(const Http::RequestHeaderMap& request_headers,
                               const Http::ResponseHeaderMap&, const Http::ResponseTrailerMap&,
                               const StreamInfo::StreamInfo&, absl::string_view,
                               AccessLog::AccessLogType) const {
  return HeaderFormatter::format(request_headers);
}

ProtobufWkt::Value
RequestHeaderFormatter::formatValue(const Http::RequestHeaderMap& request_headers,
                                    const Http::ResponseHeaderMap&, const Http::ResponseTrailerMap&,
                                    const StreamInfo::StreamInfo&, absl::string_view,
                                    AccessLog::AccessLogType) const {
  return HeaderFormatter::formatValue(request_headers);
}

ResponseTrailerFormatter::ResponseTrailerFormatter(const std::string& main_header,
                                                   const std::string& alternative_header,
                                                   absl::optional<size_t> max_length)
    : HeaderFormatter(main_header, alternative_header, max_length) {}

absl::optional<std::string>
ResponseTrailerFormatter::format(const Http::RequestHeaderMap&, const Http::ResponseHeaderMap&,
                                 const Http::ResponseTrailerMap& response_trailers,
                                 const StreamInfo::StreamInfo&, absl::string_view,
                                 AccessLog::AccessLogType) const {
  return HeaderFormatter::format(response_trailers);
}

ProtobufWkt::Value
ResponseTrailerFormatter::formatValue(const Http::RequestHeaderMap&, const Http::ResponseHeaderMap&,
                                      const Http::ResponseTrailerMap& response_trailers,
                                      const StreamInfo::StreamInfo&, absl::string_view,
                                      AccessLog::AccessLogType) const {
  return HeaderFormatter::formatValue(response_trailers);
}

HeadersByteSizeFormatter::HeadersByteSizeFormatter(const HeaderType header_type)
    : header_type_(header_type) {}

uint64_t HeadersByteSizeFormatter::extractHeadersByteSize(
    const Http::RequestHeaderMap& request_headers, const Http::ResponseHeaderMap& response_headers,
    const Http::ResponseTrailerMap& response_trailers) const {
  switch (header_type_) {
  case HeaderType::RequestHeaders:
    return request_headers.byteSize();
  case HeaderType::ResponseHeaders:
    return response_headers.byteSize();
  case HeaderType::ResponseTrailers:
    return response_trailers.byteSize();
  }
  PANIC_DUE_TO_CORRUPT_ENUM;
}

absl::optional<std::string> HeadersByteSizeFormatter::format(
    const Http::RequestHeaderMap& request_headers, const Http::ResponseHeaderMap& response_headers,
    const Http::ResponseTrailerMap& response_trailers, const StreamInfo::StreamInfo&,
    absl::string_view, AccessLog::AccessLogType) const {
  return absl::StrCat(extractHeadersByteSize(request_headers, response_headers, response_trailers));
}

ProtobufWkt::Value HeadersByteSizeFormatter::formatValue(
    const Http::RequestHeaderMap& request_headers, const Http::ResponseHeaderMap& response_headers,
    const Http::ResponseTrailerMap& response_trailers, const StreamInfo::StreamInfo&,
    absl::string_view, AccessLog::AccessLogType) const {
  return ValueUtil::numberValue(
      extractHeadersByteSize(request_headers, response_headers, response_trailers));
}

GrpcStatusFormatter::Format GrpcStatusFormatter::parseFormat(absl::string_view format) {
  if (format.empty() || format == "CAMEL_STRING") {
    return GrpcStatusFormatter::CamelString;
  }

  if (format == "SNAKE_STRING") {
    return GrpcStatusFormatter::SnakeString;
  }
  if (format == "NUMBER") {
    return GrpcStatusFormatter::Number;
  }

  throw EnvoyException("GrpcStatusFormatter only supports CAMEL_STRING, SNAKE_STRING or NUMBER.");
}

GrpcStatusFormatter::GrpcStatusFormatter(const std::string& main_header,
                                         const std::string& alternative_header,
                                         absl::optional<size_t> max_length, Format format)
    : HeaderFormatter(main_header, alternative_header, max_length), format_(format) {}

absl::optional<std::string> GrpcStatusFormatter::format(
    const Http::RequestHeaderMap& request_headers, const Http::ResponseHeaderMap& response_headers,
    const Http::ResponseTrailerMap& response_trailers, const StreamInfo::StreamInfo& info,
    absl::string_view, AccessLog::AccessLogType) const {
  if (Runtime::runtimeFeatureEnabled(
          "envoy.reloadable_features.validate_grpc_header_before_log_grpc_status")) {
    if (!Grpc::Common::isGrpcRequestHeaders(request_headers)) {
      return absl::nullopt;
    }
  }
  const auto grpc_status =
      Grpc::Common::getGrpcStatus(response_trailers, response_headers, info, true);
  if (!grpc_status.has_value()) {
    return absl::nullopt;
  }
  switch (format_) {
  case CamelString: {
    const auto grpc_status_message = Grpc::Utility::grpcStatusToString(grpc_status.value());
    if (grpc_status_message == EMPTY_STRING || grpc_status_message == "InvalidCode") {
      return std::to_string(grpc_status.value());
    }
    return grpc_status_message;
  }
  case SnakeString: {
    const auto grpc_status_message =
        absl::StatusCodeToString(static_cast<absl::StatusCode>(grpc_status.value()));
    if (grpc_status_message == EMPTY_STRING) {
      return std::to_string(grpc_status.value());
    }
    return grpc_status_message;
  }
  case Number: {
    return std::to_string(grpc_status.value());
  }
  }
  PANIC_DUE_TO_CORRUPT_ENUM;
}

ProtobufWkt::Value GrpcStatusFormatter::formatValue(
    const Http::RequestHeaderMap& request_headers, const Http::ResponseHeaderMap& response_headers,
    const Http::ResponseTrailerMap& response_trailers, const StreamInfo::StreamInfo& info,
    absl::string_view, AccessLog::AccessLogType) const {
  if (Runtime::runtimeFeatureEnabled(
          "envoy.reloadable_features.validate_grpc_header_before_log_grpc_status")) {
    if (!Grpc::Common::isGrpcRequestHeaders(request_headers)) {
      return unspecifiedValue();
    }
  }
  const auto grpc_status =
      Grpc::Common::getGrpcStatus(response_trailers, response_headers, info, true);
  if (!grpc_status.has_value()) {
    return unspecifiedValue();
  }

  switch (format_) {
  case CamelString: {
    const auto grpc_status_message = Grpc::Utility::grpcStatusToString(grpc_status.value());
    if (grpc_status_message == EMPTY_STRING || grpc_status_message == "InvalidCode") {
      return ValueUtil::stringValue(std::to_string(grpc_status.value()));
    }
    return ValueUtil::stringValue(grpc_status_message);
  }
  case SnakeString: {
    const auto grpc_status_message =
        absl::StatusCodeToString(static_cast<absl::StatusCode>(grpc_status.value()));
    if (grpc_status_message == EMPTY_STRING) {
      return ValueUtil::stringValue(std::to_string(grpc_status.value()));
    }
    return ValueUtil::stringValue(grpc_status_message);
  }
  case Number: {
    return ValueUtil::numberValue(grpc_status.value());
  }
  }
  PANIC_DUE_TO_CORRUPT_ENUM;
}

void CommandSyntaxChecker::verifySyntax(CommandSyntaxFlags flags, const std::string& command,
                                        const std::string& subcommand,
                                        const absl::optional<size_t>& length) {
  if ((flags == COMMAND_ONLY) && ((subcommand.length() != 0) || length.has_value())) {
    throw EnvoyException(fmt::format("{} does not take any parameters or length", command));
  }

  if ((flags & PARAMS_REQUIRED).any() && (subcommand.length() == 0)) {
    throw EnvoyException(fmt::format("{} requires parameters", command));
  }

  if ((flags & LENGTH_ALLOWED).none() && length.has_value()) {
    throw EnvoyException(fmt::format("{} does not allow length to be specified.", command));
  }
}

StreamInfoRequestHeaderFormatter::StreamInfoRequestHeaderFormatter(
    const std::string& main_header, const std::string& alternative_header,
    absl::optional<size_t> max_length)
    : HeaderFormatter(main_header, alternative_header, max_length) {}

absl::optional<std::string> StreamInfoRequestHeaderFormatter::format(
    const Http::RequestHeaderMap&, const Http::ResponseHeaderMap&, const Http::ResponseTrailerMap&,
    const StreamInfo::StreamInfo& stream_info, absl::string_view, AccessLog::AccessLogType) const {
  return HeaderFormatter::format(*stream_info.getRequestHeaders());
}

ProtobufWkt::Value StreamInfoRequestHeaderFormatter::formatValue(
    const Http::RequestHeaderMap&, const Http::ResponseHeaderMap&, const Http::ResponseTrailerMap&,
    const StreamInfo::StreamInfo& stream_info, absl::string_view, AccessLog::AccessLogType) const {
  return HeaderFormatter::formatValue(*stream_info.getRequestHeaders());
}

} // namespace Formatter
} // namespace Envoy
