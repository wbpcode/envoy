#include <memory>

#include "envoy/extensions/filters/http/filter_chain/v3/filter_chain.pb.h"
#include "envoy/http/metadata_interface.h"

#include "source/extensions/filters/http/filter_chain/config.h"
#include "source/extensions/filters/http/filter_chain/filter.h"

#include "test/mocks/access_log/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/mocks/server/instance.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace FilterChain {
namespace {

class FilterChainFilterTest : public ::testing::Test {
public:
  FilterChainFilterTest() {
    // Create an empty filter config
    envoy::extensions::filters::http::filter_chain::v3::FilterChainConfig proto_config;
    filter_config_ = std::make_shared<FilterChainConfig>(
        proto_config, context_.serverFactoryContext(), "filter_chain.");
    filter_ = std::make_unique<Filter>(filter_config_);
    filter_->setDecoderFilterCallbacks(decoder_callbacks_);
    filter_->setEncoderFilterCallbacks(encoder_callbacks_);
  }

  void doAllDecodingCallbacks() {
    filter_->decodeHeaders(default_request_headers_, false);

    Http::MetadataMap metadata;
    filter_->decodeMetadata(metadata);

    Buffer::OwnedImpl buffer("data");
    filter_->decodeData(buffer, false);

    filter_->decodeTrailers(default_request_trailers_);

    filter_->decodeComplete();
  }

  void doAllEncodingCallbacks() {
    filter_->encode1xxHeaders(default_response_headers_);

    filter_->encodeHeaders(default_response_headers_, false);

    Http::MetadataMap metadata;
    filter_->encodeMetadata(metadata);

    Buffer::OwnedImpl buffer("data");
    filter_->encodeData(buffer, false);

    filter_->encodeTrailers(default_response_trailers_);

    filter_->encodeComplete();
  }

  testing::NiceMock<Server::Configuration::MockFactoryContext> context_;
  testing::NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
  testing::NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_callbacks_;
  FilterChainConfigSharedPtr filter_config_;
  std::unique_ptr<Filter> filter_;

  Http::TestRequestHeaderMapImpl default_request_headers_{
      {":method", "GET"}, {":path", "/test/long/url"}, {":scheme", "http"}, {":authority", "host"}};
  Http::TestRequestTrailerMapImpl default_request_trailers_{{"trailers", "something"}};
  Http::TestResponseHeaderMapImpl default_response_headers_{{":status", "200"}};
  Http::TestResponseTrailerMapImpl default_response_trailers_{{"trailers", "something"}};
};

// Test that the filter passes through when no filter chain is configured
TEST_F(FilterChainFilterTest, NoFilterChainPassThrough) {
  // With empty config, all callbacks should pass through
  EXPECT_EQ(Http::FilterHeadersStatus::Continue,
            filter_->decodeHeaders(default_request_headers_, false));

  Buffer::OwnedImpl buffer("data");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(buffer, false));

  EXPECT_EQ(Http::FilterTrailersStatus::Continue,
            filter_->decodeTrailers(default_request_trailers_));

  Http::MetadataMap metadata;
  EXPECT_EQ(Http::FilterMetadataStatus::Continue, filter_->decodeMetadata(metadata));

  EXPECT_EQ(Http::Filter1xxHeadersStatus::Continue,
            filter_->encode1xxHeaders(default_response_headers_));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue,
            filter_->encodeHeaders(default_response_headers_, false));

  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(buffer, false));

  EXPECT_EQ(Http::FilterTrailersStatus::Continue,
            filter_->encodeTrailers(default_response_trailers_));

  EXPECT_EQ(Http::FilterMetadataStatus::Continue, filter_->encodeMetadata(metadata));
}

// Test onDestroy and onStreamComplete with no delegated filter
TEST_F(FilterChainFilterTest, LifecycleCallbacksNoDelegate) {
  // These should not crash when no delegated filter exists
  EXPECT_NO_THROW(filter_->onDestroy());
  EXPECT_NO_THROW(filter_->onStreamComplete());
}

// Test complete decode/encode cycle with no filter chain
TEST_F(FilterChainFilterTest, CompleteCycleNoFilterChain) {
  EXPECT_NO_THROW({
    doAllDecodingCallbacks();
    doAllEncodingCallbacks();
    filter_->onStreamComplete();
    filter_->onDestroy();
  });
}

// Test with a configured filter chain
class FilterChainFilterWithConfigTest : public ::testing::Test {
public:
  FilterChainFilterWithConfigTest() {
    const std::string yaml_string = R"EOF(
    filter_chain:
      filters:
      - name: envoy.filters.http.buffer
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.filters.http.buffer.v3.Buffer
          max_request_bytes: 1024
    )EOF";

    envoy::extensions::filters::http::filter_chain::v3::FilterChainConfig proto_config;
    TestUtility::loadFromYaml(yaml_string, proto_config);
    filter_config_ = std::make_shared<FilterChainConfig>(
        proto_config, context_.serverFactoryContext(), "filter_chain.");
    filter_ = std::make_unique<Filter>(filter_config_);
    filter_->setDecoderFilterCallbacks(decoder_callbacks_);
    filter_->setEncoderFilterCallbacks(encoder_callbacks_);
  }

  testing::NiceMock<Server::Configuration::MockFactoryContext> context_;
  testing::NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
  testing::NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_callbacks_;
  FilterChainConfigSharedPtr filter_config_;
  std::unique_ptr<Filter> filter_;

  Http::TestRequestHeaderMapImpl default_request_headers_{
      {":method", "GET"}, {":path", "/test/long/url"}, {":scheme", "http"}, {":authority", "host"}};
  Http::TestRequestTrailerMapImpl default_request_trailers_{{"trailers", "something"}};
  Http::TestResponseHeaderMapImpl default_response_headers_{{":status", "200"}};
  Http::TestResponseTrailerMapImpl default_response_trailers_{{"trailers", "something"}};
};

// Test that decodeHeaders creates the delegated filter chain
TEST_F(FilterChainFilterWithConfigTest, DecodeHeadersCreatesDelegatedChain) {
  // The buffer filter will buffer data, so decodeHeaders returns StopIteration
  auto result = filter_->decodeHeaders(default_request_headers_, false);
  // Buffer filter stops iteration when not at end_stream
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, result);
}

// Test decode with end_stream true
TEST_F(FilterChainFilterWithConfigTest, DecodeHeadersWithEndStream) {
  auto result = filter_->decodeHeaders(default_request_headers_, true);
  // Buffer filter continues when at end_stream
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, result);
}

// Test onDestroy after delegation
TEST_F(FilterChainFilterWithConfigTest, OnDestroyAfterDelegation) {
  filter_->decodeHeaders(default_request_headers_, true);
  // Should not crash
  EXPECT_NO_THROW(filter_->onDestroy());
}

// Test onStreamComplete after delegation
TEST_F(FilterChainFilterWithConfigTest, OnStreamCompleteAfterDelegation) {
  filter_->decodeHeaders(default_request_headers_, true);
  // Should not crash
  EXPECT_NO_THROW(filter_->onStreamComplete());
}

// Test DelegatedFilterChain directly
class DelegatedFilterChainTest : public ::testing::Test {
public:
  DelegatedFilterChainTest() {
    mock_filter_ = std::make_shared<testing::NiceMock<Http::MockStreamFilter>>();
    std::vector<Http::StreamFilterSharedPtr> filters;
    filters.push_back(mock_filter_);
    delegated_chain_ = std::make_unique<DelegatedFilterChain>(std::move(filters));
  }

  std::shared_ptr<testing::NiceMock<Http::MockStreamFilter>> mock_filter_;
  std::unique_ptr<DelegatedFilterChain> delegated_chain_;

  Http::TestRequestHeaderMapImpl default_request_headers_{
      {":method", "GET"}, {":path", "/test"}, {":scheme", "http"}, {":authority", "host"}};
  Http::TestRequestTrailerMapImpl default_request_trailers_{{"trailers", "something"}};
  Http::TestResponseHeaderMapImpl default_response_headers_{{":status", "200"}};
  Http::TestResponseTrailerMapImpl default_response_trailers_{{"trailers", "something"}};
};

// Test DelegatedFilterChain decode callbacks
TEST_F(DelegatedFilterChainTest, DecodeCallbacks) {
  EXPECT_CALL(*mock_filter_, decodeHeaders(_, false))
      .WillOnce(testing::Return(Http::FilterHeadersStatus::Continue));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue,
            delegated_chain_->decodeHeaders(default_request_headers_, false));

  Buffer::OwnedImpl buffer("data");
  EXPECT_CALL(*mock_filter_, decodeData(_, false))
      .WillOnce(testing::Return(Http::FilterDataStatus::Continue));
  EXPECT_EQ(Http::FilterDataStatus::Continue, delegated_chain_->decodeData(buffer, false));

  EXPECT_CALL(*mock_filter_, decodeTrailers(_))
      .WillOnce(testing::Return(Http::FilterTrailersStatus::Continue));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue,
            delegated_chain_->decodeTrailers(default_request_trailers_));

  Http::MetadataMap metadata;
  EXPECT_CALL(*mock_filter_, decodeMetadata(_))
      .WillOnce(testing::Return(Http::FilterMetadataStatus::Continue));
  EXPECT_EQ(Http::FilterMetadataStatus::Continue, delegated_chain_->decodeMetadata(metadata));

  EXPECT_CALL(*mock_filter_, decodeComplete());
  delegated_chain_->decodeComplete();
}

// Test DelegatedFilterChain encode callbacks (reverse order)
TEST_F(DelegatedFilterChainTest, EncodeCallbacks) {
  EXPECT_CALL(*mock_filter_, encode1xxHeaders(_))
      .WillOnce(testing::Return(Http::Filter1xxHeadersStatus::Continue));
  EXPECT_EQ(Http::Filter1xxHeadersStatus::Continue,
            delegated_chain_->encode1xxHeaders(default_response_headers_));

  EXPECT_CALL(*mock_filter_, encodeHeaders(_, false))
      .WillOnce(testing::Return(Http::FilterHeadersStatus::Continue));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue,
            delegated_chain_->encodeHeaders(default_response_headers_, false));

  Buffer::OwnedImpl buffer("data");
  EXPECT_CALL(*mock_filter_, encodeData(_, false))
      .WillOnce(testing::Return(Http::FilterDataStatus::Continue));
  EXPECT_EQ(Http::FilterDataStatus::Continue, delegated_chain_->encodeData(buffer, false));

  EXPECT_CALL(*mock_filter_, encodeTrailers(_))
      .WillOnce(testing::Return(Http::FilterTrailersStatus::Continue));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue,
            delegated_chain_->encodeTrailers(default_response_trailers_));

  Http::MetadataMap metadata;
  EXPECT_CALL(*mock_filter_, encodeMetadata(_))
      .WillOnce(testing::Return(Http::FilterMetadataStatus::Continue));
  EXPECT_EQ(Http::FilterMetadataStatus::Continue, delegated_chain_->encodeMetadata(metadata));

  EXPECT_CALL(*mock_filter_, encodeComplete());
  delegated_chain_->encodeComplete();
}

// Test DelegatedFilterChain lifecycle callbacks
TEST_F(DelegatedFilterChainTest, LifecycleCallbacks) {
  EXPECT_CALL(*mock_filter_, onDestroy());
  delegated_chain_->onDestroy();

  EXPECT_CALL(*mock_filter_, onStreamComplete());
  delegated_chain_->onStreamComplete();
}

// Test that decode status propagation stops on non-Continue
TEST_F(DelegatedFilterChainTest, DecodeStatusPropagationStops) {
  EXPECT_CALL(*mock_filter_, decodeHeaders(_, false))
      .WillOnce(testing::Return(Http::FilterHeadersStatus::StopIteration));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            delegated_chain_->decodeHeaders(default_request_headers_, false));
}

// Test that encode status propagation stops on non-Continue
TEST_F(DelegatedFilterChainTest, EncodeStatusPropagationStops) {
  EXPECT_CALL(*mock_filter_, encodeHeaders(_, false))
      .WillOnce(testing::Return(Http::FilterHeadersStatus::StopIteration));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            delegated_chain_->encodeHeaders(default_response_headers_, false));
}

} // namespace
} // namespace FilterChain
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
