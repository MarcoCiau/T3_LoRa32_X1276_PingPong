/**
 * @file radio_service.cpp
 * @brief Check-in handler. An Ack is the only transmit, and it happens before poll() returns.
 *
 * A duplicate sequence is detected before the replay window, because a retry reuses
 * the counter. The replay window still rejects a different sequence with an old counter.
 */

#include "radio_service.h"

#include "clock_port.h"
#include "energy_payload.h"
#include "node_registry.h"
#include "outbound_buffer.h"
#include "packet_codec.h"
#include "radio_port.h"
#include "security.h"
#include "sequence_manager.h"

namespace gateway {
namespace {

/**
 * @brief Encode one frame and append the auth tag when @p auth requires one.
 *
 * The tag covers the header and payload. LabAuthOff adds no bytes.
 * verify() of a received frame still happens before the replay counter is stored.
 */
proto::Status seal(proto::PacketCodec& codec,
                   proto::AuthPort& auth,
                   const proto::PacketHeader& header,
                   const uint8_t* payload,
                   uint8_t payloadLen,
                   uint8_t* packet,
                   size_t cap,
                   size_t* packetLen) {
    const bool authOn = auth.tagLen() != 0;
    uint8_t tag[proto::kAuthTagBytes];
    const uint8_t* tagPtr = nullptr;
    if (authOn) {
        uint8_t frame[proto::kMaxPhyBytes];
        size_t frameLen = 0;
        const proto::Status bare =
            codec.encode(header, payload, payloadLen, false, nullptr, frame, sizeof(frame), &frameLen);
        if (bare != proto::Status::Ok) {
            return bare;
        }
        const proto::Status signedFrame = auth.sign(frame, frameLen, tag);
        if (signedFrame != proto::Status::Ok) {
            return signedFrame;
        }
        tagPtr = tag;
    }
    return codec.encode(header, payload, payloadLen, authOn, tagPtr, packet, cap, packetLen);
}

}  // namespace

RadioService::RadioService(RadioPort& radio,
                           ClockPort& clock,
                           proto::PacketCodec& codec,
                           proto::SequenceManager& seq,
                           proto::ReplayWindow& replay,
                           proto::AuthPort& auth,
                           NodeRegistry& registry,
                           CommandQueue& commands,
                           OutboundBuffer& uplink)
    : radio_(radio),
      clock_(clock),
      codec_(codec),
      seq_(seq),
      replay_(replay),
      auth_(auth),
      registry_(registry),
      commands_(commands),
      uplink_(uplink) {}

proto::Status RadioService::poll() {
    const proto::Status listening = radio_.startReceive();
    if (listening != proto::Status::Ok) {
        return listening;
    }

    uint8_t buffer[proto::kMaxPhyBytes] = {};
    size_t length = 0;
    const proto::Status read = radio_.readPacket(buffer, sizeof(buffer), &length);
    if (read == proto::Status::Busy) {
        return proto::Status::Ok;
    }
    if (read != proto::Status::Ok) {
        return proto::Status::Ok;
    }
    return handle(buffer, length, radio_.rssi(), radio_.snr());
}

proto::Status RadioService::handle(const uint8_t* data, size_t len, float rssi, float snr) {
    const bool authOn = auth_.tagLen() != 0;
    proto::DecodedPacket decoded = {};
    if (codec_.decode(data, len, proto::kGatewayId, authOn, &decoded) != proto::Status::Ok) {
        return proto::Status::Ok;
    }
    if (authOn && len < auth_.tagLen()) {
        return proto::Status::Ok;
    }
    const size_t frameLen = authOn ? len - auth_.tagLen() : len;
    if (auth_.verify(data, frameLen, decoded.tag) != proto::Status::Ok) {
        return proto::Status::Ok;
    }
    if (decoded.srcId < proto::kMinNodeId || decoded.srcId > proto::kMaxNodeId) {
        return proto::Status::Ok;
    }
    if (decoded.type != proto::MessageType::Telemetry && decoded.type != proto::MessageType::CommandResult) {
        return proto::Status::Ok;
    }

    const proto::Status seqStatus = seq_.accept(decoded.srcId, decoded.seq);
    if (seqStatus == proto::Status::Duplicate) {
        const LastAck& previous = lastAck_[decoded.srcId];
        const bool repeatCommand = decoded.type == proto::MessageType::Telemetry && previous.valid &&
                                    previous.seq == decoded.seq && previous.hasCommand;
        return sendAck(decoded.srcId, repeatCommand, repeatCommand ? &previous.command : nullptr);
    }
    if (seqStatus != proto::Status::Ok) {
        return proto::Status::Ok;
    }
    if (replay_.accept(decoded.srcId, decoded.counter) != proto::Status::Ok) {
        return proto::Status::Ok;
    }

    if (decoded.type == proto::MessageType::Telemetry) {
        return acceptTelemetry(decoded, rssi, snr);
    }
    return acceptResult(decoded);
}

proto::Status RadioService::acceptTelemetry(const proto::DecodedPacket& packet, float rssi, float snr) {
    if (codec_.checkTelemetry(packet.payload, packet.payloadLen, auth_.tagLen() != 0) != proto::Status::Ok) {
        return proto::Status::Ok;
    }

    proto::EnergyReading reading = {};
    const bool hasEnergy = proto::decodeEnergy(packet.payload, packet.payloadLen, &reading) == proto::Status::Ok;
    registry_.observe(packet.srcId,
                      packet.counter,
                      packet.seq,
                      rssi,
                      snr,
                      clock_.millis(),
                      hasEnergy ? &reading : nullptr);

    UplinkRecord uplink = {};
    uplink.kind = UplinkKind::Telemetry;
    uplink.nodeId = packet.srcId;
    uplink.seq = packet.seq;
    uplink.counter = packet.counter;
    uplink.rssi = rssi;
    uplink.snr = snr;
    uplink.hasEnergy = hasEnergy;
    if (hasEnergy) {
        uplink.energy = reading;
    }
    (void)uplink_.push(uplink);

    QueuedCommand command = {};
    const bool hasCommand = commands_.nextFor(packet.srcId, &command);
    if (hasCommand && commands_.markDelivered(command.commandId) != proto::Status::Ok) {
        lastAck_[packet.srcId].valid = true;
        lastAck_[packet.srcId].seq = packet.seq;
        lastAck_[packet.srcId].hasCommand = false;
        return sendAck(packet.srcId, false, nullptr);
    }

    LastAck& saved = lastAck_[packet.srcId];
    saved.valid = true;
    saved.seq = packet.seq;
    saved.hasCommand = hasCommand;
    if (hasCommand) {
        saved.command = command;
    }
    return sendAck(packet.srcId, hasCommand, hasCommand ? &saved.command : nullptr);
}

proto::Status RadioService::acceptResult(const proto::DecodedPacket& packet) {
    proto::CommandResultBody body = {};
    if (codec_.readCommandResult(packet.payload, packet.payloadLen, &body) != proto::Status::Ok) {
        return proto::Status::Ok;
    }
    const proto::CommandResultStatus result = static_cast<proto::CommandResultStatus>(body.status);
    if (commands_.markResult(body.commandId, result) != proto::Status::Ok) {
        return proto::Status::Ok;
    }

    UplinkRecord uplink = {};
    uplink.kind = UplinkKind::CommandResult;
    uplink.nodeId = packet.srcId;
    uplink.commandId = body.commandId;
    uplink.resultStatus = body.status;
    (void)uplink_.push(uplink);

    LastAck& saved = lastAck_[packet.srcId];
    saved.valid = true;
    saved.seq = packet.seq;
    saved.hasCommand = false;
    return sendAck(packet.srcId, false, nullptr);
}

proto::Status RadioService::sendAck(uint8_t nodeId, bool hasCommand, const QueuedCommand* command) {
    uint16_t seq = 0;
    const proto::Status started = seq_.beginTransmit(nodeId, &seq);
    if (started != proto::Status::Ok) {
        return started;
    }

    uint8_t payload[proto::kMaxPhyBytes] = {};
    uint8_t payloadLen = 0;
    const uint8_t* payloadPtr = nullptr;
    const bool authOn = auth_.tagLen() != 0;
    if (hasCommand) {
        if (command == nullptr) {
            return proto::Status::BadPayload;
        }
        const proto::Status wrote = codec_.writeAckCommand(command->commandId,
                                                            command->commandType,
                                                            command->body,
                                                            command->bodyLen,
                                                            authOn,
                                                            payload,
                                                            sizeof(payload),
                                                            &payloadLen);
        if (wrote != proto::Status::Ok) {
            return wrote;
        }
        payloadPtr = payload;
    }

    proto::PacketHeader header = {};
    header.type = proto::MessageType::Ack;
    header.srcId = proto::kGatewayId;
    header.dstId = nodeId;
    header.seq = seq;
    header.counter = txCounter_;

    uint8_t packet[proto::kMaxPhyBytes] = {};
    size_t packetLen = 0;
    const proto::Status encoded =
        seal(codec_, auth_, header, payloadPtr, payloadLen, packet, sizeof(packet), &packetLen);
    if (encoded != proto::Status::Ok) {
        return encoded;
    }
    const proto::Status sent = radio_.startTransmit(packet, packetLen);
    if (sent != proto::Status::Ok) {
        return sent;
    }
    txCounter_ += 1;
    return proto::Status::Ok;
}

}  // namespace gateway
