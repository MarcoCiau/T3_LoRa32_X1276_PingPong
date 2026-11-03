/**
 * @file message_types.h
 * @brief Draft radio-protocol constants shared by the node and the gateway.
 */

#pragma once

#include <cstdint>

/**
 * @brief Wire types for the star-network protocol.
 *
 * Multi-byte integers on the air are little-endian. This namespace holds the
 * draft identifiers only; it does not encode packets.
 */
namespace proto {

/** @brief Protocol version spoken by this build. Receivers drop any other value. */
constexpr uint8_t kVersion = 1;

/**
 * @brief Packet type byte.
 *
 * There is no standalone command datagram. A command rides inside an Ack.
 */
enum class MessageType : uint8_t {
    Telemetry = 1,      ///< Node report. Payload is an opaque application span.
    Ack = 2,            ///< Packet accepted. Payload is empty or one command.
    CommandResult = 3,  ///< Node report of what a command did. Not an Ack.
    Heartbeat = 4,      ///< Optional liveness packet. No body builder in the prototype.
    PairRequest = 5,    ///< Unpaired node, button window only. Payload is the 6-byte MAC.
    PairAccept = 6,     ///< Gateway assigns an id. Payload is id, MAC, and last command id.
    PairReject = 7,     ///< Gateway pairing window is open and every id is taken.
    Unpair = 8,         ///< Paired node asks the gateway to free its id.
    Ping = 9,           ///< Paired node range check. The Ack is empty.
};

/** @brief Gateway device id. */
constexpr uint8_t kGatewayId = 0;
/** @brief Lowest node id. */
constexpr uint8_t kMinNodeId = 1;
/** @brief Highest node id in this version. */
constexpr uint8_t kMaxNodeId = 10;
/** @brief Sender or destination that has no node id yet. Used only by pairing frames. */
constexpr uint8_t kUnassignedId = 0xFF;
/** @brief Wi-Fi station MAC length carried in pairing and unpair payloads. */
constexpr uint8_t kMacBytes = 6;
/** @brief PairReject reason: every id from 1 to 10 is already paired. */
constexpr uint8_t kPairRejectFull = 1;

/**
 * @brief True for the three pairing frames that omit the auth tag.
 * @param type Frame type.
 * @return true for PairRequest, PairAccept, and PairReject.
 */
constexpr bool omitsAuthTag(MessageType type) {
    return type == MessageType::PairRequest || type == MessageType::PairAccept ||
           type == MessageType::PairReject;
}

/** @brief Maximum PHY payload, header and auth tag included. */
constexpr uint8_t kMaxPhyBytes = 64;
/** @brief Header size with no auth tag: version through payload_len. */
constexpr uint8_t kHeaderBytes = 11;
/** @brief Auth tag size when auth is on. The tag is omitted when auth is off. */
constexpr uint8_t kAuthTagBytes = 8;

static_assert(1 + 1 + 1 + 1 + 2 + 4 + 1 == kHeaderBytes,
              "header is version, type, src_id, dst_id, seq, counter, payload_len");

/**
 * @brief Codec and validation result.
 *
 * Ok is 0. Failures are negative so they stay distinct from CommandResultStatus.
 */
enum class Status : int8_t {
    Ok = 0,               ///< Parsed or accepted.
    Truncated = -1,       ///< Buffer shorter than the header or the declared payload.
    BadVersion = -2,      ///< version is not kVersion.
    BadType = -3,         ///< type is not a MessageType value.
    BadDestination = -4,  ///< dst_id is not this device, or the peer id is outside 0..10.
    BadAuth = -5,         ///< Tag missing where the frame requires one. The tag is not checked yet.
    Replay = -6,          ///< Replay counter is not strictly newer. Reserved for a later task.
    Duplicate = -7,       ///< This seq was already accepted for the same peer.
    BadPayload = -8,      ///< Length or range check failed.
    Busy = -9,            ///< Radio is in the other half-duplex state. Reserved for a later task.
    Timeout = -10,        ///< Receive window ended with no valid packet. Reserved for a later task.
};

/**
 * @brief Status byte inside a CommandResult payload.
 */
enum class CommandResultStatus : uint8_t {
    Ok = 0,               ///< Command ran.
    UnknownCommand = 1,   ///< command_type is not implemented.
    Duplicate = 2,        ///< command_id was already completed.
    Failed = 3,           ///< Command was attempted and did not succeed.
};

}  // namespace proto
