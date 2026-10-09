/**
 * @file node_check_in.cpp
 * @brief Telemetry check-in state machine driven by poll().
 *
 * Retries reuse seq and the replay counter. A new report advances both.
 */

#include "node_check_in.h"

#include "clock_port.h"
#include "message_types.h"
#include "packet_codec.h"
#include "radio_port.h"
#include "random_port.h"
#include "security.h"
#include "sequence_manager.h"

namespace node {

NodeCheckIn::NodeCheckIn(RadioPort& radio,
                         ClockPort& clock,
                         RandomPort& rng,
                         proto::SequenceManager& seq,
                         proto::PacketCodec& codec,
                         proto::ReplayWindow& replay,
                         proto::AuthPort& auth,
                         uint8_t nodeId,
                         uint32_t intervalMs)
    : radio_(radio),
      clock_(clock),
      seq_(seq),
      codec_(codec),
      replay_(replay),
      auth_(auth),
      nodeId_(nodeId),
      intervalMs_(intervalMs) {
    (void)rng;
}

proto::Status NodeCheckIn::poll() {
    const uint32_t now = clock_.millis();
    if (phase_ == Phase::WaitInterval) {
        if (started_ && static_cast<uint32_t>(now - waitStarted_) < intervalMs_) {
            return proto::Status::Ok;
        }
        return beginReport(now);
    }
    if (phase_ == Phase::WaitRetry) {
        if (static_cast<uint32_t>(now - waitStarted_) < retryDelayMs_) {
            return proto::Status::Ok;
        }
        return transmit(now);
    }

    uint8_t buffer[proto::kMaxPhyBytes] = {};
    size_t length = 0;
    const proto::Status read = radio_.readPacket(buffer, sizeof(buffer), &length);
    if (read == proto::Status::Ok && acceptAck(buffer, length) == proto::Status::Ok) {
        phase_ = Phase::WaitInterval;
        started_ = true;
        waitStarted_ = now;
        return proto::Status::Ok;
    }
    if (static_cast<uint32_t>(now - windowStarted_) < kAckWindowMs) {
        return proto::Status::Ok;
    }

    proto::RetryResult result = proto::RetryResult::WaitForInterval;
    uint16_t seq = 0;
    uint32_t delayMs = 0;
    const proto::Status timedOut = seq_.onTimeout(proto::kGatewayId, &result, &seq, &delayMs);
    if (timedOut != proto::Status::Ok) {
        return timedOut;
    }
    if (result == proto::RetryResult::WaitForInterval) {
        phase_ = Phase::WaitInterval;
        started_ = true;
        waitStarted_ = now;
        return proto::Status::Ok;
    }
    seqNum_ = seq;
    phase_ = Phase::WaitRetry;
    retryDelayMs_ = delayMs;
    waitStarted_ = now;
    return proto::Status::Ok;
}

bool NodeCheckIn::inIntervalWait() const {
    return started_ && phase_ == Phase::WaitInterval;
}

proto::Status NodeCheckIn::beginReport(uint32_t now) {
    uint16_t seq = 0;
    const proto::Status started = seq_.beginTransmit(proto::kGatewayId, &seq);
    if (started != proto::Status::Ok) {
        return started;
    }
    seqNum_ = seq;
    counter_ += 1;
    return transmit(now);
}

proto::Status NodeCheckIn::transmit(uint32_t now) {
    proto::PacketHeader header = {};
    header.type = proto::MessageType::Telemetry;
    header.srcId = nodeId_;
    header.dstId = proto::kGatewayId;
    header.seq = seqNum_;
    header.counter = counter_;

    uint8_t packet[proto::kMaxPhyBytes] = {};
    size_t packetLen = 0;
    const bool authOn = auth_.tagLen() != 0;
    const proto::Status encoded = codec_.encode(header, nullptr, 0, authOn, nullptr, packet, sizeof(packet), &packetLen);
    if (encoded != proto::Status::Ok) {
        return encoded;
    }
    const proto::Status signedPacket = auth_.sign(packet, packetLen, nullptr);
    if (signedPacket != proto::Status::Ok) {
        return signedPacket;
    }
    const proto::Status sent = radio_.startTransmit(packet, packetLen);
    if (sent != proto::Status::Ok) {
        return sent;
    }
    const proto::Status listening = radio_.startReceive();
    if (listening != proto::Status::Ok) {
        return listening;
    }
    phase_ = Phase::Receive;
    windowStarted_ = now;
    return proto::Status::Ok;
}

proto::Status NodeCheckIn::acceptAck(const uint8_t* data, size_t len) {
    const bool authOn = auth_.tagLen() != 0;
    proto::DecodedPacket decoded = {};
    const proto::Status parsed = codec_.decode(data, len, nodeId_, authOn, &decoded);
    if (parsed != proto::Status::Ok) {
        return parsed;
    }
    const proto::Status verified = auth_.verify(data, len, decoded.tag);
    if (verified != proto::Status::Ok) {
        return verified;
    }
    if (decoded.type != proto::MessageType::Ack || decoded.srcId != proto::kGatewayId) {
        return proto::Status::BadType;
    }
    bool hasCommand = true;
    proto::AckCommand command = {};
    const proto::Status ack = codec_.readAckPayload(decoded.payload, decoded.payloadLen, &hasCommand, &command);
    if (ack != proto::Status::Ok || hasCommand) {
        return proto::Status::BadPayload;
    }
    return replay_.accept(decoded.srcId, decoded.counter);
}

}  // namespace node
