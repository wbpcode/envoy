#include "source/extensions/filters/network/dubbo_proxy/decoder.h"

#include "source/common/common/macros.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {

DecoderStateMachine::DecoderStatus
DecoderStateMachine::onDecodeStreamHeader(Buffer::Instance& buffer) {
  ASSERT(!active_stream_);

  auto metadata = std::make_shared<MessageMetadata>();
  auto status = protocol_.decodeHeader(buffer, *metadata);
  if (status == CommonDecodeStatus::Waiting) {
    ENVOY_LOG(debug, "dubbo decoder: need more data for dubbo protocol");
    return {ProtocolState::WaitForData};
  }
  ASSERT(metadata->hasMessageContextInfo());

  active_stream_ = delegate_.newStream(metadata);
  return {ProtocolState::OnDecodeStreamData};
}

DecoderStateMachine::DecoderStatus
DecoderStateMachine::onDecodeStreamData(Buffer::Instance& buffer) {
  ASSERT(active_stream_);

  auto status = protocol_.decodeData(buffer, *active_stream_->metadata_);
  if (status == CommonDecodeStatus::Waiting) {
    ENVOY_LOG(debug, "dubbo decoder: need more data for {} serialization, current size {}",
              Utility::serializeTypeToString(protocol_.serializer()->type()), buffer.length());
    return {ProtocolState::WaitForData};
  }

  const auto& context = active_stream_->metadata_->messageContextInfo();
  if (context.heartbeat()) {
    delegate_.onHeartbeat(std::move(active_stream_->metadata_));
    active_stream_ = nullptr;
    return {ProtocolState::Done};
  }

  active_stream_->onStreamDecoded();
  active_stream_ = nullptr;

  ENVOY_LOG(debug, "dubbo decoder: ends the deserialization of the message");
  return {ProtocolState::Done};
}

DecoderStateMachine::DecoderStatus DecoderStateMachine::handleState(Buffer::Instance& buffer) {
  switch (state_) {
  case ProtocolState::OnDecodeStreamHeader:
    return onDecodeStreamHeader(buffer);
  case ProtocolState::OnDecodeStreamData:
    return onDecodeStreamData(buffer);
  default:
    PANIC("unexpected");
  }
}

ProtocolState DecoderStateMachine::run(Buffer::Instance& buffer) {
  while (state_ != ProtocolState::Done) {
    ENVOY_LOG(trace, "dubbo decoder: state {}, {} bytes available",
              ProtocolStateNameValues::name(state_), buffer.length());

    DecoderStatus s = handleState(buffer);
    if (s.next_state_ == ProtocolState::WaitForData) {
      return ProtocolState::WaitForData;
    }

    state_ = s.next_state_;
  }

  return state_;
}

using DecoderStateMachinePtr = std::unique_ptr<DecoderStateMachine>;

DecoderBase::DecoderBase(Protocol& protocol) : protocol_(protocol) {}

DecoderBase::~DecoderBase() { complete(); }

FilterStatus DecoderBase::onData(Buffer::Instance& data, bool& buffer_underflow) {
  ENVOY_LOG(debug, "dubbo decoder: {} bytes available", data.length());
  buffer_underflow = false;

  if (!decode_started_) {
    start();
  }

  ASSERT(state_machine_ != nullptr);

  ENVOY_LOG(debug, "dubbo decoder: protocol dubbo, state {}, {} bytes available",
            ProtocolStateNameValues::name(state_machine_->currentState()), data.length());

  ProtocolState rv = state_machine_->run(data);
  switch (rv) {
  case ProtocolState::WaitForData:
    ENVOY_LOG(debug, "dubbo decoder: wait for data");
    buffer_underflow = true;
    return FilterStatus::Continue;
  default:
    break;
  }

  ASSERT(rv == ProtocolState::Done);

  complete();
  buffer_underflow = (data.length() == 0);
  ENVOY_LOG(debug, "dubbo decoder: data length {}", data.length());
  return FilterStatus::Continue;
}

void DecoderBase::start() {
  state_machine_ = std::make_unique<DecoderStateMachine>(protocol_, *this);
  decode_started_ = true;
}

void DecoderBase::complete() {
  state_machine_.reset();
  stream_.reset();
  decode_started_ = false;
}

void DecoderBase::reset() { complete(); }

} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
