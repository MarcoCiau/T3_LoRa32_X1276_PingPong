/**
 * @file node_check_in.cpp
 * @brief Telemetry check-in and command policy driven by poll().
 *
 * Retries reuse seq and the replay counter. A new report or a CommandResult
 * advances both. A command id is stored only after the executor runs.
 */

#include "node_check_in.h"

#include "clock_port.h"
#include "command_executor_port.h"
#include "message_types.h"
#include "packet_codec.h"
#include "radio_port.h"
#include "random_port.h"
#include "record_store_port.h"
#include "security.h"
#include "sequence_manager.h"
#include "store_keys.h"

namespace node {
namespace {

constexpr size_t kCommandIdBytes = 4;

/** @brief Store @p value little-endian at @p p. @p p must hold 4 bytes. */
void writeU32(uint8_t* p, uint32_t value) {
    p[0] = static_cast<uint8_t>(value);
    p[1] = static_cast<uint8_t>(value >> 8);
    p[2] = static_cast<uint8_t>(value >> 16);
    p[3] = static_cast<uint8_t>(value >> 24);
}

/** @brief Read a little-endian uint32 from @p p. */
uint32_t readU32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

}  // namespace

NodeCheckIn::NodeCheckIn(RadioPort& radio,
                         ClockPort& clock,
                         RandomPort& rng,
                         proto::SequenceManager& seq,
                         proto::PacketCodec& codec,
                         proto::ReplayWindow& replay,
                         proto::AuthPort& auth,
                         CommandExecutorPort& commands,
                         RecordStorePort& records,
                         uint8_t nodeId,
                         uint32_t intervalMs)
    : radio_(radio),
      clock_(clock),
      seq_(seq),
      codec_(codec),
      replay_(replay),
      auth_(auth),
      commands_(commands),
      records_(records),
      nodeId_(nodeId),
      intervalMs_(intervalMs) {
    (void)rng;
    uint8_t stored[kCommandIdBytes] = {};
    if (records_.read(proto::lastCommandIdKey(nodeId_), stored, kCommandIdBytes)) {
        hasLastCommand_ = true;
        lastCommandId_ = readU32(stored);
    }
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
    if (read == proto::Status::Ok) {
        proto::Status packetStatus = proto::Status::Ok;
        const PacketFate fate = takePacket(buffer, length, now, &packetStatus);
        if (fate == PacketFate::CycleDone) {
            phase_ = Phase::WaitInterval;
            started_ = true;
            waitStarted_ = now;
            return proto::Status::Ok;
        }
        if (fate == PacketFate::ResultSent) {
            return proto::Status::Ok;
        }
        if (fate == PacketFate::Failed) {
            return packetStatus;
        }
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
    outbound_ = Outbound::Telemetry;
    return transmit(now);
}

proto::Status NodeCheckIn::beginResult(uint32_t now, uint32_t commandId, uint8_t status) {
    uint16_t seq = 0;
    const proto::Status started = seq_.beginTransmit(proto::kGatewayId, &seq);
    if (started != proto::Status::Ok) {
        return started;
    }
    seqNum_ = seq;
    counter_ += 1;
    outbound_ = Outbound::CommandResult;
    resultCommandId_ = commandId;
    resultStatus_ = status;
    return transmit(now);
}

proto::Status NodeCheckIn::transmit(uint32_t now) {
    uint8_t payload[proto::kMaxPhyBytes] = {};
    uint8_t payloadLen = 0;
    const uint8_t* payloadPtr = nullptr;
    proto::MessageType type = proto::MessageType::Telemetry;
    const bool authOn = auth_.tagLen() != 0;
    if (outbound_ == Outbound::CommandResult) {
        type = proto::MessageType::CommandResult;
        const proto::Status wrote = codec_.writeCommandResult(resultCommandId_,
                                                               resultStatus_,
                                                               nullptr,
                                                               0,
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
    header.type = type;
    header.srcId = nodeId_;
    header.dstId = proto::kGatewayId;
    header.seq = seqNum_;
    header.counter = counter_;

    uint8_t packet[proto::kMaxPhyBytes] = {};
    size_t packetLen = 0;
    const proto::Status encoded =
        codec_.encode(header, payloadPtr, payloadLen, authOn, nullptr, packet, sizeof(packet), &packetLen);
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

NodeCheckIn::PacketFate NodeCheckIn::takePacket(const uint8_t* data,
                                                size_t len,
                                                uint32_t now,
                                                proto::Status* status) {
    const bool authOn = auth_.tagLen() != 0;
    proto::DecodedPacket decoded = {};
    if (codec_.decode(data, len, nodeId_, authOn, &decoded) != proto::Status::Ok) {
        return PacketFate::Ignore;
    }
    if (auth_.verify(data, len, decoded.tag) != proto::Status::Ok) {
        return PacketFate::Ignore;
    }
    if (decoded.type != proto::MessageType::Ack || decoded.srcId != proto::kGatewayId) {
        return PacketFate::Ignore;
    }
    bool hasCommand = true;
    proto::AckCommand command = {};
    if (codec_.readAckPayload(decoded.payload, decoded.payloadLen, &hasCommand, &command) != proto::Status::Ok) {
        return PacketFate::Ignore;
    }
    if (replay_.accept(decoded.srcId, decoded.counter) != proto::Status::Ok) {
        return PacketFate::Ignore;
    }
    if (!hasCommand) {
        return PacketFate::CycleDone;
    }
    if (outbound_ != Outbound::Telemetry) {
        return PacketFate::Ignore;
    }
    return answerCommand(command, now, status);
}

NodeCheckIn::PacketFate NodeCheckIn::answerCommand(const proto::AckCommand& command,
                                                   uint32_t now,
                                                   proto::Status* status) {
    proto::CommandResultStatus result = proto::CommandResultStatus::UnknownCommand;
    if (hasLastCommand_ && command.commandId == lastCommandId_) {
        result = proto::CommandResultStatus::Duplicate;
    } else if (command.commandType != kNopCommandType) {
        result = proto::CommandResultStatus::UnknownCommand;
    } else {
        result = commands_.execute(command.commandId, command.commandType, command.body, command.commandLen);
        if (!rememberCommand(command.commandId)) {
            if (status != nullptr) {
                *status = proto::Status::BadPayload;
            }
            return PacketFate::Failed;
        }
    }

    const proto::Status sent =
        beginResult(now, command.commandId, static_cast<uint8_t>(result));
    if (sent != proto::Status::Ok) {
        if (status != nullptr) {
            *status = sent;
        }
        return PacketFate::Failed;
    }
    return PacketFate::ResultSent;
}

bool NodeCheckIn::rememberCommand(uint32_t commandId) {
    uint8_t raw[kCommandIdBytes] = {};
    writeU32(raw, commandId);
    if (!records_.commit(proto::lastCommandIdKey(nodeId_), raw, kCommandIdBytes)) {
        return false;
    }
    hasLastCommand_ = true;
    lastCommandId_ = commandId;
    return true;
}

}  // namespace node
