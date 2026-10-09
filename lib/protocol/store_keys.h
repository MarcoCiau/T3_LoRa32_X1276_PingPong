/**
 * @file store_keys.h
 * @brief Record-store keys for the replay counter and the last completed command.
 */

#pragma once

#include <cstdint>

namespace proto {

/** @brief First key for a sender's last accepted replay counter. Ids 0..10 follow. */
constexpr uint16_t kReplayCounterKeyBase = 0x0100;

/** @brief First key for a node's last completed command id. Ids 0..10 follow. */
constexpr uint16_t kLastCommandIdKeyBase = 0x0200;

/**
 * @brief Storage key for the replay counter of @p srcId.
 * @param srcId Sender id. Callers use 0..proto::kMaxNodeId.
 */
constexpr uint16_t replayCounterKey(uint8_t srcId) {
    return static_cast<uint16_t>(kReplayCounterKeyBase + srcId);
}

/**
 * @brief Storage key for the last completed command id of @p nodeId.
 *
 * The blob is a little-endian uint32. Id 0 is reserved so the table lines up with replay keys.
 * @param nodeId Node id. Callers use 1..proto::kMaxNodeId.
 */
constexpr uint16_t lastCommandIdKey(uint8_t nodeId) {
    return static_cast<uint16_t>(kLastCommandIdKeyBase + nodeId);
}

static_assert(kReplayCounterKeyBase + 16 <= kLastCommandIdKeyBase,
              "replay counter keys and command id keys must not overlap");

}  // namespace proto
