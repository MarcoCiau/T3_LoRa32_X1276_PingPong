/**
 * @file command_queue.h
 * @brief Fixed, persistent queue of commands waiting for a node check-in.
 */

#pragma once

#include "message_types.h"

#include <cstdint>

class RecordStorePort;

namespace gateway {

/** @brief How many commands the gateway keeps, including delivered and finished ones. */
constexpr uint8_t kCommandCapacity = 8;

/** @brief Command body bytes stored in one record. The record store blob is 16 bytes. */
constexpr uint8_t kCommandBodyBytes = 8;

/**
 * @brief Lifetime of one queued command.
 *
 * The numeric values are written to the record store. Do not renumber them.
 */
enum class CommandState : uint8_t {
    Pending = 1,    ///< Waiting for the node's next check-in.
    Delivered = 2,  ///< Placed in an Ack. Waiting for the node's CommandResult.
    Succeeded = 3,  ///< The node reported Ok, or reported that the id was already done.
    Failed = 4,     ///< The node reported failure or an unknown command type.
};

/**
 * @brief One command copied out of the queue.
 *
 * body is owned by this struct. It is not a view into the queue.
 */
struct QueuedCommand {
    uint32_t commandId;                  ///< Id assigned by the queue or supplied by the caller.
    uint8_t nodeId;                      ///< Target node, 1..proto::kMaxNodeId.
    uint8_t commandType;                 ///< Application command type.
    CommandState state;                  ///< Pending, delivered, succeeded, or failed.
    uint8_t bodyLen;                     ///< Bytes used in body. At most kCommandBodyBytes.
    uint8_t body[kCommandBodyBytes];     ///< Command body. Bytes past bodyLen are zero.
};

/**
 * @brief At most eight commands, reloaded from RecordStorePort.
 *
 * nextFor() returns the oldest pending command for one node. markDelivered() and
 * markResult() are the only state changes. A new object constructed on the same
 * store is the reboot path. This class does not transmit and does not allocate.
 */
class CommandQueue {
public:
    /**
     * @brief Load slots and the next command id from @p store.
     * @param store Persistence for the slots and the monotonic id. Must outlive this object.
     */
    explicit CommandQueue(RecordStorePort& store);

    /**
     * @brief Append a command and assign the next monotonic id.
     *
     * The assigned id is stored beside the queue before the slot is written.
     * Ids start at 1.
     *
     * @param nodeId Target node, 1..10.
     * @param commandType Application command type.
     * @param body Command body. May be null when @p bodyLen is 0.
     * @param bodyLen Body length. Greater than kCommandBodyBytes is rejected.
     * @param commandId Receives the assigned id. May be null.
     * @return Status::Ok, BadDestination, BadPayload, or Busy when all eight slots are used.
     */
    proto::Status enqueue(uint8_t nodeId,
                          uint8_t commandType,
                          const uint8_t* body,
                          uint8_t bodyLen,
                          uint32_t* commandId);

    /**
     * @brief Append a command that already has an id from upstream.
     *
     * The stored monotonic id advances past @p commandId so a later enqueue() will not reuse it.
     *
     * @param nodeId Target node, 1..10.
     * @param commandType Application command type.
     * @param body Command body. May be null when @p bodyLen is 0.
     * @param bodyLen Body length. Greater than kCommandBodyBytes is rejected.
     * @param commandId Caller-supplied id. Stored as-is.
     * @return Status::Ok, BadDestination, BadPayload, Duplicate, or Busy.
     */
    proto::Status enqueueWithId(uint8_t nodeId,
                                uint8_t commandType,
                                const uint8_t* body,
                                uint8_t bodyLen,
                                uint32_t commandId);

    /**
     * @brief Copy the oldest pending command for @p nodeId.
     * @param nodeId Target node, 1..10.
     * @param out Filled when a pending command exists. May be null to test for one.
     * @return true when a pending command was found.
     */
    bool nextFor(uint8_t nodeId, QueuedCommand* out) const;

    /**
     * @brief Copy the command with @p commandId, in any state.
     * @param commandId Id to find.
     * @param out Filled when the id is present. May be null.
     * @return true when the id is in the queue.
     */
    bool find(uint32_t commandId, QueuedCommand* out) const;

    /**
     * @brief Move @p commandId from pending to delivered and store that state.
     * @param commandId Id previously returned by nextFor() or enqueue().
     * @return Status::Ok, BadPayload when the id is missing or not pending, or BadPayload when the store rejects the write.
     */
    proto::Status markDelivered(uint32_t commandId);

    /**
     * @brief Move @p commandId from delivered to succeeded or failed.
     *
     * CommandResultStatus::Ok and Duplicate become Succeeded. UnknownCommand and Failed
     * become Failed. The new state is stored before this returns Ok.
     *
     * @param commandId Id that was marked delivered.
     * @param result Status byte from the node's CommandResult.
     * @return Status::Ok, or BadPayload when the id is missing, not delivered, or the store rejects the write.
     */
    proto::Status markResult(uint32_t commandId, proto::CommandResultStatus result);

    /** @brief How many slots are occupied, in any state. */
    uint8_t size() const;

private:
    /** @brief One stored command. used is false for an empty slot. */
    struct Slot {
        bool used = false;
        QueuedCommand command = {};
    };

    /**
     * @brief Insert @p command, assigning an id when @p assignId is true.
     * @param commandId On entry, the caller id when @p assignId is false. On success, the stored id.
     */
    proto::Status insert(uint8_t nodeId,
                         uint8_t commandType,
                         const uint8_t* body,
                         uint8_t bodyLen,
                         bool assignId,
                         uint32_t* commandId);

    /** @brief Write the monotonic id. A failed write leaves the previous blob. */
    bool saveNextId() const;

    /** @brief Write one occupied slot. A failed write leaves the previous blob. */
    bool saveSlot(uint8_t index) const;

    /** @brief Index of @p commandId, or kCommandCapacity when it is absent. */
    uint8_t indexOf(uint32_t commandId) const;

    RecordStorePort& store_;
    Slot slots_[kCommandCapacity] = {};
    uint32_t nextId_ = 1;
};

}  // namespace gateway
