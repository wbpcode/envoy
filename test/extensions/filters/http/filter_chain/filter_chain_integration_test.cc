#include "envoy/extensions/filters/http/filter_chain/v3/filter_chain.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"

#include "source/common/protobuf/utility.h"

#include "test/integration/http_protocol_integration.h"

namespace Envoy {
namespace {

using FilterChainIntegrationTest = UpstreamDownstreamIntegrationTest;

INSTANTIATE_TEST_SUITE_P(
    Protocols, FilterChainIntegrationTest,
    testing::ValuesIn(UpstreamDownstreamIntegrationTest::getDefaultTestParams()),
    UpstreamDownstreamIntegrationTest::testParamsToString);

// Test basic filter chain with buffer filter
TEST_P(FilterChainIntegrationTest, BasicFilterChainWithBuffer) {
  const std::string filter_config = R"EOF(
name: envoy.filters.http.filter_chain
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.filter_chain.v3.FilterChainConfig
  filter_chain:
    filters:
    - name: envoy.filters.http.buffer
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.filters.http.buffer.v3.Buffer
        max_request_bytes: 65536
)EOF";

  config_helper_.prependFilter(filter_config, testing_downstream_filter_);
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest();

  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

// Test filter chain with request body
TEST_P(FilterChainIntegrationTest, FilterChainWithRequestBody) {
  const std::string filter_config = R"EOF(
name: envoy.filters.http.filter_chain
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.filter_chain.v3.FilterChainConfig
  filter_chain:
    filters:
    - name: envoy.filters.http.buffer
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.filters.http.buffer.v3.Buffer
        max_request_bytes: 65536
)EOF";

  config_helper_.prependFilter(filter_config, testing_downstream_filter_);
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto encoder_decoder = codec_client_->startRequest(default_request_headers_);
  request_encoder_ = &encoder_decoder.first;
  IntegrationStreamDecoderPtr response = std::move(encoder_decoder.second);

  codec_client_->sendData(*request_encoder_, "test body content", true);

  waitForNextUpstreamRequest();
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));

  // Verify the request body was received
  EXPECT_EQ("test body content", upstream_request_->body().toString());

  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

// Test empty filter chain (passthrough)
TEST_P(FilterChainIntegrationTest, EmptyFilterChainPassthrough) {
  const std::string filter_config = R"EOF(
name: envoy.filters.http.filter_chain
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.filter_chain.v3.FilterChainConfig
)EOF";

  config_helper_.prependFilter(filter_config, testing_downstream_filter_);
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest();

  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

// Test filter chain with per-route configuration using filter_chain_name
TEST_P(FilterChainIntegrationTest, PerRouteFilterChainByName) {
  const std::string filter_config = R"EOF(
name: envoy.filters.http.filter_chain
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.filter_chain.v3.FilterChainConfig
  filter_chain:
    filters:
    - name: envoy.filters.http.buffer
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.filters.http.buffer.v3.Buffer
        max_request_bytes: 1024
  filter_chains:
    large_buffer:
      filters:
      - name: envoy.filters.http.buffer
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.filters.http.buffer.v3.Buffer
          max_request_bytes: 65536
)EOF";

  config_helper_.prependFilter(filter_config, testing_downstream_filter_);

  // Add per-route config that references the large_buffer chain
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) {
        auto* virtual_host = hcm.mutable_route_config()->mutable_virtual_hosts(0);
        auto* route = virtual_host->mutable_routes(0);
        auto* typed_per_filter_config = route->mutable_typed_per_filter_configs();
        envoy::extensions::filters::http::filter_chain::v3::FilterChainConfigPerRoute per_route;
        per_route.set_filter_chain_name("large_buffer");
        (*typed_per_filter_config)["envoy.filters.http.filter_chain"].PackFrom(per_route);
      });

  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest();

  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

// Test filter chain with per-route configuration using inline filter_chain
TEST_P(FilterChainIntegrationTest, PerRouteInlineFilterChain) {
  const std::string filter_config = R"EOF(
name: envoy.filters.http.filter_chain
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.filter_chain.v3.FilterChainConfig
)EOF";

  config_helper_.prependFilter(filter_config, testing_downstream_filter_);

  // Add per-route config with inline filter chain
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) {
        auto* virtual_host = hcm.mutable_route_config()->mutable_virtual_hosts(0);
        auto* route = virtual_host->mutable_routes(0);
        auto* typed_per_filter_config = route->mutable_typed_per_filter_configs();

        envoy::extensions::filters::http::filter_chain::v3::FilterChainConfigPerRoute per_route;
        auto* filter_chain = per_route.mutable_filter_chain();
        auto* filter = filter_chain->add_filters();
        filter->set_name("envoy.filters.http.buffer");
        envoy::extensions::filters::http::buffer::v3::Buffer buffer_config;
        buffer_config.mutable_max_request_bytes()->set_value(65536);
        filter->mutable_typed_config()->PackFrom(buffer_config);

        (*typed_per_filter_config)["envoy.filters.http.filter_chain"].PackFrom(per_route);
      });

  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest();

  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

// Test filter chain with response body
TEST_P(FilterChainIntegrationTest, FilterChainWithResponseBody) {
  const std::string filter_config = R"EOF(
name: envoy.filters.http.filter_chain
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.filter_chain.v3.FilterChainConfig
  filter_chain:
    filters:
    - name: envoy.filters.http.buffer
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.filters.http.buffer.v3.Buffer
        max_request_bytes: 65536
)EOF";

  config_helper_.prependFilter(filter_config, testing_downstream_filter_);
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest();

  // Send response with body
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
  upstream_request_->encodeData("response body content", true);

  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ("response body content", response->body());
}

// Test that requests work correctly when named filter chain is not found
TEST_P(FilterChainIntegrationTest, NamedFilterChainNotFound) {
  const std::string filter_config = R"EOF(
name: envoy.filters.http.filter_chain
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.filter_chain.v3.FilterChainConfig
  filter_chain:
    filters:
    - name: envoy.filters.http.buffer
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.filters.http.buffer.v3.Buffer
        max_request_bytes: 65536
)EOF";

  config_helper_.prependFilter(filter_config, testing_downstream_filter_);

  // Add per-route config that references a non-existent chain
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) {
        auto* virtual_host = hcm.mutable_route_config()->mutable_virtual_hosts(0);
        auto* route = virtual_host->mutable_routes(0);
        auto* typed_per_filter_config = route->mutable_typed_per_filter_configs();
        envoy::extensions::filters::http::filter_chain::v3::FilterChainConfigPerRoute per_route;
        per_route.set_filter_chain_name("nonexistent_chain");
        (*typed_per_filter_config)["envoy.filters.http.filter_chain"].PackFrom(per_route);
      });

  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest();

  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  // Should fallback to default filter chain and succeed
  EXPECT_EQ("200", response->headers().getStatusValue());
}

} // namespace
} // namespace Envoy
