/**
 * @file store_keys.h
 * @brief Record-store keys for replay counters, completed command ids, and the gateway queue.
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

/** @brief Next command id the gateway will assign. The blob is a little-endian uint32. */
constexpr uint16_t kNextCommandIdKey = 0x0300;

/** @brief First key for a gateway command slot. Slot indexes 0..7 follow. */
constexpr uint16_t kCommandSlotKeyBase = 0x0310;

/**
 * @brief Storage key for command queue slot @p slot.
 *
 * The blob is 16 bytes: command id, node, type, state, body length, and up to 8 body bytes.
 * @param slot Slot index. Callers use 0..7.
 */
constexpr uint16_t commandSlotKey(uint8_t slot) {
    return static_cast<uint16_t>(kCommandSlotKeyBase + slot);
}

static_assert(kReplayCounterKeyBase + 16 <= kLastCommandIdKeyBase,
              "replay counter keys and command id keys must not overlap");
static_assert(kLastCommandIdKeyBase + 16 <= kNextCommandIdKey,
              "completed command id keys and the next command id key must not overlap");
static_assert(kNextCommandIdKey < kCommandSlotKeyBase,
              "the next command id key and command slots must not overlap");

/** @brief First key for a gateway pairing slot. Node ids 1..10 follow. The blob is 7 bytes. */
constexpr uint16_t kPairSlotKeyBase = 0x0400;

/**
 * @brief Storage key for the pairing slot of @p nodeId.
 * @param nodeId Node id. Callers use 1..proto::kMaxNodeId.
 */
constexpr uint16_t pairSlotKey(uint8_t nodeId) {
    return static_cast<uint16_t>(kPairSlotKeyBase + nodeId);
}

/** @brief Node pairing record. The blob is 6 bytes: flag, id, and last command id. */
constexpr uint16_t kNodeBindingKey = 0x0500;

/**
 * @brief Next outgoing replay counter for this device. The blob is a little-endian uint32.
 *
 * The node stores the telemetry counter here. The gateway stores the Ack counter here.
 * Each board has its own store, so the same key is not shared between them.
 */
constexpr uint16_t kLocalSendCounterKey = 0x0501;

static_assert(kCommandSlotKeyBase + 16 <= kPairSlotKeyBase,
              "command slots and pairing slots must not overlap");
static_assert(kPairSlotKeyBase + 16 <= kNodeBindingKey,
              "pairing slots and the node binding must not overlap");

}  // namespace proto
