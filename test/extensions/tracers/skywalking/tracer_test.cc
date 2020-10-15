#include "extensions/tracers/skywalking/tracer.h"

#include "test/extensions/tracers/skywalking/skywalking_test_helper.h"
#include "test/mocks/common.h"
#include "test/mocks/stats/mocks.h"
#include "test/mocks/tracing/mocks.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using namespace testing;

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace SkyWalking {

class TracerTest : public testing::Test {
public:
  void setupTracer(const std::string& yaml_string) {
    EXPECT_CALL(mock_dispatcher_, createTimer_(_)).WillOnce(Invoke([this](Event::TimerCb timer_cb) {
      timer_cb_ = timer_cb;
      return timer_;
    }));

    timer_ = new NiceMock<Event::MockTimer>();

    auto mock_client_factory = std::make_unique<NiceMock<Grpc::MockAsyncClientFactory>>();

    auto mock_client = std::make_unique<NiceMock<Grpc::MockAsyncClient>>();

    mock_stream_ptr_ = std::make_unique<NiceMock<Grpc::MockAsyncStream>>();

    EXPECT_CALL(*mock_client, startRaw(_, _, _, _)).WillOnce(Return(mock_stream_ptr_.get()));
    EXPECT_CALL(*mock_client_factory, create()).WillOnce(Return(ByMove(std::move(mock_client))));

    TestUtility::loadFromYaml(yaml_string, client_config_);
    tracer_ = std::make_unique<Tracer>(mock_time_source_);
    tracer_->setReporter(std::make_unique<TraceSegmentReporter>(
        std::move(mock_client_factory), mock_dispatcher_, mock_random_generator_, tracing_stats_,
        client_config_));
  }

protected:
  NiceMock<Envoy::Tracing::MockConfig> mock_tracing_config_;
  NiceMock<Event::MockDispatcher> mock_dispatcher_;
  NiceMock<Random::MockRandomGenerator> mock_random_generator_;
  NiceMock<Envoy::MockTimeSystem> mock_time_source_;
  NiceMock<Stats::MockIsolatedStatsStore> mock_scope_;

  std::unique_ptr<NiceMock<Grpc::MockAsyncStream>> mock_stream_ptr_{nullptr};

  NiceMock<Event::MockTimer>* timer_;
  Event::TimerCb timer_cb_;

  envoy::config::trace::v3::ClientConfig client_config_;
  SkyWalkingTracerStats tracing_stats_{
      SKYWALKING_TRACER_STATS(POOL_COUNTER_PREFIX(mock_scope_, "tracing.skywalking."))};
  TracerPtr tracer_;
};

// Test that the basic functionality of Tracer is working, including creating Span, using Span to
// create new child Spans.
TEST_F(TracerTest, TracerTestCreateNewSpanWithNoPropagationHeaders) {
  setupTracer("{}");
  EXPECT_CALL(mock_random_generator_, random()).WillRepeatedly(Return(666666));
  Event::SimulatedTimeSystem time_system;
  ON_CALL(mock_time_source_, systemTime()).WillByDefault(Return(time_system.systemTime()));

  // Create a new SegmentContext.
  SegmentContextSharedPtr segment_context =
      SkyWalkingTestHelper::createSegmentContext(true, "CURR", "", mock_random_generator_);

  Envoy::Tracing::SpanPtr org_span = tracer_->startSpan(
      mock_tracing_config_, mock_time_source_.systemTime(), "TEST_OP", segment_context, nullptr);
  Span* span = dynamic_cast<Span*>(org_span.get());

  EXPECT_EQ(true, span->spanStore()->isEntrySpan());

  EXPECT_EQ("", span->getBaggage("FakeStringAndNothingToDo"));
  span->setBaggage("FakeStringAndNothingToDo", "FakeStringAndNothingToDo");

  // Test whether the basic functions of Span are normal.

  span->setSampled(false);
  EXPECT_EQ(false, span->spanStore()->sampled());

  // The initial operation name is consistent with the 'operation' parameter in the 'startSpan'
  // method call.
  EXPECT_EQ("TEST_OP", span->spanStore()->operation());
  span->setOperation("op");
  EXPECT_EQ("op", span->spanStore()->operation());

  // Test whether the tag can be set correctly.
  span->setTag("TestTagKeyA", "TestTagValueA");
  span->setTag("TestTagKeyB", "TestTagValueB");
  EXPECT_EQ("TestTagValueA", span->spanStore()->tags().at(0).value());
  EXPECT_EQ("TestTagValueB", span->spanStore()->tags().at(1).value());

  // When setting the status code tag, the corresponding tag name will be rewritten as
  // 'status_code'.
  span->setTag(Tracing::Tags::get().HttpStatusCode, "200");
  EXPECT_EQ("status_code", span->spanStore()->tags().at(2).key());
  EXPECT_EQ("200", span->spanStore()->tags().at(2).value());

  // When setting the error tag, the SpanStore object will also mark itself as an error.
  span->setTag(Tracing::Tags::get().Error, Tracing::Tags::get().True);
  EXPECT_EQ(Tracing::Tags::get().Error, span->spanStore()->tags().at(3).key());
  EXPECT_EQ(Tracing::Tags::get().True, span->spanStore()->tags().at(3).value());
  EXPECT_EQ(true, span->spanStore()->isError());

  // When setting http url tag, the corresponding tag name will be rewritten as 'url'.
  span->setTag(Tracing::Tags::get().HttpUrl, "http://test.com/test/path");
  EXPECT_EQ("url", span->spanStore()->tags().at(4).key());

  Envoy::Tracing::SpanPtr org_first_child_span =
      span->spawnChild(mock_tracing_config_, "TestChild", mock_time_source_.systemTime());
  Span* first_child_span = dynamic_cast<Span*>(org_first_child_span.get());

  EXPECT_EQ(false, first_child_span->spanStore()->isEntrySpan());

  EXPECT_EQ(0, first_child_span->spanStore()->sampled());
  EXPECT_EQ(1, first_child_span->spanStore()->spanId());
  EXPECT_EQ(0, first_child_span->spanStore()->parentSpanId());

  EXPECT_EQ("TestChild", first_child_span->spanStore()->operation());

  Http::TestRequestHeaderMapImpl first_child_headers{{":authority", "test.com"}};
  std::string expected_header_value = fmt::format(
      "{}-{}-{}-{}-{}-{}-{}-{}", 0,
      SkyWalkingTestHelper::base64Encode(SkyWalkingTestHelper::generateId(mock_random_generator_)),
      SkyWalkingTestHelper::base64Encode(SkyWalkingTestHelper::generateId(mock_random_generator_)),
      1, SkyWalkingTestHelper::base64Encode("CURR#SERVICE"),
      SkyWalkingTestHelper::base64Encode("CURR#INSTANCE"), SkyWalkingTestHelper::base64Encode("op"),
      SkyWalkingTestHelper::base64Encode("test.com"));

  first_child_span->injectContext(first_child_headers);
  EXPECT_EQ(expected_header_value, first_child_headers.get_("sw8"));

  // Reset sampling flag to true.
  span->setSampled(true);
  Envoy::Tracing::SpanPtr org_second_child_span =
      span->spawnChild(mock_tracing_config_, "TestChild", mock_time_source_.systemTime());
  Span* second_child_span = dynamic_cast<Span*>(org_second_child_span.get());

  EXPECT_EQ(1, second_child_span->spanStore()->sampled());
  EXPECT_EQ(2, second_child_span->spanStore()->spanId());
  EXPECT_EQ(0, second_child_span->spanStore()->parentSpanId());

  EXPECT_CALL(*mock_stream_ptr_, sendMessageRaw_(_, _)).Times(1);

  // When the child span ends, the data is not reported immediately, but the end time is set.
  first_child_span->finishSpan();
  second_child_span->finishSpan();
  EXPECT_NE(0, first_child_span->spanStore()->endTime());
  EXPECT_NE(0, second_child_span->spanStore()->endTime());

  EXPECT_EQ(0U, mock_scope_.counter("tracing.skywalking.segments_sent").value());
  EXPECT_EQ(0U, mock_scope_.counter("tracing.skywalking.segments_dropped").value());
  EXPECT_EQ(0U, mock_scope_.counter("tracing.skywalking.cache_flushed").value());
  EXPECT_EQ(0U, mock_scope_.counter("tracing.skywalking.segments_flushed").value());

  // When the first span in the current segment ends, the entire segment is reported.
  span->finishSpan();

  EXPECT_EQ(1U, mock_scope_.counter("tracing.skywalking.segments_sent").value());
  EXPECT_EQ(0U, mock_scope_.counter("tracing.skywalking.segments_dropped").value());
  EXPECT_EQ(0U, mock_scope_.counter("tracing.skywalking.cache_flushed").value());
  EXPECT_EQ(0U, mock_scope_.counter("tracing.skywalking.segments_flushed").value());
}

} // namespace SkyWalking
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
