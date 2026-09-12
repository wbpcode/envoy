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

// CodeStatsImpl and TaggedCodeStatsImpl name the stats they charge identically; they differ only
// in whether the response code, its class, the virtual host, the virtual cluster and the route
// travel with the stat as explicit tags or are left to be recovered from the name by the tag
// extraction rules. The stat names are therefore covered for both implementations, and the tags
// are covered for the tagged one below.
template <class CodeStatsType> class HttpCodeStatsTest : public testing::Test {
public:
  HttpCodeStatsTest()
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

  Stats::TestUtil::TestSymbolTable symbol_table_;
  Stats::TestUtil::TestStore global_store_;
  Stats::TestUtil::TestStore cluster_store_;
  CodeStatsType code_stats_;
  Stats::StatNamePool pool_;
  // ResponseStatInfo::prefix_; empty for the router, ext_authz and ratelimit call sites.
  std::string prefix_{"prefix"};
};

using CodeStatsImplementations = ::testing::Types<CodeStatsImpl, TaggedCodeStatsImpl>;
TYPED_TEST_SUITE(HttpCodeStatsTest, CodeStatsImplementations);

TYPED_TEST(HttpCodeStatsTest, NoCanary) {
  this->addResponse(201, false, false);
  this->addResponse(301, false, true);
  this->addResponse(401, false, false);
  this->addResponse(501, false, true);

  auto& store = this->cluster_store_;
  EXPECT_EQ(1U, store.counter("prefix.upstream_rq_2xx").value());
  EXPECT_EQ(1U, store.counter("prefix.upstream_rq_201").value());
  EXPECT_EQ(1U, store.counter("prefix.external.upstream_rq_2xx").value());
  EXPECT_EQ(1U, store.counter("prefix.external.upstream_rq_201").value());
  EXPECT_EQ(1U, store.counter("prefix.upstream_rq_3xx").value());
  EXPECT_EQ(1U, store.counter("prefix.upstream_rq_301").value());
  EXPECT_EQ(1U, store.counter("prefix.internal.upstream_rq_3xx").value());
  EXPECT_EQ(1U, store.counter("prefix.internal.upstream_rq_301").value());
  EXPECT_EQ(1U, store.counter("prefix.upstream_rq_4xx").value());
  EXPECT_EQ(1U, store.counter("prefix.upstream_rq_401").value());
  EXPECT_EQ(1U, store.counter("prefix.external.upstream_rq_4xx").value());
  EXPECT_EQ(1U, store.counter("prefix.external.upstream_rq_401").value());
  EXPECT_EQ(1U, store.counter("prefix.upstream_rq_5xx").value());
  EXPECT_EQ(1U, store.counter("prefix.upstream_rq_501").value());
  EXPECT_EQ(1U, store.counter("prefix.internal.upstream_rq_5xx").value());
  EXPECT_EQ(1U, store.counter("prefix.internal.upstream_rq_501").value());

  EXPECT_EQ(4U, store.counter("prefix.upstream_rq_completed").value());
  EXPECT_EQ(2U, store.counter("prefix.external.upstream_rq_completed").value());
  EXPECT_EQ(2U, store.counter("prefix.internal.upstream_rq_completed").value());

  EXPECT_EQ(19U, store.counters().size());
}

// The router, ext_authz and ratelimit all charge response stats with an empty prefix. The
// resulting names must match the non-empty-prefix shape minus the prefix, with no stray dot.
TYPED_TEST(HttpCodeStatsTest, EmptyPrefix) {
  this->prefix_.clear();
  this->addResponse(201, false, false);
  this->addResponse(301, false, true);

  auto& store = this->cluster_store_;
  EXPECT_EQ(1U, store.counter("upstream_rq_2xx").value());
  EXPECT_EQ(1U, store.counter("upstream_rq_201").value());
  EXPECT_EQ(1U, store.counter("external.upstream_rq_2xx").value());
  EXPECT_EQ(1U, store.counter("external.upstream_rq_201").value());
  EXPECT_EQ(1U, store.counter("upstream_rq_3xx").value());
  EXPECT_EQ(1U, store.counter("upstream_rq_301").value());
  EXPECT_EQ(1U, store.counter("internal.upstream_rq_3xx").value());
  EXPECT_EQ(1U, store.counter("internal.upstream_rq_301").value());

  EXPECT_EQ(2U, store.counter("upstream_rq_completed").value());
  EXPECT_EQ(1U, store.counter("external.upstream_rq_completed").value());
  EXPECT_EQ(1U, store.counter("internal.upstream_rq_completed").value());

  EXPECT_EQ(11U, store.counters().size());
}

TYPED_TEST(HttpCodeStatsTest, Canary) {
  this->addResponse(100, true, true);
  this->addResponse(200, true, true);
  this->addResponse(300, false, false);
  this->addResponse(500, true, false);

  auto& store = this->cluster_store_;
  EXPECT_EQ(1U, store.counter("prefix.upstream_rq_1xx").value());
  EXPECT_EQ(1U, store.counter("prefix.upstream_rq_100").value());
  EXPECT_EQ(1U, store.counter("prefix.internal.upstream_rq_1xx").value());
  EXPECT_EQ(1U, store.counter("prefix.internal.upstream_rq_100").value());
  EXPECT_EQ(1U, store.counter("prefix.canary.upstream_rq_1xx").value());
  EXPECT_EQ(1U, store.counter("prefix.canary.upstream_rq_100").value());

  EXPECT_EQ(1U, store.counter("prefix.upstream_rq_2xx").value());
  EXPECT_EQ(1U, store.counter("prefix.upstream_rq_200").value());
  EXPECT_EQ(1U, store.counter("prefix.internal.upstream_rq_2xx").value());
  EXPECT_EQ(1U, store.counter("prefix.internal.upstream_rq_200").value());
  EXPECT_EQ(1U, store.counter("prefix.canary.upstream_rq_2xx").value());
  EXPECT_EQ(1U, store.counter("prefix.canary.upstream_rq_200").value());
  EXPECT_EQ(1U, store.counter("prefix.upstream_rq_3xx").value());
  EXPECT_EQ(1U, store.counter("prefix.upstream_rq_300").value());
  EXPECT_EQ(1U, store.counter("prefix.external.upstream_rq_3xx").value());
  EXPECT_EQ(1U, store.counter("prefix.external.upstream_rq_300").value());
  EXPECT_EQ(1U, store.counter("prefix.upstream_rq_5xx").value());
  EXPECT_EQ(1U, store.counter("prefix.upstream_rq_500").value());
  EXPECT_EQ(1U, store.counter("prefix.external.upstream_rq_5xx").value());
  EXPECT_EQ(1U, store.counter("prefix.external.upstream_rq_500").value());
  EXPECT_EQ(1U, store.counter("prefix.canary.upstream_rq_5xx").value());
  EXPECT_EQ(1U, store.counter("prefix.canary.upstream_rq_500").value());

  EXPECT_EQ(4U, store.counter("prefix.upstream_rq_completed").value());
  EXPECT_EQ(2U, store.counter("prefix.external.upstream_rq_completed").value());
  EXPECT_EQ(2U, store.counter("prefix.internal.upstream_rq_completed").value());
  EXPECT_EQ(3U, store.counter("prefix.canary.upstream_rq_completed").value());

  EXPECT_EQ(26U, store.counters().size());
}

TYPED_TEST(HttpCodeStatsTest, UnknownResponseCodes) {
  this->addResponse(23, true, true);
  this->addResponse(600, false, false);
  this->addResponse(1000000, false, true);

  auto& store = this->cluster_store_;
  EXPECT_EQ(3U, store.counter("prefix.upstream_rq_unknown").value());
  EXPECT_EQ(2U, store.counter("prefix.internal.upstream_rq_unknown").value());
  EXPECT_EQ(1U, store.counter("prefix.canary.upstream_rq_unknown").value());
  EXPECT_EQ(1U, store.counter("prefix.external.upstream_rq_unknown").value());

  EXPECT_EQ(8U, store.counters().size());
}

TYPED_TEST(HttpCodeStatsTest, RequestVirtualCluster) {
  this->addResponse(200, false, false, "test-vhost", "test-cluster");

  auto& store = this->global_store_;
  EXPECT_EQ(
      1U, store.counter("vhost.test-vhost.vcluster.test-cluster.upstream_rq_completed").value());
  EXPECT_EQ(1U, store.counter("vhost.test-vhost.vcluster.test-cluster.upstream_rq_2xx").value());
  EXPECT_EQ(1U, store.counter("vhost.test-vhost.vcluster.test-cluster.upstream_rq_200").value());
}

TYPED_TEST(HttpCodeStatsTest, RequestRoute) {
  this->addResponse(200, false, false, "test-vhost", "", "", "", "test-route");

  auto& store = this->global_store_;
  EXPECT_EQ(1U, store.counter("vhost.test-vhost.route.test-route.upstream_rq_completed").value());
  EXPECT_EQ(1U, store.counter("vhost.test-vhost.route.test-route.upstream_rq_2xx").value());
  EXPECT_EQ(1U, store.counter("vhost.test-vhost.route.test-route.upstream_rq_200").value());
}

TYPED_TEST(HttpCodeStatsTest, PerZoneStats) {
  this->addResponse(200, false, false, "", "", "from_az", "to_az");

  auto& store = this->cluster_store_;
  EXPECT_EQ(1U, store.counter("prefix.zone.from_az.to_az.upstream_rq_completed").value());
  EXPECT_EQ(1U, store.counter("prefix.zone.from_az.to_az.upstream_rq_200").value());
  EXPECT_EQ(1U, store.counter("prefix.zone.from_az.to_az.upstream_rq_2xx").value());
}

TYPED_TEST(HttpCodeStatsTest, ResponseTimingTest) {
  Http::CodeStats::ResponseTimingInfo info{*this->global_store_.rootScope(),
                                           *this->cluster_store_.rootScope(),
                                           this->pool_.add("prefix"),
                                           std::chrono::milliseconds(5),
                                           true,
                                           true,
                                           this->pool_.add("vhost_name"),
                                           this->pool_.add("req_vcluster_name"),
                                           this->pool_.add("route_name"),
                                           this->pool_.add("from_az"),
                                           this->pool_.add("to_az")};

  this->code_stats_.chargeResponseTiming(info);

  const std::vector<uint64_t> five{5};
  EXPECT_EQ(five, this->cluster_store_.histogramValues("prefix.upstream_rq_time", false));
  EXPECT_EQ(five, this->cluster_store_.histogramValues("prefix.canary.upstream_rq_time", false));
  EXPECT_EQ(five, this->cluster_store_.histogramValues("prefix.internal.upstream_rq_time", false));
  EXPECT_EQ(five,
            this->cluster_store_.histogramValues("prefix.zone.from_az.to_az.upstream_rq_time",
                                                 false));
  EXPECT_EQ(five, this->global_store_.histogramValues(
                      "vhost.vhost_name.vcluster.req_vcluster_name.upstream_rq_time", false));
  EXPECT_EQ(five, this->global_store_.histogramValues(
                      "vhost.vhost_name.route.route_name.upstream_rq_time", false));
}

// The tags TaggedCodeStatsImpl attaches to the stats it charges are observable on the stats
// themselves, alongside the name each stat is tag-extracted to.
class TaggedCodeStatsTest : public HttpCodeStatsTest<TaggedCodeStatsImpl> {
public:
  using TagVector = std::vector<std::pair<std::string, std::string>>;

  static TagVector tagsOf(const Stats::Metric& metric) {
    TagVector tags;
    for (const Stats::Tag& tag : metric.tags()) {
      tags.emplace_back(tag.name_, tag.value_);
    }
    std::sort(tags.begin(), tags.end());
    return tags;
  }

  static void expectMetric(const Stats::Metric& metric, const std::string& name,
                           const std::string& tag_extracted_name, TagVector tags) {
    EXPECT_EQ(metric.tagExtractedName(), tag_extracted_name) << " for stat '" << name << "'";
    std::sort(tags.begin(), tags.end());
    EXPECT_EQ(tagsOf(metric), tags) << " for stat '" << name << "'";
  }

  static void expectCounter(Stats::TestUtil::TestStore& store, const std::string& name,
                            const std::string& tag_extracted_name, TagVector tags, uint64_t value) {
    Stats::CounterOptConstRef counter = store.findCounterByString(name);
    ASSERT_TRUE(counter.has_value()) << "no counter named '" << name << "'";
    EXPECT_EQ(counter->get().value(), value) << " for stat '" << name << "'";
    expectMetric(counter->get(), name, tag_extracted_name, std::move(tags));
  }

  static void expectHistogram(Stats::TestUtil::TestStore& store, const std::string& name,
                              const std::string& tag_extracted_name, TagVector tags,
                              const std::vector<uint64_t>& values) {
    Stats::HistogramOptConstRef histogram = store.findHistogramByString(name);
    ASSERT_TRUE(histogram.has_value()) << "no histogram named '" << name << "'";
    EXPECT_EQ(store.histogramValues(name, false), values) << " for stat '" << name << "'";
    expectMetric(histogram->get(), name, tag_extracted_name, std::move(tags));
  }

  const std::string& response_code_tag_{Config::TagNames::get().RESPONSE_CODE};
  const std::string& response_code_class_tag_{Config::TagNames::get().RESPONSE_CODE_CLASS};
  const std::string& route_tag_{Config::TagNames::get().ROUTE};
  const std::string& virtual_cluster_tag_{Config::TagNames::get().VIRTUAL_CLUSTER};
  const std::string& virtual_host_tag_{Config::TagNames::get().VIRTUAL_HOST};
};

TEST_F(TaggedCodeStatsTest, ExplicitTags) {
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

// An invalid response code has no class to charge, so the vhost and zone stats fall back to the
// untagged 'upstream_rq_unknown' leaf rather than charging an empty class name.
TEST_F(TaggedCodeStatsTest, ExplicitTagsUnknownResponseCode) {
  addResponse(600, false, false, "test-vhost", "test-cluster", "from_az", "to_az", "test-route");

  expectCounter(global_store_, "vhost.test-vhost.vcluster.test-cluster.upstream_rq_unknown",
                "vhost.vcluster.upstream_rq_unknown",
                {{virtual_host_tag_, "test-vhost"}, {virtual_cluster_tag_, "test-cluster"}}, 1);
  expectCounter(global_store_, "vhost.test-vhost.route.test-route.upstream_rq_unknown",
                "vhost.route.upstream_rq_unknown",
                {{virtual_host_tag_, "test-vhost"}, {route_tag_, "test-route"}}, 1);
  expectCounter(cluster_store_, "prefix.zone.from_az.to_az.upstream_rq_unknown",
                "prefix.zone.from_az.to_az.upstream_rq_unknown", {}, 1);
}

TEST_F(TaggedCodeStatsTest, ResponseTimingTags) {
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

TEST(CodeUtilityTest, GroupStrings) {
  EXPECT_EQ("1xx", CodeUtility::groupStringForResponseCode(Code::SwitchingProtocols));
  EXPECT_EQ("2xx", CodeUtility::groupStringForResponseCode(Code::OK));
  EXPECT_EQ("3xx", CodeUtility::groupStringForResponseCode(Code::Found));
  EXPECT_EQ("4xx", CodeUtility::groupStringForResponseCode(Code::NotFound));
  EXPECT_EQ("5xx", CodeUtility::groupStringForResponseCode(Code::NotImplemented));
  EXPECT_EQ("", CodeUtility::groupStringForResponseCode(uncheckedEnumCastForTest<Code>(600)));
}

TEST(CodeUtilityTest, All) {
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

} // namespace Http
} // namespace Envoy
