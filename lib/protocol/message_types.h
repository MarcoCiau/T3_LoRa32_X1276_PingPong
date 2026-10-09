#pragma once

#include <cstdint>

namespace proto {

constexpr uint8_t kVersion = 1;

enum class MessageType : uint8_t {
    Telemetry = 1,
    Ack = 2,
    CommandResult = 3,
    Heartbeat = 4,
};

constexpr uint8_t kGatewayId = 0;
constexpr uint8_t kMinNodeId = 1;
constexpr uint8_t kMaxNodeId = 10;

constexpr uint8_t kMaxPhyBytes = 64;
constexpr uint8_t kHeaderBytes = 11;
constexpr uint8_t kAuthTagBytes = 8;

static_assert(1 + 1 + 1 + 1 + 2 + 4 + 1 == kHeaderBytes,
              "header is version, type, src_id, dst_id, seq, counter, payload_len");

enum class Status : int8_t {
    Ok = 0,
    Truncated = -1,
    BadVersion = -2,
    BadType = -3,
    BadDestination = -4,
    BadAuth = -5,
    Replay = -6,
    Duplicate = -7,
    BadPayload = -8,
    Busy = -9,
    Timeout = -10,
};

enum class CommandResultStatus : uint8_t {
    Ok = 0,
    UnknownCommand = 1,
    Duplicate = 2,
    Failed = 3,
};

}  // namespace proto
