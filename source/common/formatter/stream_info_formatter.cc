#include "source/common/formatter/stream_info_formatter.h"

#include "source/common/common/random_generator.h"
#include "source/common/config/metadata.h"
#include "source/common/http/utility.h"
#include "source/common/runtime/runtime_features.h"
#include "source/common/stream_info/utility.h"

#include "absl/strings/str_format.h"
#include "absl/strings/str_replace.h"
#include "re2/re2.h"

namespace Envoy {
namespace Formatter {

namespace {

static const std::string DefaultUnspecifiedValueString = "-";

const re2::RE2& getSystemTimeFormatNewlinePattern() {
  CONSTRUCT_ON_FIRST_USE(re2::RE2, "%[-_0^#]*[1-9]*(E|O)?n");
}

Network::Address::InstanceConstSharedPtr
getUpstreamRemoteAddress(const StreamInfo::StreamInfo& stream_info) {
  auto opt_ref = stream_info.upstreamInfo();
  if (!opt_ref.has_value()) {
    return nullptr;
  }

  // TODO(wbpcode): remove this after the flag is removed.
  const bool use_upstream_remote_address = Runtime::runtimeFeatureEnabled(
      "envoy.reloadable_features.upstream_remote_address_use_connection");
  if (!use_upstream_remote_address) {
    if (auto host = opt_ref->upstreamHost(); host != nullptr) {
      return host->address();
    }
    return nullptr;
  }

  if (auto addr = opt_ref->upstreamRemoteAddress(); addr != nullptr) {
    return addr;
  }
  return nullptr;
}

} // namespace

MetadataFormatter::MetadataFormatter(absl::string_view filter_namespace,
                                     const std::vector<absl::string_view>& path,
                                     absl::optional<size_t> max_length,
                                     MetadataFormatter::GetMetadataFunction get_func)
    : filter_namespace_(filter_namespace), path_(path.begin(), path.end()), max_length_(max_length),
      get_func_(get_func) {}

Value MetadataFormatter::format(const StreamInfo::StreamInfo& stream_info) const {
  auto metadata = get_func_(stream_info);
  if (path_.empty()) {
    const auto filter_it = metadata->filter_metadata().find(filter_namespace_);
    if (filter_it == metadata->filter_metadata().end()) {
      return absl::monostate{};
    }
    ProtobufWkt::Value output;
    output.mutable_struct_value()->CopyFrom(filter_it->second);
    return output;
  }

  const ProtobufWkt::Value& val =
      Config::Metadata::metadataValue(metadata, filter_namespace_, path_);

  switch (val.kind_case()) {
  case ProtobufWkt::Value::KIND_NOT_SET:
  case ProtobufWkt::Value::kNullValue:
    return absl::monostate{};
  case ProtobufWkt::Value::kStringValue:
    return SubstitutionFormatUtils::truncateStringView(val.string_value(), max_length_);
  case ProtobufWkt::Value::kBoolValue:
    return val.bool_value();
  case ProtobufWkt::Value::kNumberValue:
    return val.number_value();
  case ProtobufWkt::Value::kStructValue:
  case ProtobufWkt::Value::kListValue:
    return val;
  }
}

// TODO(glicht): Consider adding support for route/listener/cluster metadata as suggested by
// @htuch. See: https://github.com/envoyproxy/envoy/issues/3006
DynamicMetadataFormatter::DynamicMetadataFormatter(absl::string_view filter_namespace,
                                                   const std::vector<absl::string_view>& path,
                                                   absl::optional<size_t> max_length)
    : MetadataFormatter(filter_namespace, path, max_length,
                        [](const StreamInfo::StreamInfo& stream_info) {
                          return &stream_info.dynamicMetadata();
                        }) {}

ClusterMetadataFormatter::ClusterMetadataFormatter(absl::string_view filter_namespace,
                                                   const std::vector<absl::string_view>& path,
                                                   absl::optional<size_t> max_length)
    : MetadataFormatter(filter_namespace, path, max_length,
                        [](const StreamInfo::StreamInfo& stream_info)
                            -> const envoy::config::core::v3::Metadata* {
                          auto cluster_info = stream_info.upstreamClusterInfo();
                          if (!cluster_info.has_value() || cluster_info.value() == nullptr) {
                            return nullptr;
                          }
                          return &cluster_info.value()->metadata();
                        }) {}

UpstreamHostMetadataFormatter::UpstreamHostMetadataFormatter(
    absl::string_view filter_namespace, const std::vector<absl::string_view>& path,
    absl::optional<size_t> max_length)
    : MetadataFormatter(filter_namespace, path, max_length,
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

std::unique_ptr<FilterStateFormatter>
FilterStateFormatter::create(absl::string_view format, absl::optional<size_t> max_length,
                             bool is_upstream) {
  absl::string_view key, serialize_type, field_name;
  static constexpr absl::string_view PLAIN_SERIALIZATION{"PLAIN"};
  static constexpr absl::string_view TYPED_SERIALIZATION{"TYPED"};
  static constexpr absl::string_view FIELD_SERIALIZATION{"FIELD"};

  SubstitutionFormatUtils::parseSubcommand(format, ':', key, serialize_type, field_name);
  if (key.empty()) {
    throw EnvoyException("Invalid filter state configuration, key cannot be empty.");
  }

  if (serialize_type.empty()) {
    serialize_type = TYPED_SERIALIZATION;
  }
  if (serialize_type != PLAIN_SERIALIZATION && serialize_type != TYPED_SERIALIZATION &&
      serialize_type != FIELD_SERIALIZATION) {
    throw EnvoyException("Invalid filter state serialize type, only "
                         "support PLAIN/TYPED/FIELD.");
  }
  if ((serialize_type == FIELD_SERIALIZATION) ^ !field_name.empty()) {
    throw EnvoyException("Invalid filter state serialize type, FIELD "
                         "should be used with the field name.");
  }

  const bool serialize_as_string = serialize_type == PLAIN_SERIALIZATION;

  return std::make_unique<FilterStateFormatter>(key, max_length, serialize_as_string, is_upstream,
                                                field_name);
}

FilterStateFormatter::FilterStateFormatter(absl::string_view key, absl::optional<size_t> max_length,
                                           bool serialize_as_string, bool is_upstream,
                                           absl::string_view field_name)
    : key_(key), max_length_(max_length), is_upstream_(is_upstream) {
  if (!field_name.empty()) {
    format_ = FilterStateFormat::Field;
    field_name_ = std::string(field_name);
    factory_ = Registry::FactoryRegistry<StreamInfo::FilterState::ObjectFactory>::getFactory(key);
  } else if (serialize_as_string) {
    format_ = FilterStateFormat::String;
  } else {
    format_ = FilterStateFormat::Proto;
  }
}

const Envoy::StreamInfo::FilterState::Object*
FilterStateFormatter::filterState(const StreamInfo::StreamInfo& stream_info) const {
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

struct FilterStateFieldVisitor {
  Value operator()(int64_t val) { return val; }
  Value operator()(absl::string_view val) {
    return SubstitutionFormatUtils::truncateStringView(val, max_length_);
  }
  Value operator()(absl::monostate) { return {}; }

  absl::optional<size_t> max_length_;
};

Value FilterStateFormatter::format(const StreamInfo::StreamInfo& stream_info) const {
  const Envoy::StreamInfo::FilterState::Object* state = filterState(stream_info);
  if (!state) {
    return absl::monostate{};
  }

  switch (format_) {
  case FilterStateFormat::String: {
    absl::optional<std::string> plain_value = state->serializeAsString();
    if (plain_value.has_value()) {
      SubstitutionFormatUtils::truncate(plain_value.value(), max_length_);
      return std::move(plain_value).value();
    }
    return absl::monostate{};
  }
  case FilterStateFormat::Proto: {
    ProtobufTypes::MessagePtr proto = state->serializeAsProto();
    if (proto == nullptr) {
      return absl::monostate{};
    }
    ProtobufWkt::Value val;
    if (MessageUtil::jsonConvertValue(*proto, val)) {
      return val;
    }
    return absl::monostate{};
  }
  case FilterStateFormat::Field: {
    if (!factory_) {
      return absl::monostate{};
    }
    const auto reflection = factory_->reflect(state);
    if (!reflection) {
      return absl::monostate{};
    }
    return absl::visit(FilterStateFieldVisitor{max_length_}, reflection->getField(field_name_));
  }
  default:
    return absl::monostate{};
  }
}

const absl::flat_hash_map<absl::string_view, CommonDurationFormatter::TimePointGetter>
    CommonDurationFormatter::KnownTimePointGetters{
        {FirstDownstreamRxByteReceived,
         [](const StreamInfo::StreamInfo& stream_info) -> absl::optional<MonotonicTime> {
           return stream_info.startTimeMonotonic();
         }},
        {LastDownstreamRxByteReceived,
         [](const StreamInfo::StreamInfo& stream_info) -> absl::optional<MonotonicTime> {
           const auto downstream_timing = stream_info.downstreamTiming();
           if (downstream_timing.has_value()) {
             return downstream_timing->lastDownstreamRxByteReceived();
           }
           return {};
         }},
        {FirstUpstreamTxByteSent,
         [](const StreamInfo::StreamInfo& stream_info) -> absl::optional<MonotonicTime> {
           const auto upstream_info = stream_info.upstreamInfo();
           if (upstream_info.has_value()) {
             return upstream_info->upstreamTiming().first_upstream_tx_byte_sent_;
           }
           return {};
         }},
        {LastUpstreamTxByteSent,
         [](const StreamInfo::StreamInfo& stream_info) -> absl::optional<MonotonicTime> {
           const auto upstream_info = stream_info.upstreamInfo();
           if (upstream_info.has_value()) {
             return upstream_info->upstreamTiming().last_upstream_tx_byte_sent_;
           }
           return {};
         }},
        {FirstUpstreamRxByteReceived,
         [](const StreamInfo::StreamInfo& stream_info) -> absl::optional<MonotonicTime> {
           const auto upstream_info = stream_info.upstreamInfo();
           if (upstream_info.has_value()) {
             return upstream_info->upstreamTiming().first_upstream_rx_byte_received_;
           }
           return {};
         }},
        {LastUpstreamRxByteReceived,
         [](const StreamInfo::StreamInfo& stream_info) -> absl::optional<MonotonicTime> {
           const auto upstream_info = stream_info.upstreamInfo();
           if (upstream_info.has_value()) {
             return upstream_info->upstreamTiming().last_upstream_rx_byte_received_;
           }
           return {};
         }},
        {FirstDownstreamTxByteSent,
         [](const StreamInfo::StreamInfo& stream_info) -> absl::optional<MonotonicTime> {
           const auto downstream_timing = stream_info.downstreamTiming();
           if (downstream_timing.has_value()) {
             return downstream_timing->firstDownstreamTxByteSent();
           }
           return {};
         }},
        {LastDownstreamTxByteSent,
         [](const StreamInfo::StreamInfo& stream_info) -> absl::optional<MonotonicTime> {
           const auto downstream_timing = stream_info.downstreamTiming();
           if (downstream_timing.has_value()) {
             return downstream_timing->lastDownstreamTxByteSent();
           }
           return {};
         }},
    };

CommonDurationFormatter::TimePointGetter
CommonDurationFormatter::getTimePointGetterByName(absl::string_view name) {
  auto it = KnownTimePointGetters.find(name);
  if (it != KnownTimePointGetters.end()) {
    return it->second;
  }

  return [key = std::string(name)](const StreamInfo::StreamInfo& info) {
    const auto downstream_timing = info.downstreamTiming();
    if (downstream_timing.has_value()) {
      return downstream_timing->getValue(key);
    }
    return absl::optional<MonotonicTime>{};
  };
}

std::unique_ptr<CommonDurationFormatter>
CommonDurationFormatter::create(absl::string_view sub_command) {
  // Split the sub_command by ':'.
  absl::InlinedVector<absl::string_view, 3> parsed_sub_commands = absl::StrSplit(sub_command, ':');

  if (parsed_sub_commands.size() < 2 || parsed_sub_commands.size() > 3) {
    throw EnvoyException(fmt::format("Invalid common duration configuration: {}.", sub_command));
  }

  absl::string_view start = parsed_sub_commands[0];
  absl::string_view end = parsed_sub_commands[1];

  // Milliseconds is the default precision.
  DurationPrecision precision = DurationPrecision::Milliseconds;

  if (parsed_sub_commands.size() == 3) {
    absl::string_view precision_str = parsed_sub_commands[2];
    if (precision_str == MillisecondsPrecision) {
      precision = DurationPrecision::Milliseconds;
    } else if (precision_str == MicrosecondsPrecision) {
      precision = DurationPrecision::Microseconds;
    } else if (precision_str == NanosecondsPrecision) {
      precision = DurationPrecision::Nanoseconds;
    } else {
      throw EnvoyException(fmt::format("Invalid common duration precision: {}.", precision_str));
    }
  }

  TimePointGetter start_getter = getTimePointGetterByName(start);
  TimePointGetter end_getter = getTimePointGetterByName(end);

  return std::make_unique<CommonDurationFormatter>(std::move(start_getter), std::move(end_getter),
                                                   precision);
}

absl::optional<uint64_t>
CommonDurationFormatter::getDurationCount(const StreamInfo::StreamInfo& info) const {
  auto time_point_beg = time_point_beg_(info);
  auto time_point_end = time_point_end_(info);

  if (!time_point_beg.has_value() || !time_point_end.has_value()) {
    return absl::nullopt;
  }

  if (time_point_end.value() < time_point_beg.value()) {
    return absl::nullopt;
  }

  auto duration = time_point_end.value() - time_point_beg.value();

  switch (duration_precision_) {
  case DurationPrecision::Milliseconds:
    return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
  case DurationPrecision::Microseconds:
    return std::chrono::duration_cast<std::chrono::microseconds>(duration).count();
  case DurationPrecision::Nanoseconds:
    return std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();
  }
  PANIC("Invalid duration precision");
}

Value CommonDurationFormatter::format(const StreamInfo::StreamInfo& info) const {
  const absl::optional<int64_t> duration = getDurationCount(info);
  if (!duration.has_value()) {
    return absl::monostate{};
  }
  return duration.value();
}

// A SystemTime formatter that extracts the startTime from StreamInfo. Must be provided
// an access log command that starts with `START_TIME`.
StartTimeFormatter::StartTimeFormatter(absl::string_view format)
    : SystemTimeFormatter(
          format, std::make_unique<SystemTimeFormatter::TimeFieldExtractor>(
                      [](const StreamInfo::StreamInfo& stream_info) -> absl::optional<SystemTime> {
                        return stream_info.startTime();
                      })) {}

DownstreamPeerCertVStartFormatter::DownstreamPeerCertVStartFormatter(absl::string_view format)
    : SystemTimeFormatter(
          format, std::make_unique<SystemTimeFormatter::TimeFieldExtractor>(
                      [](const StreamInfo::StreamInfo& stream_info) -> absl::optional<SystemTime> {
                        const auto connection_info =
                            stream_info.downstreamAddressProvider().sslConnection();
                        return connection_info != nullptr
                                   ? connection_info->validFromPeerCertificate()
                                   : absl::optional<SystemTime>();
                      })) {}
DownstreamPeerCertVEndFormatter::DownstreamPeerCertVEndFormatter(absl::string_view format)
    : SystemTimeFormatter(
          format, std::make_unique<SystemTimeFormatter::TimeFieldExtractor>(
                      [](const StreamInfo::StreamInfo& stream_info) -> absl::optional<SystemTime> {
                        const auto connection_info =
                            stream_info.downstreamAddressProvider().sslConnection();
                        return connection_info != nullptr
                                   ? connection_info->expirationPeerCertificate()
                                   : absl::optional<SystemTime>();
                      })) {}
UpstreamPeerCertVStartFormatter::UpstreamPeerCertVStartFormatter(absl::string_view format)
    : SystemTimeFormatter(
          format, std::make_unique<SystemTimeFormatter::TimeFieldExtractor>(
                      [](const StreamInfo::StreamInfo& stream_info) -> absl::optional<SystemTime> {
                        return stream_info.upstreamInfo() &&
                                       stream_info.upstreamInfo()->upstreamSslConnection() !=
                                           nullptr
                                   ? stream_info.upstreamInfo()
                                         ->upstreamSslConnection()
                                         ->validFromPeerCertificate()
                                   : absl::optional<SystemTime>();
                      })) {}
UpstreamPeerCertVEndFormatter::UpstreamPeerCertVEndFormatter(absl::string_view format)
    : SystemTimeFormatter(
          format, std::make_unique<SystemTimeFormatter::TimeFieldExtractor>(
                      [](const StreamInfo::StreamInfo& stream_info) -> absl::optional<SystemTime> {
                        return stream_info.upstreamInfo() &&
                                       stream_info.upstreamInfo()->upstreamSslConnection() !=
                                           nullptr
                                   ? stream_info.upstreamInfo()
                                         ->upstreamSslConnection()
                                         ->expirationPeerCertificate()
                                   : absl::optional<SystemTime>();
                      })) {}

SystemTimeFormatter::SystemTimeFormatter(absl::string_view format, TimeFieldExtractorPtr f,
                                         bool local_time)
    : date_formatter_(format, local_time), time_field_extractor_(std::move(f)),
      local_time_(local_time) {
  // Validate the input specifier here. The formatted string may be destined for a header, and
  // should not contain invalid characters {NUL, LR, CF}.
  if (re2::RE2::PartialMatch(format, getSystemTimeFormatNewlinePattern())) {
    throw EnvoyException("Invalid header configuration. Format string contains newline.");
  }
}

Value SystemTimeFormatter::format(const StreamInfo::StreamInfo& stream_info) const {
  const auto time_field = (*time_field_extractor_)(stream_info);
  if (!time_field.has_value()) {
    return absl::monostate{};
  }
  if (date_formatter_.formatString().empty()) {
    return AccessLogDateTimeFormatter::fromTime(time_field.value(), local_time_);
  }
  return date_formatter_.fromTime(time_field.value());
}

EnvironmentFormatter::EnvironmentFormatter(absl::string_view key,
                                           absl::optional<size_t> max_length) {
  ASSERT(!key.empty());

  const std::string key_str = std::string(key);
  const char* env_value = std::getenv(key_str.c_str());
  if (env_value != nullptr) {
    std::string env_string = env_value;
    SubstitutionFormatUtils::truncate(env_string, max_length);
    str_ = env_string;
    return;
  }
  str_ = DefaultUnspecifiedValueString;
}

Value EnvironmentFormatter::format(const StreamInfo::StreamInfo&) const {
  return absl::string_view{str_};
}

// StreamInfo std::string formatter provider.
class StreamInfoValueFormatterProvider : public StreamInfoFormatterProvider {
public:
  using FieldExtractor = std::function<Value(const StreamInfo::StreamInfo&)>;

  StreamInfoValueFormatterProvider(FieldExtractor f) : field_extractor_(f) {}

  // StreamInfoFormatterProvider
  Value format(const StreamInfo::StreamInfo& stream_info) const override {
    return field_extractor_(stream_info);
  }

private:
  FieldExtractor field_extractor_;
};

// StreamInfo std::chrono_nanoseconds field extractor.
class StreamInfoDurationFormatterProvider : public StreamInfoFormatterProvider {
public:
  using FieldExtractor =
      std::function<absl::optional<std::chrono::nanoseconds>(const StreamInfo::StreamInfo&)>;

  StreamInfoDurationFormatterProvider(FieldExtractor f) : field_extractor_(f) {}

  // StreamInfoFormatterProvider
  Value format(const StreamInfo::StreamInfo& stream_info) const override {
    const absl::optional<int64_t> millis = extractMillis(stream_info);
    if (!millis.has_value()) {
      return absl::monostate{};
    }

    return millis.value();
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

// StreamInfo Network::Address::InstanceConstSharedPtr field extractor.
class StreamInfoAddressFormatterProvider : public StreamInfoFormatterProvider {
public:
  using FieldExtractor =
      std::function<Network::Address::InstanceConstSharedPtr(const StreamInfo::StreamInfo&)>;

  static std::unique_ptr<StreamInfoAddressFormatterProvider> withPort(FieldExtractor f) {
    return std::make_unique<StreamInfoAddressFormatterProvider>(
        f, StreamInfoAddressFieldExtractionType::WithPort);
  }

  static std::unique_ptr<StreamInfoAddressFormatterProvider> withoutPort(FieldExtractor f) {
    return std::make_unique<StreamInfoAddressFormatterProvider>(
        f, StreamInfoAddressFieldExtractionType::WithoutPort);
  }

  static std::unique_ptr<StreamInfoAddressFormatterProvider> justPort(FieldExtractor f) {
    return std::make_unique<StreamInfoAddressFormatterProvider>(
        f, StreamInfoAddressFieldExtractionType::JustPort);
  }

  StreamInfoAddressFormatterProvider(FieldExtractor f,
                                     StreamInfoAddressFieldExtractionType extraction_type)
      : field_extractor_(f), extraction_type_(extraction_type) {}

  // StreamInfoFormatterProvider
  Value format(const StreamInfo::StreamInfo& stream_info) const override {
    Network::Address::InstanceConstSharedPtr address = field_extractor_(stream_info);
    if (!address) {
      return absl::monostate{};
    }

    switch (extraction_type_) {
    case StreamInfoAddressFieldExtractionType::WithoutPort:
      return absl::string_view{StreamInfo::Utility::formatDownstreamAddressNoPort(*address)};
    case StreamInfoAddressFieldExtractionType::JustPort:
      return StreamInfo::Utility::formatDownstreamAddressJustPort(*address);
    case StreamInfoAddressFieldExtractionType::WithPort:
    default:
      return address->asStringView();
    }
  }

private:
  FieldExtractor field_extractor_;
  const StreamInfoAddressFieldExtractionType extraction_type_;
};

// Ssl::ConnectionInfo std::string field extractor.
class StreamInfoSslConnectionInfoFormatterProvider : public StreamInfoFormatterProvider {
public:
  using FieldExtractor =
      std::function<absl::optional<std::string>(const Ssl::ConnectionInfo& connection_info)>;

  StreamInfoSslConnectionInfoFormatterProvider(FieldExtractor f) : field_extractor_(f) {}

  Value format(const StreamInfo::StreamInfo& stream_info) const override {
    if (stream_info.downstreamAddressProvider().sslConnection() == nullptr) {
      return absl::monostate{};
    }

    const auto value = field_extractor_(*stream_info.downstreamAddressProvider().sslConnection());
    if (value && value->empty()) {
      return absl::monostate{};
    }

    return std::move(value).value();
  }

private:
  FieldExtractor field_extractor_;
};

class StreamInfoUpstreamSslConnectionInfoFormatterProvider : public StreamInfoFormatterProvider {
public:
  using FieldExtractor =
      std::function<absl::optional<std::string>(const Ssl::ConnectionInfo& connection_info)>;

  StreamInfoUpstreamSslConnectionInfoFormatterProvider(FieldExtractor f) : field_extractor_(f) {}

  Value format(const StreamInfo::StreamInfo& stream_info) const override {
    if (!stream_info.upstreamInfo() ||
        stream_info.upstreamInfo()->upstreamSslConnection() == nullptr) {
      return absl::monostate{};
    }

    const auto value = field_extractor_(*(stream_info.upstreamInfo()->upstreamSslConnection()));
    if (value && value->empty()) {
      return absl::monostate{};
    }

    return std::move(value).value();
  }

private:
  FieldExtractor field_extractor_;
};

using StreamInfoFormatterProviderLookupTable =
    absl::flat_hash_map<absl::string_view, std::pair<CommandSyntaxChecker::CommandSyntaxFlags,
                                                     StreamInfoFormatterProviderCreateFunc>>;

const StreamInfoFormatterProviderLookupTable& getKnownStreamInfoFormatterProviders() {
  CONSTRUCT_ON_FIRST_USE(
      StreamInfoFormatterProviderLookupTable,
      {
          {"REQUEST_DURATION",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoDurationFormatterProvider>(
                  [](const StreamInfo::StreamInfo& stream_info) {
                    StreamInfo::TimingUtility timing(stream_info);
                    return timing.lastDownstreamRxByteReceived();
                  });
            }}},
          {"REQUEST_TX_DURATION",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoDurationFormatterProvider>(
                  [](const StreamInfo::StreamInfo& stream_info) {
                    StreamInfo::TimingUtility timing(stream_info);
                    return timing.lastUpstreamTxByteSent();
                  });
            }}},
          {"RESPONSE_DURATION",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoDurationFormatterProvider>(
                  [](const StreamInfo::StreamInfo& stream_info) {
                    StreamInfo::TimingUtility timing(stream_info);
                    return timing.firstUpstreamRxByteReceived();
                  });
            }}},
          {"RESPONSE_TX_DURATION",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoDurationFormatterProvider>(
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
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoDurationFormatterProvider>(
                  [](const StreamInfo::StreamInfo& stream_info) {
                    StreamInfo::TimingUtility timing(stream_info);
                    return timing.downstreamHandshakeComplete();
                  });
            }}},
          {"ROUNDTRIP_DURATION",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoDurationFormatterProvider>(
                  [](const StreamInfo::StreamInfo& stream_info) {
                    StreamInfo::TimingUtility timing(stream_info);
                    return timing.lastDownstreamAckReceived();
                  });
            }}},
          {"BYTES_RECEIVED",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoValueFormatterProvider>(
                  [](const StreamInfo::StreamInfo& stream_info) {
                    return stream_info.bytesReceived();
                  });
            }}},
          {"BYTES_RETRANSMITTED",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoValueFormatterProvider>(
                  [](const StreamInfo::StreamInfo& stream_info) {
                    return stream_info.bytesRetransmitted();
                  });
            }}},
          {"PACKETS_RETRANSMITTED",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoValueFormatterProvider>(
                  [](const StreamInfo::StreamInfo& stream_info) {
                    return stream_info.packetsRetransmitted();
                  });
            }}},
          {"UPSTREAM_WIRE_BYTES_RECEIVED",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoValueFormatterProvider>(
                  [](const StreamInfo::StreamInfo& stream_info) {
                    const auto& bytes_meter = stream_info.getUpstreamBytesMeter();
                    return bytes_meter ? bytes_meter->wireBytesReceived() : 0;
                  });
            }}},
          {"UPSTREAM_HEADER_BYTES_RECEIVED",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoValueFormatterProvider>(
                  [](const StreamInfo::StreamInfo& stream_info) {
                    const auto& bytes_meter = stream_info.getUpstreamBytesMeter();
                    return bytes_meter ? bytes_meter->headerBytesReceived() : 0;
                  });
            }}},
          {"DOWNSTREAM_WIRE_BYTES_RECEIVED",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoValueFormatterProvider>(
                  [](const StreamInfo::StreamInfo& stream_info) {
                    const auto& bytes_meter = stream_info.getDownstreamBytesMeter();
                    return bytes_meter ? bytes_meter->wireBytesReceived() : 0;
                  });
            }}},
          {"DOWNSTREAM_HEADER_BYTES_RECEIVED",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoValueFormatterProvider>(
                  [](const StreamInfo::StreamInfo& stream_info) {
                    const auto& bytes_meter = stream_info.getDownstreamBytesMeter();
                    return bytes_meter ? bytes_meter->headerBytesReceived() : 0;
                  });
            }}},
          {"PROTOCOL",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoValueFormatterProvider>(
                  [](const StreamInfo::StreamInfo& stream_info) -> Value {
                    auto protocol =
                        SubstitutionFormatUtils::protocolToString(stream_info.protocol());
                    if (!protocol.has_value()) {
                      return absl::monostate{};
                    }
                    return absl::string_view{protocol.value().get()};
                  });
            }}},
          {"UPSTREAM_PROTOCOL",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoValueFormatterProvider>(
                  [](const StreamInfo::StreamInfo& stream_info) -> Value {
                    if (!stream_info.upstreamInfo()) {
                      return absl::monostate{};
                    }
                    auto protocol = SubstitutionFormatUtils::protocolToString(
                        stream_info.upstreamInfo()->upstreamProtocol());
                    if (!protocol.has_value()) {
                      return absl::monostate{};
                    }
                    return absl::string_view{protocol.value().get()};
                  });
            }}},
          {"RESPONSE_CODE",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoValueFormatterProvider>(
                  [](const StreamInfo::StreamInfo& stream_info) -> Value {
                    return static_cast<uint64_t>(stream_info.responseCode().value_or(0));
                  });
            }}},
          {"RESPONSE_CODE_DETAILS",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoValueFormatterProvider>(
                  [](const StreamInfo::StreamInfo& stream_info) -> Value {
                    const auto& details = stream_info.responseCodeDetails();
                    if (!details.has_value()) {
                      return absl::monostate{};
                    }
                    return absl::string_view{details.value()};
                  });
            }}},
          {"CONNECTION_TERMINATION_DETAILS",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoValueFormatterProvider>(
                  [](const StreamInfo::StreamInfo& stream_info) -> Value {
                    const auto& details = stream_info.connectionTerminationDetails();
                    if (!details.has_value()) {
                      return absl::monostate{};
                    }
                    return absl::string_view{details.value()};
                  });
            }}},
          {"BYTES_SENT",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoValueFormatterProvider>(
                  [](const StreamInfo::StreamInfo& stream_info) {
                    return stream_info.bytesSent();
                  });
            }}},
          {"UPSTREAM_WIRE_BYTES_SENT",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoValueFormatterProvider>(
                  [](const StreamInfo::StreamInfo& stream_info) {
                    const auto& bytes_meter = stream_info.getUpstreamBytesMeter();
                    return bytes_meter ? bytes_meter->wireBytesSent() : 0;
                  });
            }}},
          {"UPSTREAM_HEADER_BYTES_SENT",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoValueFormatterProvider>(
                  [](const StreamInfo::StreamInfo& stream_info) {
                    const auto& bytes_meter = stream_info.getUpstreamBytesMeter();
                    return bytes_meter ? bytes_meter->headerBytesSent() : 0;
                  });
            }}},
          {"DOWNSTREAM_WIRE_BYTES_SENT",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoValueFormatterProvider>(
                  [](const StreamInfo::StreamInfo& stream_info) {
                    const auto& bytes_meter = stream_info.getDownstreamBytesMeter();
                    return bytes_meter ? bytes_meter->wireBytesSent() : 0;
                  });
            }}},
          {"DOWNSTREAM_HEADER_BYTES_SENT",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoValueFormatterProvider>(
                  [](const StreamInfo::StreamInfo& stream_info) {
                    const auto& bytes_meter = stream_info.getDownstreamBytesMeter();
                    return bytes_meter ? bytes_meter->headerBytesSent() : 0;
                  });
            }}},
          {"DURATION",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoDurationFormatterProvider>(
                  [](const StreamInfo::StreamInfo& stream_info) {
                    return stream_info.currentDuration();
                  });
            }}},
          {"COMMON_DURATION",
           {CommandSyntaxChecker::PARAMS_REQUIRED,
            [](absl::string_view sub_command, absl::optional<size_t>) {
              return CommonDurationFormatter::create(sub_command);
            }}},
          {"RESPONSE_FLAGS",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoValueFormatterProvider>(
                  [](const StreamInfo::StreamInfo& stream_info) {
                    return StreamInfo::ResponseFlagUtils::toShortString(stream_info);
                  });
            }}},
          {"RESPONSE_FLAGS_LONG",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoValueFormatterProvider>(
                  [](const StreamInfo::StreamInfo& stream_info) {
                    return StreamInfo::ResponseFlagUtils::toString(stream_info);
                  });
            }}},
          {"UPSTREAM_HOST_NAME",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoValueFormatterProvider>(
                  [](const StreamInfo::StreamInfo& stream_info) -> Value {
                    const auto opt_ref = stream_info.upstreamInfo();
                    if (!opt_ref.has_value()) {
                      return absl::monostate{};
                    }
                    const auto host = opt_ref->upstreamHost();
                    if (host == nullptr) {
                      return absl::monostate{};
                    }
                    absl::string_view host_name = host->hostname();
                    if (host_name.empty()) {
                      // If no hostname is available, the main address is used.
                      return host->address()->asStringView();
                    }
                    return host_name;
                  });
            }}},
          {"UPSTREAM_HOST",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return StreamInfoAddressFormatterProvider::withPort(
                  [](const StreamInfo::StreamInfo& stream_info)
                      -> Network::Address::InstanceConstSharedPtr {
                    const auto opt_ref = stream_info.upstreamInfo();
                    if (!opt_ref.has_value()) {
                      return nullptr;
                    }
                    const auto host = opt_ref->upstreamHost();
                    if (host == nullptr) {
                      return nullptr;
                    }
                    return host->address();
                  });
            }}},
          {"UPSTREAM_CONNECTION_ID",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoValueFormatterProvider>(
                  [](const StreamInfo::StreamInfo& stream_info) {
                    uint64_t upstream_connection_id = 0;
                    if (stream_info.upstreamInfo().has_value()) {
                      upstream_connection_id =
                          stream_info.upstreamInfo()->upstreamConnectionId().value_or(0);
                    }
                    return upstream_connection_id;
                  });
            }}},
          {"UPSTREAM_CLUSTER",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoValueFormatterProvider>(
                  [](const StreamInfo::StreamInfo& stream_info) -> Value {
                    absl::string_view upstream_cluster_name;
                    if (stream_info.upstreamClusterInfo().has_value() &&
                        stream_info.upstreamClusterInfo().value() != nullptr) {
                      upstream_cluster_name =
                          stream_info.upstreamClusterInfo().value()->observabilityName();
                    }
                    if (upstream_cluster_name.empty()) {
                      return absl::monostate{};
                    }
                    return upstream_cluster_name;
                  });
            }}},
          {"UPSTREAM_CLUSTER_RAW",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoValueFormatterProvider>(
                  [](const StreamInfo::StreamInfo& stream_info) -> Value {
                    absl::string_view upstream_cluster_name;
                    if (stream_info.upstreamClusterInfo().has_value() &&
                        stream_info.upstreamClusterInfo().value() != nullptr) {
                      upstream_cluster_name = stream_info.upstreamClusterInfo().value()->name();
                    }

                    if (upstream_cluster_name.empty()) {
                      return absl::monostate{};
                    }
                    return upstream_cluster_name;
                  });
            }}},
          {"UPSTREAM_LOCAL_ADDRESS",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return StreamInfoAddressFormatterProvider::withPort(
                  [](const StreamInfo::StreamInfo& stream_info)
                      -> Network::Address::InstanceConstSharedPtr {
                    if (stream_info.upstreamInfo().has_value()) {
                      return stream_info.upstreamInfo().value().get().upstreamLocalAddress();
                    }
                    return nullptr;
                  });
            }}},
          {"UPSTREAM_LOCAL_ADDRESS_WITHOUT_PORT",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return StreamInfoAddressFormatterProvider::withoutPort(
                  [](const StreamInfo::StreamInfo& stream_info)
                      -> Network::Address::InstanceConstSharedPtr {
                    if (stream_info.upstreamInfo().has_value()) {
                      return stream_info.upstreamInfo().value().get().upstreamLocalAddress();
                    }
                    return nullptr;
                  });
            }}},
          {"UPSTREAM_LOCAL_PORT",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return StreamInfoAddressFormatterProvider::justPort(
                  [](const StreamInfo::StreamInfo& stream_info)
                      -> Network::Address::InstanceConstSharedPtr {
                    if (stream_info.upstreamInfo().has_value()) {
                      return stream_info.upstreamInfo().value().get().upstreamLocalAddress();
                    }
                    return nullptr;
                  });
            }}},
          {"UPSTREAM_REMOTE_ADDRESS",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return StreamInfoAddressFormatterProvider::withPort(
                  [](const StreamInfo::StreamInfo& stream_info)
                      -> Network::Address::InstanceConstSharedPtr {
                    return getUpstreamRemoteAddress(stream_info);
                  });
            }}},
          {"UPSTREAM_REMOTE_ADDRESS_WITHOUT_PORT",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return StreamInfoAddressFormatterProvider::withoutPort(
                  [](const StreamInfo::StreamInfo& stream_info)
                      -> Network::Address::InstanceConstSharedPtr {
                    return getUpstreamRemoteAddress(stream_info);
                  });
            }}},
          {"UPSTREAM_REMOTE_PORT",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return StreamInfoAddressFormatterProvider::justPort(
                  [](const StreamInfo::StreamInfo& stream_info)
                      -> Network::Address::InstanceConstSharedPtr {
                    return getUpstreamRemoteAddress(stream_info);
                  });
            }}},
          {"UPSTREAM_REQUEST_ATTEMPT_COUNT",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoValueFormatterProvider>(
                  [](const StreamInfo::StreamInfo& stream_info) {
                    return static_cast<uint64_t>(stream_info.attemptCount().value_or(0));
                  });
            }}},
          {"UPSTREAM_TLS_CIPHER",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoUpstreamSslConnectionInfoFormatterProvider>(
                  [](const Ssl::ConnectionInfo& connection_info) {
                    return connection_info.ciphersuiteString();
                  });
            }}},
          {"UPSTREAM_TLS_VERSION",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoUpstreamSslConnectionInfoFormatterProvider>(
                  [](const Ssl::ConnectionInfo& connection_info) {
                    return connection_info.tlsVersion();
                  });
            }}},
          {"UPSTREAM_TLS_SESSION_ID",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoUpstreamSslConnectionInfoFormatterProvider>(
                  [](const Ssl::ConnectionInfo& connection_info) {
                    return connection_info.sessionId();
                  });
            }}},
          {"UPSTREAM_PEER_ISSUER",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoUpstreamSslConnectionInfoFormatterProvider>(
                  [](const Ssl::ConnectionInfo& connection_info) {
                    return connection_info.issuerPeerCertificate();
                  });
            }}},
          {"UPSTREAM_PEER_CERT",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoUpstreamSslConnectionInfoFormatterProvider>(
                  [](const Ssl::ConnectionInfo& connection_info) {
                    return connection_info.urlEncodedPemEncodedPeerCertificate();
                  });
            }}},
          {"UPSTREAM_PEER_SUBJECT",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoUpstreamSslConnectionInfoFormatterProvider>(
                  [](const Ssl::ConnectionInfo& connection_info) {
                    return connection_info.subjectPeerCertificate();
                  });
            }}},
          {"DOWNSTREAM_LOCAL_ADDRESS",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return StreamInfoAddressFormatterProvider::withPort(
                  [](const StreamInfo::StreamInfo& stream_info) {
                    return stream_info.downstreamAddressProvider().localAddress();
                  });
            }}},
          {"DOWNSTREAM_LOCAL_ADDRESS_WITHOUT_PORT",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return StreamInfoAddressFormatterProvider::withoutPort(
                  [](const Envoy::StreamInfo::StreamInfo& stream_info) {
                    return stream_info.downstreamAddressProvider().localAddress();
                  });
            }}},
          {"DOWNSTREAM_LOCAL_PORT",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return StreamInfoAddressFormatterProvider::justPort(
                  [](const Envoy::StreamInfo::StreamInfo& stream_info) {
                    return stream_info.downstreamAddressProvider().localAddress();
                  });
            }}},
          {"DOWNSTREAM_REMOTE_ADDRESS",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return StreamInfoAddressFormatterProvider::withPort(
                  [](const StreamInfo::StreamInfo& stream_info) {
                    return stream_info.downstreamAddressProvider().remoteAddress();
                  });
            }}},
          {"DOWNSTREAM_REMOTE_ADDRESS_WITHOUT_PORT",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return StreamInfoAddressFormatterProvider::withoutPort(
                  [](const StreamInfo::StreamInfo& stream_info) {
                    return stream_info.downstreamAddressProvider().remoteAddress();
                  });
            }}},
          {"DOWNSTREAM_REMOTE_PORT",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return StreamInfoAddressFormatterProvider::justPort(
                  [](const Envoy::StreamInfo::StreamInfo& stream_info) {
                    return stream_info.downstreamAddressProvider().remoteAddress();
                  });
            }}},
          {"DOWNSTREAM_DIRECT_REMOTE_ADDRESS",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return StreamInfoAddressFormatterProvider::withPort(
                  [](const StreamInfo::StreamInfo& stream_info) {
                    return stream_info.downstreamAddressProvider().directRemoteAddress();
                  });
            }}},
          {"DOWNSTREAM_DIRECT_REMOTE_ADDRESS_WITHOUT_PORT",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return StreamInfoAddressFormatterProvider::withoutPort(
                  [](const StreamInfo::StreamInfo& stream_info) {
                    return stream_info.downstreamAddressProvider().directRemoteAddress();
                  });
            }}},
          {"DOWNSTREAM_DIRECT_REMOTE_PORT",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return StreamInfoAddressFormatterProvider::justPort(
                  [](const Envoy::StreamInfo::StreamInfo& stream_info) {
                    return stream_info.downstreamAddressProvider().directRemoteAddress();
                  });
            }}},
          {"CONNECTION_ID",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoValueFormatterProvider>(
                  [](const StreamInfo::StreamInfo& stream_info) {
                    return stream_info.downstreamAddressProvider().connectionID().value_or(0);
                  });
            }}},
          {"REQUESTED_SERVER_NAME",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoValueFormatterProvider>(
                  [](const StreamInfo::StreamInfo& stream_info) -> Value {
                    if (!stream_info.downstreamAddressProvider().requestedServerName().empty()) {
                      if (Runtime::runtimeFeatureEnabled(
                              "envoy.reloadable_features.sanitize_sni_in_access_log")) {
                        return StringUtil::sanitizeInvalidHostname(
                            stream_info.downstreamAddressProvider().requestedServerName());
                      } else {
                        return stream_info.downstreamAddressProvider().requestedServerName();
                      }
                    }
                    return absl::monostate{};
                  });
            }}},
          {"ROUTE_NAME",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoValueFormatterProvider>(
                  [](const StreamInfo::StreamInfo& stream_info) -> Value {
                    absl::string_view route_name = stream_info.getRouteName();
                    if (!route_name.empty()) {
                      return route_name;
                    }
                    return absl::monostate{};
                  });
            }}},
          {"UPSTREAM_PEER_URI_SAN",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoUpstreamSslConnectionInfoFormatterProvider>(
                  [](const Ssl::ConnectionInfo& connection_info) {
                    return absl::StrJoin(connection_info.uriSanPeerCertificate(), ",");
                  });
            }}},
          {"UPSTREAM_PEER_DNS_SAN",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoUpstreamSslConnectionInfoFormatterProvider>(
                  [](const Ssl::ConnectionInfo& connection_info) {
                    return absl::StrJoin(connection_info.dnsSansPeerCertificate(), ",");
                  });
            }}},
          {"UPSTREAM_PEER_IP_SAN",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoUpstreamSslConnectionInfoFormatterProvider>(
                  [](const Ssl::ConnectionInfo& connection_info) {
                    return absl::StrJoin(connection_info.ipSansPeerCertificate(), ",");
                  });
            }}},
          {"UPSTREAM_LOCAL_URI_SAN",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoUpstreamSslConnectionInfoFormatterProvider>(
                  [](const Ssl::ConnectionInfo& connection_info) {
                    return absl::StrJoin(connection_info.uriSanLocalCertificate(), ",");
                  });
            }}},
          {"UPSTREAM_LOCAL_DNS_SAN",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoUpstreamSslConnectionInfoFormatterProvider>(
                  [](const Ssl::ConnectionInfo& connection_info) {
                    return absl::StrJoin(connection_info.dnsSansLocalCertificate(), ",");
                  });
            }}},
          {"UPSTREAM_LOCAL_IP_SAN",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoUpstreamSslConnectionInfoFormatterProvider>(
                  [](const Ssl::ConnectionInfo& connection_info) {
                    return absl::StrJoin(connection_info.ipSansLocalCertificate(), ",");
                  });
            }}},
          {"DOWNSTREAM_PEER_URI_SAN",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoSslConnectionInfoFormatterProvider>(
                  [](const Ssl::ConnectionInfo& connection_info) {
                    return absl::StrJoin(connection_info.uriSanPeerCertificate(), ",");
                  });
            }}},
          {"DOWNSTREAM_PEER_DNS_SAN",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoSslConnectionInfoFormatterProvider>(
                  [](const Ssl::ConnectionInfo& connection_info) {
                    return absl::StrJoin(connection_info.dnsSansPeerCertificate(), ",");
                  });
            }}},
          {"DOWNSTREAM_PEER_IP_SAN",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoSslConnectionInfoFormatterProvider>(
                  [](const Ssl::ConnectionInfo& connection_info) {
                    return absl::StrJoin(connection_info.ipSansPeerCertificate(), ",");
                  });
            }}},
          {"DOWNSTREAM_LOCAL_URI_SAN",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoSslConnectionInfoFormatterProvider>(
                  [](const Ssl::ConnectionInfo& connection_info) {
                    return absl::StrJoin(connection_info.uriSanLocalCertificate(), ",");
                  });
            }}},
          {"DOWNSTREAM_LOCAL_DNS_SAN",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoSslConnectionInfoFormatterProvider>(
                  [](const Ssl::ConnectionInfo& connection_info) {
                    return absl::StrJoin(connection_info.dnsSansLocalCertificate(), ",");
                  });
            }}},
          {"DOWNSTREAM_LOCAL_IP_SAN",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoSslConnectionInfoFormatterProvider>(
                  [](const Ssl::ConnectionInfo& connection_info) {
                    return absl::StrJoin(connection_info.ipSansLocalCertificate(), ",");
                  });
            }}},
          {"DOWNSTREAM_PEER_SUBJECT",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoSslConnectionInfoFormatterProvider>(
                  [](const Ssl::ConnectionInfo& connection_info) {
                    return connection_info.subjectPeerCertificate();
                  });
            }}},
          {"DOWNSTREAM_LOCAL_SUBJECT",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoSslConnectionInfoFormatterProvider>(
                  [](const Ssl::ConnectionInfo& connection_info) {
                    return connection_info.subjectLocalCertificate();
                  });
            }}},
          {"DOWNSTREAM_TLS_SESSION_ID",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoSslConnectionInfoFormatterProvider>(
                  [](const Ssl::ConnectionInfo& connection_info) {
                    return connection_info.sessionId();
                  });
            }}},
          {"DOWNSTREAM_TLS_CIPHER",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoSslConnectionInfoFormatterProvider>(
                  [](const Ssl::ConnectionInfo& connection_info) {
                    return connection_info.ciphersuiteString();
                  });
            }}},
          {"DOWNSTREAM_TLS_VERSION",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoSslConnectionInfoFormatterProvider>(
                  [](const Ssl::ConnectionInfo& connection_info) {
                    return connection_info.tlsVersion();
                  });
            }}},
          {"DOWNSTREAM_PEER_FINGERPRINT_256",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoSslConnectionInfoFormatterProvider>(
                  [](const Ssl::ConnectionInfo& connection_info) {
                    return connection_info.sha256PeerCertificateDigest();
                  });
            }}},
          {"DOWNSTREAM_PEER_FINGERPRINT_1",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoSslConnectionInfoFormatterProvider>(
                  [](const Ssl::ConnectionInfo& connection_info) {
                    return connection_info.sha1PeerCertificateDigest();
                  });
            }}},
          {"DOWNSTREAM_PEER_SERIAL",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoSslConnectionInfoFormatterProvider>(
                  [](const Ssl::ConnectionInfo& connection_info) {
                    return connection_info.serialNumberPeerCertificate();
                  });
            }}},
          {"DOWNSTREAM_PEER_CHAIN_FINGERPRINTS_256",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](const absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoSslConnectionInfoFormatterProvider>(
                  [](const Ssl::ConnectionInfo& connection_info) {
                    return absl::StrJoin(connection_info.sha256PeerCertificateChainDigests(), ",");
                  });
            }}},
          {"DOWNSTREAM_PEER_CHAIN_FINGERPRINTS_1",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](const absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoSslConnectionInfoFormatterProvider>(
                  [](const Ssl::ConnectionInfo& connection_info) {
                    return absl::StrJoin(connection_info.sha1PeerCertificateChainDigests(), ",");
                  });
            }}},
          {"DOWNSTREAM_PEER_CHAIN_SERIALS",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](const absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoSslConnectionInfoFormatterProvider>(
                  [](const Ssl::ConnectionInfo& connection_info) {
                    return absl::StrJoin(connection_info.serialNumbersPeerCertificates(), ",");
                  });
            }}},
          {"DOWNSTREAM_PEER_ISSUER",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoSslConnectionInfoFormatterProvider>(
                  [](const Ssl::ConnectionInfo& connection_info) {
                    return connection_info.issuerPeerCertificate();
                  });
            }}},
          {"DOWNSTREAM_PEER_CERT",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoSslConnectionInfoFormatterProvider>(
                  [](const Ssl::ConnectionInfo& connection_info) {
                    return connection_info.urlEncodedPemEncodedPeerCertificate();
                  });
            }}},
          {"DOWNSTREAM_TRANSPORT_FAILURE_REASON",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoValueFormatterProvider>(
                  [](const StreamInfo::StreamInfo& stream_info) -> Value {
                    if (!stream_info.downstreamTransportFailureReason().empty()) {
                      std::string result = absl::StrReplaceAll(
                          stream_info.downstreamTransportFailureReason(), {{" ", "_"}});
                      return result;
                    }
                    return absl::monostate{};
                  });
            }}},
          {"UPSTREAM_TRANSPORT_FAILURE_REASON",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoValueFormatterProvider>(
                  [](const StreamInfo::StreamInfo& stream_info) -> Value {
                    if (stream_info.upstreamInfo().has_value() &&
                        !stream_info.upstreamInfo()
                             .value()
                             .get()
                             .upstreamTransportFailureReason()
                             .empty()) {
                      std::string result =
                          stream_info.upstreamInfo().value().get().upstreamTransportFailureReason();
                      std::replace(result.begin(), result.end(), ' ', '_');
                      return result;
                    }
                    return absl::monostate{};
                  });
            }}},
          {"HOSTNAME",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              absl::optional<std::string> hostname = SubstitutionFormatUtils::getHostname();
              return std::make_unique<StreamInfoValueFormatterProvider>(
                  [hostname](const StreamInfo::StreamInfo&) -> Value {
                    if (hostname.has_value()) {
                      return absl::string_view{hostname.value()};
                    }
                    return absl::monostate{};
                  });
            }}},
          {"FILTER_CHAIN_NAME",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoValueFormatterProvider>(
                  [](const StreamInfo::StreamInfo& stream_info) -> Value {
                    if (const auto info = stream_info.downstreamAddressProvider().filterChainInfo();
                        info.has_value()) {
                      if (!info->name().empty()) {
                        return info->name();
                      }
                    }
                    return absl::monostate{};
                  });
            }}},
          {"VIRTUAL_CLUSTER_NAME",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoValueFormatterProvider>(
                  [](const StreamInfo::StreamInfo& stream_info) -> Value {
                    const auto& virtual_cluster_name = stream_info.virtualClusterName();
                    if (virtual_cluster_name.has_value()) {
                      return absl::string_view{virtual_cluster_name.value()};
                    }
                    return absl::monostate{};
                  });
            }}},
          {"TLS_JA3_FINGERPRINT",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoValueFormatterProvider>(
                  [](const StreamInfo::StreamInfo& stream_info) -> Value {
                    absl::optional<std::string> result;
                    if (!stream_info.downstreamAddressProvider().ja3Hash().empty()) {
                      return stream_info.downstreamAddressProvider().ja3Hash();
                    }
                    return absl::monostate{};
                  });
            }}},
          {"UNIQUE_ID",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, const absl::optional<size_t>&) {
              return std::make_unique<StreamInfoValueFormatterProvider>(
                  [](const StreamInfo::StreamInfo&) -> Value {
                    return Random::RandomUtility::uuid();
                  });
            }}},
          {"STREAM_ID",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoValueFormatterProvider>(
                  [](const StreamInfo::StreamInfo& stream_info) -> Value {
                    auto provider = stream_info.getStreamIdProvider();
                    if (!provider.has_value()) {
                      return {};
                    }
                    auto id = provider->toStringView();
                    if (!id.has_value()) {
                      return absl::monostate{};
                    }
                    return id.value();
                  });
            }}},
          {"START_TIME",
           {CommandSyntaxChecker::PARAMS_OPTIONAL,
            [](absl::string_view format, absl::optional<size_t>) {
              return std::make_unique<SystemTimeFormatter>(
                  format,
                  std::make_unique<SystemTimeFormatter::TimeFieldExtractor>(
                      [](const StreamInfo::StreamInfo& stream_info) -> absl::optional<SystemTime> {
                        return stream_info.startTime();
                      }));
            }}},
          {"START_TIME_LOCAL",
           {CommandSyntaxChecker::PARAMS_OPTIONAL,
            [](absl::string_view format, absl::optional<size_t>) {
              return std::make_unique<SystemTimeFormatter>(
                  format,
                  std::make_unique<SystemTimeFormatter::TimeFieldExtractor>(
                      [](const StreamInfo::StreamInfo& stream_info) -> absl::optional<SystemTime> {
                        return stream_info.startTime();
                      }),
                  true);
            }}},
          {"EMIT_TIME",
           {CommandSyntaxChecker::PARAMS_OPTIONAL,
            [](absl::string_view format, absl::optional<size_t>) {
              return std::make_unique<SystemTimeFormatter>(
                  format,
                  std::make_unique<SystemTimeFormatter::TimeFieldExtractor>(
                      [](const StreamInfo::StreamInfo& stream_info) -> absl::optional<SystemTime> {
                        return stream_info.timeSource().systemTime();
                      }));
            }}},
          {"EMIT_TIME_LOCAL",
           {CommandSyntaxChecker::PARAMS_OPTIONAL,
            [](absl::string_view format, absl::optional<size_t>) {
              return std::make_unique<SystemTimeFormatter>(
                  format,
                  std::make_unique<SystemTimeFormatter::TimeFieldExtractor>(
                      [](const StreamInfo::StreamInfo& stream_info) -> absl::optional<SystemTime> {
                        return stream_info.timeSource().systemTime();
                      }),
                  true);
            }}},
          {"DYNAMIC_METADATA",
           {CommandSyntaxChecker::PARAMS_REQUIRED,
            [](absl::string_view format, absl::optional<size_t> max_length) {
              absl::string_view filter_namespace;
              std::vector<absl::string_view> path;

              SubstitutionFormatUtils::parseSubcommand(format, ':', filter_namespace, path);
              return std::make_unique<DynamicMetadataFormatter>(filter_namespace, path, max_length);
            }}},

          {"CLUSTER_METADATA",
           {CommandSyntaxChecker::PARAMS_REQUIRED,
            [](absl::string_view format, absl::optional<size_t> max_length) {
              absl::string_view filter_namespace;
              std::vector<absl::string_view> path;

              SubstitutionFormatUtils::parseSubcommand(format, ':', filter_namespace, path);
              return std::make_unique<ClusterMetadataFormatter>(filter_namespace, path, max_length);
            }}},
          {"UPSTREAM_METADATA",
           {CommandSyntaxChecker::PARAMS_REQUIRED,
            [](absl::string_view format, absl::optional<size_t> max_length) {
              absl::string_view filter_namespace;
              std::vector<absl::string_view> path;

              SubstitutionFormatUtils::parseSubcommand(format, ':', filter_namespace, path);
              return std::make_unique<UpstreamHostMetadataFormatter>(filter_namespace, path,
                                                                     max_length);
            }}},
          {"FILTER_STATE",
           {CommandSyntaxChecker::PARAMS_OPTIONAL | CommandSyntaxChecker::LENGTH_ALLOWED,
            [](absl::string_view format, absl::optional<size_t> max_length) {
              return FilterStateFormatter::create(format, max_length, false);
            }}},
          {"UPSTREAM_FILTER_STATE",
           {CommandSyntaxChecker::PARAMS_OPTIONAL | CommandSyntaxChecker::LENGTH_ALLOWED,
            [](absl::string_view format, absl::optional<size_t> max_length) {
              return FilterStateFormatter::create(format, max_length, true);
            }}},
          {"DOWNSTREAM_PEER_CERT_V_START",
           {CommandSyntaxChecker::PARAMS_OPTIONAL,
            [](absl::string_view format, absl::optional<size_t>) {
              return std::make_unique<DownstreamPeerCertVStartFormatter>(format);
            }}},
          {"DOWNSTREAM_PEER_CERT_V_END",
           {CommandSyntaxChecker::PARAMS_OPTIONAL,
            [](absl::string_view format, absl::optional<size_t>) {
              return std::make_unique<DownstreamPeerCertVEndFormatter>(format);
            }}},
          {"UPSTREAM_PEER_CERT_V_START",
           {CommandSyntaxChecker::PARAMS_OPTIONAL,
            [](absl::string_view format, absl::optional<size_t>) {
              return std::make_unique<UpstreamPeerCertVStartFormatter>(format);
            }}},
          {"UPSTREAM_PEER_CERT_V_END",
           {CommandSyntaxChecker::PARAMS_OPTIONAL,
            [](absl::string_view format, absl::optional<size_t>) {
              return std::make_unique<UpstreamPeerCertVEndFormatter>(format);
            }}},
          {"ENVIRONMENT",
           {CommandSyntaxChecker::PARAMS_REQUIRED | CommandSyntaxChecker::LENGTH_ALLOWED,
            [](absl::string_view key, absl::optional<size_t> max_length) {
              return std::make_unique<EnvironmentFormatter>(key, max_length);
            }}},
          {"UPSTREAM_CONNECTION_POOL_READY_DURATION",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, const absl::optional<size_t>&) {
              return std::make_unique<StreamInfoDurationFormatterProvider>(
                  [](const StreamInfo::StreamInfo& stream_info)
                      -> absl::optional<std::chrono::nanoseconds> {
                    if (auto upstream_info = stream_info.upstreamInfo();
                        upstream_info.has_value()) {
                      if (auto connection_pool_callback_latency =
                              upstream_info.value()
                                  .get()
                                  .upstreamTiming()
                                  .connectionPoolCallbackLatency();
                          connection_pool_callback_latency.has_value()) {
                        return connection_pool_callback_latency;
                      }
                    }
                    return absl::nullopt;
                  });
            }}},
      });
}

class BuiltInStreamInfoCommandParser : public StreamInfoCommandParser {
public:
  BuiltInStreamInfoCommandParser() = default;

  // StreamInfoCommandParser
  StreamInfoFormatterProviderPtr parse(absl::string_view command, absl::string_view sub_command,
                                       absl::optional<size_t> max_length) const override {

    auto it = getKnownStreamInfoFormatterProviders().find(command);

    // No throw because the stream info command parser may not be the last parser and other
    // formatter parsers may be tried.
    if (it == getKnownStreamInfoFormatterProviders().end()) {
      return nullptr;
    }
    // Check flags for the command.
    THROW_IF_NOT_OK(Envoy::Formatter::CommandSyntaxChecker::verifySyntax(
        (*it).second.first, command, sub_command, max_length));

    return (*it).second.second(sub_command, max_length);
  }
};

std::string DefaultBuiltInStreamInfoCommandParserFactory::name() const {
  return "envoy.built_in_formatters.stream_info.default";
}

StreamInfoCommandParserPtr
DefaultBuiltInStreamInfoCommandParserFactory::createCommandParser() const {
  return std::make_unique<BuiltInStreamInfoCommandParser>();
}

REGISTER_FACTORY(DefaultBuiltInStreamInfoCommandParserFactory,
                 BuiltInStreamInfoCommandParserFactory);

} // namespace Formatter
} // namespace Envoy
