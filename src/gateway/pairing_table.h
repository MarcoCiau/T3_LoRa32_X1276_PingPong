/**
 * @file pairing_table.h
 * @brief Gateway record of which MAC owns each node id.
 *
 * A slot is free when its assigned flag is clear. The same MAC keeps its id.
 * This class does not transmit.
 */

#pragma once

#include "command_queue.h"
#include "message_types.h"

#include <cstdint>

class RecordStorePort;

namespace proto {
class ReplayWindow;
}

namespace gateway {

/**
 * @brief Ten pairing slots, loaded from the record store.
 *
 * admit() assigns the lowest free id, or returns the id a MAC already owns.
 * The pairing window is RAM only. A reboot starts with the window closed.
 */
class PairingTable {
public:
    /** @brief How long a click keeps the gateway willing to answer, in milliseconds. */
    static constexpr uint32_t kWindowMs = 60000;
    /** @brief Pairing slot blob: assigned flag plus proto::kMacBytes. */
    static constexpr uint8_t kSlotBytes = 1 + proto::kMacBytes;

    /**
     * @brief Load slots from @p store.
     * @param store Must outlive this object.
     */
    explicit PairingTable(RecordStorePort& store);

    /**
     * @brief Open the pairing window.
     * @param now Millisecond clock.
     */
    void openWindow(uint32_t now);

    /** @brief Close the pairing window. A later PairRequest is silence. */
    void closeWindow();

    /**
     * @brief True while the window is open and younger than kWindowMs.
     * @param now Millisecond clock.
     */
    bool windowOpen(uint32_t now) const;

    /**
     * @brief True when @p nodeId has an assigned MAC.
     * @param nodeId Node id, 1..10.
     */
    bool paired(uint8_t nodeId) const;

    /**
     * @brief Copy the MAC stored for @p nodeId.
     * @param nodeId Node id, 1..10.
     * @param mac Receives proto::kMacBytes. Required.
     * @return false when the id is free or @p mac is null.
     */
    bool macFor(uint8_t nodeId, uint8_t* mac) const;

    /**
     * @brief Assign @p mac, or return the id it already has.
     *
     * The slot is stored before this returns Ok. @p lastCommandId is the stored
     * completed command id, or 0 when that record is absent.
     *
     * @param mac proto::kMacBytes. Required.
     * @param nodeId Receives the assigned id.
     * @param lastCommandId Receives the stored command id. May be null.
     * @return Status::Ok, Busy when every id is taken, or BadPayload.
     */
    proto::Status admit(const uint8_t* mac, uint8_t* nodeId, uint32_t* lastCommandId);

    /**
     * @brief Free @p nodeId, its replay counter, its last command id, and its commands.
     * @param nodeId Node id, 1..10.
     * @param replay Replay counters. The id's counter is forgotten.
     * @param commands Queue. Commands for @p nodeId are dropped.
     * @return Status::Ok, or BadDestination when the id is outside 1..10.
     */
    proto::Status release(uint8_t nodeId, proto::ReplayWindow& replay, CommandQueue& commands);

    /**
     * @brief Clear every slot, replay counter, last command id, and the command queue.
     * @param replay Replay counters for ids 0..10.
     * @param commands Command queue.
     */
    void wipe(proto::ReplayWindow& replay, CommandQueue& commands);

    /**
     * @brief Take the id assigned by the latest successful admit().
     * @return The id, or 0 when none is waiting. A second call returns 0.
     */
    uint8_t takeAdmitted();

private:
    /** @brief One stored binding. assigned is false when the id is free. */
    struct Slot {
        bool assigned = false;
        uint8_t mac[proto::kMacBytes] = {};
    };

    /** @brief Write @p index. A failed commit leaves the previous blob and the RAM slot. */
    bool save(uint8_t nodeId);

    /** @brief Read the little-endian last command id, or 0 when the key is absent. */
    uint32_t storedCommand(uint8_t nodeId) const;

    RecordStorePort& store_;
    Slot slots_[proto::kMaxNodeId + 1] = {};
    bool window_ = false;
    uint32_t windowAt_ = 0;
    uint8_t admitted_ = 0;
};

}  // namespace gateway
