#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "envoy/stats/stats.h"

#include "source/common/common/empty_string.h"
#include "source/common/config/well_known_names.h"
#include "source/common/http/codes.h"
#include "source/common/http/header_map_impl.h"

#include "test/common/stats/stat_test_utility.h"
#include "test/test_common/enum_test_utils.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Http {

// The response code, its class, the virtual host, the virtual cluster and the route are carried by
// explicit tags rather than being recovered from the stat name by a tag extractor, so these stats
// are created against a real store, where the flat name, the name it is tag-extracted to and the
// tags attached to it are all observable.
using TagVector = std::vector<std::pair<std::string, std::string>>;

TagVector tagsOf(const Stats::Metric& metric) {
  TagVector tags;
  for (const Stats::Tag& tag : metric.tags()) {
    tags.emplace_back(tag.name_, tag.value_);
  }
  std::sort(tags.begin(), tags.end());
  return tags;
}

void expectMetric(const Stats::Metric& metric, const std::string& name,
                  const std::string& tag_extracted_name, TagVector tags) {
  EXPECT_EQ(metric.tagExtractedName(), tag_extracted_name) << " for stat '" << name << "'";
  std::sort(tags.begin(), tags.end());
  EXPECT_EQ(tagsOf(metric), tags) << " for stat '" << name << "'";
}

void expectCounter(Stats::TestUtil::TestStore& store, const std::string& name,
                   const std::string& tag_extracted_name, TagVector tags, uint64_t value) {
  Stats::CounterOptConstRef counter = store.findCounterByString(name);
  ASSERT_TRUE(counter.has_value()) << "no counter named '" << name << "'";
  EXPECT_EQ(counter->get().value(), value) << " for stat '" << name << "'";
  expectMetric(counter->get(), name, tag_extracted_name, std::move(tags));
}

void expectHistogram(Stats::TestUtil::TestStore& store, const std::string& name,
                     const std::string& tag_extracted_name, TagVector tags,
                     const std::vector<uint64_t>& values) {
  Stats::HistogramOptConstRef histogram = store.findHistogramByString(name);
  ASSERT_TRUE(histogram.has_value()) << "no histogram named '" << name << "'";
  EXPECT_EQ(store.histogramValues(name, false), values) << " for stat '" << name << "'";
  expectMetric(histogram->get(), name, tag_extracted_name, std::move(tags));
}

class CodeUtilityTest : public testing::Test {
public:
  CodeUtilityTest()
      : global_store_(*symbol_table_), cluster_store_(*symbol_table_), code_stats_(*symbol_table_),
        pool_(*symbol_table_) {}

  void addResponse(uint64_t code, bool canary, bool internal_request,
                   const std::string& request_vhost_name = EMPTY_STRING,
                   const std::string& request_vcluster_name = EMPTY_STRING,
                   const std::string& from_az = EMPTY_STRING,
                   const std::string& to_az = EMPTY_STRING,
                   const std::string& request_route_name = EMPTY_STRING) {
    Stats::StatName prefix = pool_.add(prefix_);
    Stats::StatName from_zone = pool_.add(from_az);
    Stats::StatName to_zone = pool_.add(to_az);
    Stats::StatName vhost_name = pool_.add(request_vhost_name);
    Stats::StatName vcluster_name = pool_.add(request_vcluster_name);
    Stats::StatName route_name = pool_.add(request_route_name);
    Http::CodeStats::ResponseStatInfo info{*global_store_.rootScope(),
                                           *cluster_store_.rootScope(),
                                           prefix,
                                           code,
                                           internal_request,
                                           vhost_name,
                                           vcluster_name,
                                           route_name,
                                           from_zone,
                                           to_zone,
                                           canary};

    code_stats_.chargeResponseStat(info, false);
  }

  const std::string& response_code_tag_{Config::TagNames::get().RESPONSE_CODE};
  const std::string& response_code_class_tag_{Config::TagNames::get().RESPONSE_CODE_CLASS};
  const std::string& route_tag_{Config::TagNames::get().ROUTE};
  const std::string& virtual_cluster_tag_{Config::TagNames::get().VIRTUAL_CLUSTER};
  const std::string& virtual_host_tag_{Config::TagNames::get().VIRTUAL_HOST};

  Stats::TestUtil::TestSymbolTable symbol_table_;
  Stats::TestUtil::TestStore global_store_;
  Stats::TestUtil::TestStore cluster_store_;
  Http::CodeStatsImpl code_stats_;
  Stats::StatNamePool pool_;
  // ResponseStatInfo::prefix_; empty for the router, ext_authz and ratelimit call sites.
  std::string prefix_{"prefix"};
};

TEST_F(CodeUtilityTest, GroupStrings) {
  EXPECT_EQ("1xx", CodeUtility::groupStringForResponseCode(Code::SwitchingProtocols));
  EXPECT_EQ("2xx", CodeUtility::groupStringForResponseCode(Code::OK));
  EXPECT_EQ("3xx", CodeUtility::groupStringForResponseCode(Code::Found));
  EXPECT_EQ("4xx", CodeUtility::groupStringForResponseCode(Code::NotFound));
  EXPECT_EQ("5xx", CodeUtility::groupStringForResponseCode(Code::NotImplemented));
  EXPECT_EQ("", CodeUtility::groupStringForResponseCode(uncheckedEnumCastForTest<Code>(600)));
}

TEST_F(CodeUtilityTest, NoCanary) {
  addResponse(201, false, false);
  addResponse(301, false, true);
  addResponse(401, false, false);
  addResponse(501, false, true);

  EXPECT_EQ(1U, cluster_store_.counter("prefix.upstream_rq_2xx").value());
  EXPECT_EQ(1U, cluster_store_.counter("prefix.upstream_rq_201").value());
  EXPECT_EQ(1U, cluster_store_.counter("prefix.external.upstream_rq_2xx").value());
  EXPECT_EQ(1U, cluster_store_.counter("prefix.external.upstream_rq_201").value());
  EXPECT_EQ(1U, cluster_store_.counter("prefix.upstream_rq_3xx").value());
  EXPECT_EQ(1U, cluster_store_.counter("prefix.upstream_rq_301").value());
  EXPECT_EQ(1U, cluster_store_.counter("prefix.internal.upstream_rq_3xx").value());
  EXPECT_EQ(1U, cluster_store_.counter("prefix.internal.upstream_rq_301").value());
  EXPECT_EQ(1U, cluster_store_.counter("prefix.upstream_rq_4xx").value());
  EXPECT_EQ(1U, cluster_store_.counter("prefix.upstream_rq_401").value());
  EXPECT_EQ(1U, cluster_store_.counter("prefix.external.upstream_rq_4xx").value());
  EXPECT_EQ(1U, cluster_store_.counter("prefix.external.upstream_rq_401").value());
  EXPECT_EQ(1U, cluster_store_.counter("prefix.upstream_rq_5xx").value());
  EXPECT_EQ(1U, cluster_store_.counter("prefix.upstream_rq_501").value());
  EXPECT_EQ(1U, cluster_store_.counter("prefix.internal.upstream_rq_5xx").value());
  EXPECT_EQ(1U, cluster_store_.counter("prefix.internal.upstream_rq_501").value());

  EXPECT_EQ(4U, cluster_store_.counter("prefix.upstream_rq_completed").value());
  EXPECT_EQ(2U, cluster_store_.counter("prefix.external.upstream_rq_completed").value());
  EXPECT_EQ(2U, cluster_store_.counter("prefix.internal.upstream_rq_completed").value());

  EXPECT_EQ(19U, cluster_store_.counters().size());
}

// The router, ext_authz and ratelimit all charge response stats with an empty prefix. The
// resulting names must match the non-empty-prefix shape minus the prefix, with no stray dot.
TEST_F(CodeUtilityTest, EmptyPrefix) {
  prefix_.clear();
  addResponse(201, false, false);
  addResponse(301, false, true);

  EXPECT_EQ(1U, cluster_store_.counter("upstream_rq_2xx").value());
  EXPECT_EQ(1U, cluster_store_.counter("upstream_rq_201").value());
  EXPECT_EQ(1U, cluster_store_.counter("external.upstream_rq_2xx").value());
  EXPECT_EQ(1U, cluster_store_.counter("external.upstream_rq_201").value());
  EXPECT_EQ(1U, cluster_store_.counter("upstream_rq_3xx").value());
  EXPECT_EQ(1U, cluster_store_.counter("upstream_rq_301").value());
  EXPECT_EQ(1U, cluster_store_.counter("internal.upstream_rq_3xx").value());
  EXPECT_EQ(1U, cluster_store_.counter("internal.upstream_rq_301").value());

  EXPECT_EQ(2U, cluster_store_.counter("upstream_rq_completed").value());
  EXPECT_EQ(1U, cluster_store_.counter("external.upstream_rq_completed").value());
  EXPECT_EQ(1U, cluster_store_.counter("internal.upstream_rq_completed").value());

  EXPECT_EQ(11U, cluster_store_.counters().size());
}

TEST_F(CodeUtilityTest, Canary) {
  addResponse(100, true, true);
  addResponse(200, true, true);
  addResponse(300, false, false);
  addResponse(500, true, false);

  EXPECT_EQ(1U, cluster_store_.counter("prefix.upstream_rq_1xx").value());
  EXPECT_EQ(1U, cluster_store_.counter("prefix.upstream_rq_100").value());
  EXPECT_EQ(1U, cluster_store_.counter("prefix.internal.upstream_rq_1xx").value());
  EXPECT_EQ(1U, cluster_store_.counter("prefix.internal.upstream_rq_100").value());
  EXPECT_EQ(1U, cluster_store_.counter("prefix.canary.upstream_rq_1xx").value());
  EXPECT_EQ(1U, cluster_store_.counter("prefix.canary.upstream_rq_100").value());

  EXPECT_EQ(1U, cluster_store_.counter("prefix.upstream_rq_2xx").value());
  EXPECT_EQ(1U, cluster_store_.counter("prefix.upstream_rq_200").value());
  EXPECT_EQ(1U, cluster_store_.counter("prefix.internal.upstream_rq_2xx").value());
  EXPECT_EQ(1U, cluster_store_.counter("prefix.internal.upstream_rq_200").value());
  EXPECT_EQ(1U, cluster_store_.counter("prefix.canary.upstream_rq_2xx").value());
  EXPECT_EQ(1U, cluster_store_.counter("prefix.canary.upstream_rq_200").value());
  EXPECT_EQ(1U, cluster_store_.counter("prefix.upstream_rq_3xx").value());
  EXPECT_EQ(1U, cluster_store_.counter("prefix.upstream_rq_300").value());
  EXPECT_EQ(1U, cluster_store_.counter("prefix.external.upstream_rq_3xx").value());
  EXPECT_EQ(1U, cluster_store_.counter("prefix.external.upstream_rq_300").value());
  EXPECT_EQ(1U, cluster_store_.counter("prefix.upstream_rq_5xx").value());
  EXPECT_EQ(1U, cluster_store_.counter("prefix.upstream_rq_500").value());
  EXPECT_EQ(1U, cluster_store_.counter("prefix.external.upstream_rq_5xx").value());
  EXPECT_EQ(1U, cluster_store_.counter("prefix.external.upstream_rq_500").value());
  EXPECT_EQ(1U, cluster_store_.counter("prefix.canary.upstream_rq_5xx").value());
  EXPECT_EQ(1U, cluster_store_.counter("prefix.canary.upstream_rq_500").value());

  EXPECT_EQ(4U, cluster_store_.counter("prefix.upstream_rq_completed").value());
  EXPECT_EQ(2U, cluster_store_.counter("prefix.external.upstream_rq_completed").value());
  EXPECT_EQ(2U, cluster_store_.counter("prefix.internal.upstream_rq_completed").value());
  EXPECT_EQ(3U, cluster_store_.counter("prefix.canary.upstream_rq_completed").value());

  EXPECT_EQ(26U, cluster_store_.counters().size());
}

TEST_F(CodeUtilityTest, UnknownResponseCodes) {
  addResponse(23, true, true);
  addResponse(600, false, false);
  addResponse(1000000, false, true);

  EXPECT_EQ(3U, cluster_store_.counter("prefix.upstream_rq_unknown").value());
  EXPECT_EQ(2U, cluster_store_.counter("prefix.internal.upstream_rq_unknown").value());
  EXPECT_EQ(1U, cluster_store_.counter("prefix.canary.upstream_rq_unknown").value());
  EXPECT_EQ(1U, cluster_store_.counter("prefix.external.upstream_rq_unknown").value());

  EXPECT_EQ(8U, cluster_store_.counters().size());
}

TEST_F(CodeUtilityTest, All) {
  const std::vector<std::pair<Code, std::string>> test_set = {
      std::make_pair(Code::Continue, "Continue"),
      std::make_pair(Code::SwitchingProtocols, "Switching Protocols"),
      std::make_pair(Code::OK, "OK"),
      std::make_pair(Code::Created, "Created"),
      std::make_pair(Code::Accepted, "Accepted"),
      std::make_pair(Code::NonAuthoritativeInformation, "Non-Authoritative Information"),
      std::make_pair(Code::NoContent, "No Content"),
      std::make_pair(Code::ResetContent, "Reset Content"),
      std::make_pair(Code::PartialContent, "Partial Content"),
      std::make_pair(Code::MultiStatus, "Multi-Status"),
      std::make_pair(Code::AlreadyReported, "Already Reported"),
      std::make_pair(Code::IMUsed, "IM Used"),
      std::make_pair(Code::MultipleChoices, "Multiple Choices"),
      std::make_pair(Code::MovedPermanently, "Moved Permanently"),
      std::make_pair(Code::Found, "Found"),
      std::make_pair(Code::SeeOther, "See Other"),
      std::make_pair(Code::NotModified, "Not Modified"),
      std::make_pair(Code::UseProxy, "Use Proxy"),
      std::make_pair(Code::TemporaryRedirect, "Temporary Redirect"),
      std::make_pair(Code::PermanentRedirect, "Permanent Redirect"),
      std::make_pair(Code::BadRequest, "Bad Request"),
      std::make_pair(Code::Unauthorized, "Unauthorized"),
      std::make_pair(Code::PaymentRequired, "Payment Required"),
      std::make_pair(Code::Forbidden, "Forbidden"),
      std::make_pair(Code::NotFound, "Not Found"),
      std::make_pair(Code::MethodNotAllowed, "Method Not Allowed"),
      std::make_pair(Code::NotAcceptable, "Not Acceptable"),
      std::make_pair(Code::ProxyAuthenticationRequired, "Proxy Authentication Required"),
      std::make_pair(Code::RequestTimeout, "Request Timeout"),
      std::make_pair(Code::Conflict, "Conflict"),
      std::make_pair(Code::Gone, "Gone"),
      std::make_pair(Code::LengthRequired, "Length Required"),
      std::make_pair(Code::PreconditionFailed, "Precondition Failed"),
      std::make_pair(Code::PayloadTooLarge, "Payload Too Large"),
      std::make_pair(Code::URITooLong, "URI Too Long"),
      std::make_pair(Code::UnsupportedMediaType, "Unsupported Media Type"),
      std::make_pair(Code::RangeNotSatisfiable, "Range Not Satisfiable"),
      std::make_pair(Code::ExpectationFailed, "Expectation Failed"),
      std::make_pair(Code::MisdirectedRequest, "Misdirected Request"),
      std::make_pair(Code::UnprocessableEntity, "Unprocessable Entity"),
      std::make_pair(Code::Locked, "Locked"),
      std::make_pair(Code::FailedDependency, "Failed Dependency"),
      std::make_pair(Code::UpgradeRequired, "Upgrade Required"),
      std::make_pair(Code::PreconditionRequired, "Precondition Required"),
      std::make_pair(Code::TooManyRequests, "Too Many Requests"),
      std::make_pair(Code::RequestHeaderFieldsTooLarge, "Request Header Fields Too Large"),
      std::make_pair(Code::InternalServerError, "Internal Server Error"),
      std::make_pair(Code::NotImplemented, "Not Implemented"),
      std::make_pair(Code::BadGateway, "Bad Gateway"),
      std::make_pair(Code::ServiceUnavailable, "Service Unavailable"),
      std::make_pair(Code::GatewayTimeout, "Gateway Timeout"),
      std::make_pair(Code::HTTPVersionNotSupported, "HTTP Version Not Supported"),
      std::make_pair(Code::VariantAlsoNegotiates, "Variant Also Negotiates"),
      std::make_pair(Code::InsufficientStorage, "Insufficient Storage"),
      std::make_pair(Code::LoopDetected, "Loop Detected"),
      std::make_pair(Code::NotExtended, "Not Extended"),
      std::make_pair(Code::NetworkAuthenticationRequired, "Network Authentication Required"),
      std::make_pair(uncheckedEnumCastForTest<Code>(600), "Unknown")};

  for (const auto& test_case : test_set) {
    EXPECT_EQ(test_case.second, CodeUtility::toString(test_case.first));
  }

  EXPECT_EQ(std::string("Unknown"), CodeUtility::toString(uncheckedEnumCastForTest<Code>(600)));
}

TEST_F(CodeUtilityTest, RequestVirtualCluster) {
  addResponse(200, false, false, "test-vhost", "test-cluster");

  EXPECT_EQ(1U,
            global_store_.counter("vhost.test-vhost.vcluster.test-cluster.upstream_rq_completed")
                .value());
  EXPECT_EQ(
      1U, global_store_.counter("vhost.test-vhost.vcluster.test-cluster.upstream_rq_2xx").value());
  EXPECT_EQ(
      1U, global_store_.counter("vhost.test-vhost.vcluster.test-cluster.upstream_rq_200").value());
}

TEST_F(CodeUtilityTest, PerZoneStats) {
  addResponse(200, false, false, "", "", "from_az", "to_az");

  EXPECT_EQ(1U, cluster_store_.counter("prefix.zone.from_az.to_az.upstream_rq_completed").value());
  EXPECT_EQ(1U, cluster_store_.counter("prefix.zone.from_az.to_az.upstream_rq_200").value());
  EXPECT_EQ(1U, cluster_store_.counter("prefix.zone.from_az.to_az.upstream_rq_2xx").value());
}

// The response code, its class, the virtual host, the virtual cluster and the route are attached
// as explicit tags, so the stats carry them whether or not the store also derives tags from the
// stat name with the tag extraction rules.
TEST_F(CodeUtilityTest, ExplicitTags) {
  addResponse(200, true, false, "test-vhost", "test-cluster", "from_az", "to_az", "test-route");
  addResponse(600, false, true);

  // The response code and its class are tags of their own, so the tag-extracted name drops the
  // code, and drops the class while keeping the 'xx' that surrounds it.
  expectCounter(cluster_store_, "prefix.upstream_rq_200", "prefix.upstream_rq",
                {{response_code_tag_, "200"}}, 1);
  expectCounter(cluster_store_, "prefix.upstream_rq_2xx", "prefix.upstream_rq_xx",
                {{response_code_class_tag_, "2"}}, 1);
  expectCounter(cluster_store_, "prefix.upstream_rq_completed", "prefix.upstream_rq_completed", {},
                2);

  // The category is part of the stat name, and the code tags come along with it.
  expectCounter(cluster_store_, "prefix.canary.upstream_rq_200", "prefix.canary.upstream_rq",
                {{response_code_tag_, "200"}}, 1);
  expectCounter(cluster_store_, "prefix.external.upstream_rq_2xx", "prefix.external.upstream_rq_xx",
                {{response_code_class_tag_, "2"}}, 1);
  expectCounter(cluster_store_, "prefix.external.upstream_rq_completed",
                "prefix.external.upstream_rq_completed", {}, 1);

  // The zones are part of the stat name; they carry no tags of their own.
  expectCounter(cluster_store_, "prefix.zone.from_az.to_az.upstream_rq_200",
                "prefix.zone.from_az.to_az.upstream_rq", {{response_code_tag_, "200"}}, 1);
  expectCounter(cluster_store_, "prefix.zone.from_az.to_az.upstream_rq_completed",
                "prefix.zone.from_az.to_az.upstream_rq_completed", {}, 1);

  // An invalid response code holds no code to tag the stat with, and goes into no class.
  expectCounter(cluster_store_, "prefix.upstream_rq_unknown", "prefix.upstream_rq_unknown", {}, 1);
  expectCounter(cluster_store_, "prefix.internal.upstream_rq_unknown",
                "prefix.internal.upstream_rq_unknown", {}, 1);

  // The virtual host, the virtual cluster and the route are tags as well.
  expectCounter(global_store_, "vhost.test-vhost.vcluster.test-cluster.upstream_rq_200",
                "vhost.vcluster.upstream_rq",
                {{virtual_host_tag_, "test-vhost"},
                 {virtual_cluster_tag_, "test-cluster"},
                 {response_code_tag_, "200"}},
                1);
  expectCounter(global_store_, "vhost.test-vhost.vcluster.test-cluster.upstream_rq_completed",
                "vhost.vcluster.upstream_rq_completed",
                {{virtual_host_tag_, "test-vhost"}, {virtual_cluster_tag_, "test-cluster"}}, 1);
  expectCounter(global_store_, "vhost.test-vhost.route.test-route.upstream_rq_2xx",
                "vhost.route.upstream_rq_xx",
                {{virtual_host_tag_, "test-vhost"},
                 {route_tag_, "test-route"},
                 {response_code_class_tag_, "2"}},
                1);
}

TEST_F(CodeUtilityTest, ResponseTimingTest) {
  Http::CodeStats::ResponseTimingInfo info{*global_store_.rootScope(),
                                           *cluster_store_.rootScope(),
                                           pool_.add("prefix"),
                                           std::chrono::milliseconds(5),
                                           true,
                                           true,
                                           pool_.add("vhost_name"),
                                           pool_.add("req_vcluster_name"),
                                           pool_.add("route_name"),
                                           pool_.add("from_az"),
                                           pool_.add("to_az")};

  code_stats_.chargeResponseTiming(info);

  const std::vector<uint64_t> five{5};
  expectHistogram(cluster_store_, "prefix.upstream_rq_time", "prefix.upstream_rq_time", {}, five);
  expectHistogram(cluster_store_, "prefix.canary.upstream_rq_time",
                  "prefix.canary.upstream_rq_time", {}, five);
  expectHistogram(cluster_store_, "prefix.internal.upstream_rq_time",
                  "prefix.internal.upstream_rq_time", {}, five);
  expectHistogram(cluster_store_, "prefix.zone.from_az.to_az.upstream_rq_time",
                  "prefix.zone.from_az.to_az.upstream_rq_time", {}, five);
  expectHistogram(global_store_, "vhost.vhost_name.vcluster.req_vcluster_name.upstream_rq_time",
                  "vhost.vcluster.upstream_rq_time",
                  {{virtual_host_tag_, "vhost_name"}, {virtual_cluster_tag_, "req_vcluster_name"}},
                  five);
  expectHistogram(global_store_, "vhost.vhost_name.route.route_name.upstream_rq_time",
                  "vhost.route.upstream_rq_time",
                  {{virtual_host_tag_, "vhost_name"}, {route_tag_, "route_name"}}, five);
}

} // namespace Http
} // namespace Envoy
