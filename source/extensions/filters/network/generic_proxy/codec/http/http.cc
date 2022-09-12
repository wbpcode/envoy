#include "source/extensions/filters/network/generic_proxy/codec/http/http.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace GenericProxy {
namespace HttpCodec {

RequestDecoderPtr HttpStreamCodecFactory::requestDecoder() const {
  return std::make_unique<HttpRequestDecoder>();
}

ResponseDecoderPtr HttpStreamCodecFactory::responseDecoder() const {
  return std::make_unique<HttpResponseDecoder>();
}
RequestEncoderPtr HttpStreamCodecFactory::requestEncoder() const {
  return std::make_unique<HttpRequestEncoder>();
}
ResponseEncoderPtr HttpStreamCodecFactory::responseEncoder() const {
  return std::make_unique<HttpResponseEncoder>();
}
MessageCreatorPtr HttpStreamCodecFactory::messageCreator() const {
  return std::make_unique<HttpMessageCreator>();
}

CodecFactoryPtr
HttpStreamCodecFactoryConfig::createFactory(const Protobuf::Message&,
                                            Envoy::Server::Configuration::FactoryContext&) {
  return std::make_unique<HttpStreamCodecFactory>();
}

REGISTER_FACTORY(HttpStreamCodecFactoryConfig, CodecFactoryConfig);

} // namespace HttpCodec
} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
