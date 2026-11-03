/**
 * @file packet_codec.cpp
 * @brief Little-endian frame layout for PacketCodec.
 *
 * Auth tag bytes are copied. They are not verified.
 */

#include "packet_codec.h"

#include <cstring>

namespace proto {
namespace {

/** @brief Ack command prefix: command_id, command_type, command_len. */
constexpr uint8_t kCommandPrefixBytes = 6;
/** @brief CommandResult prefix: command_id and status. */
constexpr uint8_t kResultPrefixBytes = 5;

/** @brief Store @p value little-endian at @p p. @p p must hold 2 bytes. */
void writeU16(uint8_t* p, uint16_t value) {
    p[0] = static_cast<uint8_t>(value);
    p[1] = static_cast<uint8_t>(value >> 8);
}

/** @brief Store @p value little-endian at @p p. @p p must hold 4 bytes. */
void writeU32(uint8_t* p, uint32_t value) {
    p[0] = static_cast<uint8_t>(value);
    p[1] = static_cast<uint8_t>(value >> 8);
    p[2] = static_cast<uint8_t>(value >> 16);
    p[3] = static_cast<uint8_t>(value >> 24);
}

/** @brief Read a little-endian uint16 from @p p. */
uint16_t readU16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (static_cast<uint16_t>(p[1]) << 8));
}

/** @brief Read a little-endian uint32 from @p p. */
uint32_t readU32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

/** @brief True when @p type is one of the MessageType values. */
bool knownType(uint8_t type) {
    return type == static_cast<uint8_t>(MessageType::Telemetry) ||
           type == static_cast<uint8_t>(MessageType::Ack) ||
           type == static_cast<uint8_t>(MessageType::CommandResult) ||
           type == static_cast<uint8_t>(MessageType::Heartbeat) ||
           type == static_cast<uint8_t>(MessageType::PairRequest) ||
           type == static_cast<uint8_t>(MessageType::PairAccept) ||
           type == static_cast<uint8_t>(MessageType::PairReject) ||
           type == static_cast<uint8_t>(MessageType::Unpair) ||
           type == static_cast<uint8_t>(MessageType::Ping);
}

/**
 * @brief Maximum payload bytes that still fit in proto::kMaxPhyBytes.
 * @param authOn Reserves proto::kAuthTagBytes when true.
 */
uint8_t payloadBudget(bool authOn) {
    const uint8_t tag = authOn ? kAuthTagBytes : 0;
    return static_cast<uint8_t>(kMaxPhyBytes - kHeaderBytes - tag);
}

}  // namespace

Status PacketCodec::encode(const PacketHeader& header,
                           const uint8_t* payload,
                           uint8_t payloadLen,
                           bool authOn,
                           const uint8_t* tag,
                           uint8_t* out,
                           size_t outCap,
                           size_t* outLen) const {
    if (out == nullptr || outLen == nullptr) {
        return Status::BadPayload;
    }
    if (payloadLen > 0 && payload == nullptr) {
        return Status::BadPayload;
    }
    if (authOn && tag == nullptr) {
        return Status::BadAuth;
    }
    const uint8_t type = static_cast<uint8_t>(header.type);
    if (!knownType(type)) {
        return Status::BadType;
    }

    const size_t tagLen = authOn ? kAuthTagBytes : 0;
    const size_t total = static_cast<size_t>(kHeaderBytes) + payloadLen + tagLen;
    if (total > kMaxPhyBytes) {
        return Status::BadPayload;
    }
    if (outCap < total) {
        return Status::Truncated;
    }

    out[0] = kVersion;
    out[1] = type;
    out[2] = header.srcId;
    out[3] = header.dstId;
    writeU16(out + 4, header.seq);
    writeU32(out + 6, header.counter);
    out[10] = payloadLen;
    if (payloadLen > 0) {
        std::memcpy(out + kHeaderBytes, payload, payloadLen);
    }
    if (authOn) {
        std::memcpy(out + kHeaderBytes + payloadLen, tag, kAuthTagBytes);
    }
    *outLen = total;
    return Status::Ok;
}

Status PacketCodec::decode(const uint8_t* buf,
                           size_t len,
                           uint8_t localId,
                           bool authOn,
                           DecodedPacket* out) const {
    if (buf == nullptr || out == nullptr) {
        return Status::BadPayload;
    }
    if (len < kHeaderBytes) {
        return Status::Truncated;
    }

    const uint8_t payloadLen = buf[10];
    const size_t tagLen = authOn ? kAuthTagBytes : 0;
    const size_t need = static_cast<size_t>(kHeaderBytes) + payloadLen + tagLen;
    if (len < need) {
        return Status::Truncated;
    }
    if (len != need || need > kMaxPhyBytes) {
        return Status::BadPayload;
    }
    if (buf[0] != kVersion) {
        return Status::BadVersion;
    }
    if (!knownType(buf[1])) {
        return Status::BadType;
    }
    if (buf[3] != localId) {
        return Status::BadDestination;
    }

    out->type = static_cast<MessageType>(buf[1]);
    out->srcId = buf[2];
    out->dstId = buf[3];
    out->seq = readU16(buf + 4);
    out->counter = readU32(buf + 6);
    out->payloadLen = payloadLen;
    out->payload = payloadLen == 0 ? nullptr : buf + kHeaderBytes;
    out->tag = authOn ? buf + kHeaderBytes + payloadLen : nullptr;
    return Status::Ok;
}

Status PacketCodec::writeAckCommand(uint32_t commandId,
                                    uint8_t commandType,
                                    const uint8_t* body,
                                    uint8_t commandLen,
                                    bool authOn,
                                    uint8_t* out,
                                    size_t outCap,
                                    uint8_t* outLen) const {
    if (out == nullptr || outLen == nullptr) {
        return Status::BadPayload;
    }
    if (commandLen > 0 && body == nullptr) {
        return Status::BadPayload;
    }
    const size_t total = static_cast<size_t>(kCommandPrefixBytes) + commandLen;
    if (total > payloadBudget(authOn)) {
        return Status::BadPayload;
    }
    if (outCap < total) {
        return Status::Truncated;
    }

    writeU32(out, commandId);
    out[4] = commandType;
    out[5] = commandLen;
    if (commandLen > 0) {
        std::memcpy(out + kCommandPrefixBytes, body, commandLen);
    }
    *outLen = static_cast<uint8_t>(total);
    return Status::Ok;
}

Status PacketCodec::readAckPayload(const uint8_t* payload,
                                   uint8_t payloadLen,
                                   bool* hasCommand,
                                   AckCommand* command) const {
    if (hasCommand == nullptr) {
        return Status::BadPayload;
    }
    if (payloadLen == 0) {
        *hasCommand = false;
        return Status::Ok;
    }
    if (payload == nullptr || command == nullptr) {
        return Status::BadPayload;
    }
    if (payloadLen < kCommandPrefixBytes) {
        return Status::BadPayload;
    }
    const uint8_t commandLen = payload[5];
    if (static_cast<size_t>(kCommandPrefixBytes) + commandLen != payloadLen) {
        return Status::BadPayload;
    }

    command->commandId = readU32(payload);
    command->commandType = payload[4];
    command->commandLen = commandLen;
    command->body = commandLen == 0 ? nullptr : payload + kCommandPrefixBytes;
    *hasCommand = true;
    return Status::Ok;
}

Status PacketCodec::writeCommandResult(uint32_t commandId,
                                       uint8_t status,
                                       const uint8_t* detail,
                                       uint8_t detailLen,
                                       bool authOn,
                                       uint8_t* out,
                                       size_t outCap,
                                       uint8_t* outLen) const {
    if (out == nullptr || outLen == nullptr) {
        return Status::BadPayload;
    }
    if (detailLen > 0 && detail == nullptr) {
        return Status::BadPayload;
    }
    const size_t total = static_cast<size_t>(kResultPrefixBytes) + detailLen;
    if (total > payloadBudget(authOn)) {
        return Status::BadPayload;
    }
    if (outCap < total) {
        return Status::Truncated;
    }

    writeU32(out, commandId);
    out[4] = status;
    if (detailLen > 0) {
        std::memcpy(out + kResultPrefixBytes, detail, detailLen);
    }
    *outLen = static_cast<uint8_t>(total);
    return Status::Ok;
}

Status PacketCodec::readCommandResult(const uint8_t* payload,
                                      uint8_t payloadLen,
                                      CommandResultBody* out) const {
    if (out == nullptr || payload == nullptr) {
        return Status::BadPayload;
    }
    if (payloadLen < kResultPrefixBytes) {
        return Status::BadPayload;
    }

    out->commandId = readU32(payload);
    out->status = payload[4];
    out->detailLen = static_cast<uint8_t>(payloadLen - kResultPrefixBytes);
    out->detail = out->detailLen == 0 ? nullptr : payload + kResultPrefixBytes;
    return Status::Ok;
}

Status PacketCodec::writePairAccept(uint8_t nodeId,
                                   const uint8_t* mac,
                                   uint32_t lastCommandId,
                                   uint8_t* out,
                                   size_t outCap,
                                   uint8_t* outLen) const {
    if (out == nullptr || outLen == nullptr || mac == nullptr) {
        return Status::BadPayload;
    }
    constexpr uint8_t kBytes = 1 + kMacBytes + 4;
    if (outCap < kBytes) {
        return Status::Truncated;
    }
    out[0] = nodeId;
    std::memcpy(out + 1, mac, kMacBytes);
    writeU32(out + 1 + kMacBytes, lastCommandId);
    *outLen = kBytes;
    return Status::Ok;
}

Status PacketCodec::readPairAccept(const uint8_t* payload,
                                  uint8_t payloadLen,
                                  uint8_t* nodeId,
                                  uint8_t* mac,
                                  uint32_t* lastCommandId) const {
    constexpr uint8_t kBytes = 1 + kMacBytes + 4;
    if (payload == nullptr || nodeId == nullptr || mac == nullptr || lastCommandId == nullptr ||
        payloadLen != kBytes) {
        return Status::BadPayload;
    }
    *nodeId = payload[0];
    std::memcpy(mac, payload + 1, kMacBytes);
    *lastCommandId = readU32(payload + 1 + kMacBytes);
    return Status::Ok;
}

Status PacketCodec::writePairReject(uint8_t reason, uint8_t* out, size_t outCap, uint8_t* outLen) const {
    if (out == nullptr || outLen == nullptr) {
        return Status::BadPayload;
    }
    if (outCap < 1) {
        return Status::Truncated;
    }
    out[0] = reason;
    *outLen = 1;
    return Status::Ok;
}

Status PacketCodec::readPairReject(const uint8_t* payload, uint8_t payloadLen, uint8_t* reason) const {
    if (payload == nullptr || reason == nullptr || payloadLen != 1) {
        return Status::BadPayload;
    }
    *reason = payload[0];
    return Status::Ok;
}

Status PacketCodec::checkTelemetry(const uint8_t* data, uint8_t len, bool authOn) const {
    if (len > payloadBudget(authOn)) {
        return Status::BadPayload;
    }
    if (len > 0 && data == nullptr) {
        return Status::BadPayload;
    }
    return Status::Ok;
}

}  // namespace proto
