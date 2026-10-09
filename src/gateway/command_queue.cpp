/**
 * @file command_queue.cpp
 * @brief Load and update the eight command slots in the record store.
 *
 * The next command id is committed before a new slot. A failed commit leaves
 * the previous blob, and the in-memory slot is rolled back to match.
 */

#include "command_queue.h"

#include "record_store_port.h"
#include "store_keys.h"

namespace gateway {
namespace {

constexpr size_t kSlotBytes = 16;
constexpr size_t kIdBytes = 4;

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

/** @brief Pack @p command into a 16-byte blob. */
void encodeSlot(const QueuedCommand& command, uint8_t out[kSlotBytes]) {
    for (size_t i = 0; i < kSlotBytes; ++i) {
        out[i] = 0;
    }
    writeU32(out, command.commandId);
    out[4] = command.nodeId;
    out[5] = command.commandType;
    out[6] = static_cast<uint8_t>(command.state);
    out[7] = command.bodyLen;
    for (uint8_t i = 0; i < command.bodyLen && i < kCommandBodyBytes; ++i) {
        out[8 + i] = command.body[i];
    }
}

/**
 * @brief Unpack a slot blob.
 * @return false when the state or the body length is not one this queue writes.
 */
bool decodeSlot(const uint8_t raw[kSlotBytes], QueuedCommand* command) {
    const uint8_t state = raw[6];
    const uint8_t bodyLen = raw[7];
    if (state < static_cast<uint8_t>(CommandState::Pending) ||
        state > static_cast<uint8_t>(CommandState::Failed) || bodyLen > kCommandBodyBytes) {
        return false;
    }
    command->commandId = readU32(raw);
    command->nodeId = raw[4];
    command->commandType = raw[5];
    command->state = static_cast<CommandState>(state);
    command->bodyLen = bodyLen;
    for (uint8_t i = 0; i < kCommandBodyBytes; ++i) {
        command->body[i] = 0;
    }
    for (uint8_t i = 0; i < bodyLen; ++i) {
        command->body[i] = raw[8 + i];
    }
    return true;
}

/** @brief True when @p status closes the command as succeeded. */
bool succeeded(proto::CommandResultStatus status) {
    return status == proto::CommandResultStatus::Ok || status == proto::CommandResultStatus::Duplicate;
}

}  // namespace

CommandQueue::CommandQueue(RecordStorePort& store) : store_(store) {
    uint8_t idRaw[kIdBytes] = {};
    if (store_.read(proto::kNextCommandIdKey, idRaw, kIdBytes)) {
        const uint32_t stored = readU32(idRaw);
        if (stored != 0) {
            nextId_ = stored;
        }
    }

    uint32_t highest = 0;
    bool sawId = false;
    for (uint8_t index = 0; index < kCommandCapacity; ++index) {
        uint8_t raw[kSlotBytes] = {};
        if (!store_.read(proto::commandSlotKey(index), raw, kSlotBytes)) {
            continue;
        }
        QueuedCommand command = {};
        if (!decodeSlot(raw, &command)) {
            continue;
        }
        slots_[index].used = true;
        slots_[index].command = command;
        if (!sawId || command.commandId > highest) {
            highest = command.commandId;
            sawId = true;
        }
    }
    if (sawId && (highest >= nextId_ || nextId_ == 0)) {
        nextId_ = highest + 1;
        if (nextId_ == 0) {
            nextId_ = 1;
        }
    }
}

proto::Status CommandQueue::enqueue(uint8_t nodeId,
                                    uint8_t commandType,
                                    const uint8_t* body,
                                    uint8_t bodyLen,
                                    uint32_t* commandId) {
    uint32_t assigned = 0;
    const proto::Status status = insert(nodeId, commandType, body, bodyLen, true, &assigned);
    if (status == proto::Status::Ok && commandId != nullptr) {
        *commandId = assigned;
    }
    return status;
}

proto::Status CommandQueue::enqueueWithId(uint8_t nodeId,
                                          uint8_t commandType,
                                          const uint8_t* body,
                                          uint8_t bodyLen,
                                          uint32_t commandId) {
    return insert(nodeId, commandType, body, bodyLen, false, &commandId);
}

bool CommandQueue::nextFor(uint8_t nodeId, QueuedCommand* out) const {
    if (nodeId < proto::kMinNodeId || nodeId > proto::kMaxNodeId) {
        return false;
    }
    for (const Slot& slot : slots_) {
        if (!slot.used || slot.command.nodeId != nodeId || slot.command.state != CommandState::Pending) {
            continue;
        }
        if (out != nullptr) {
            *out = slot.command;
        }
        return true;
    }
    return false;
}

bool CommandQueue::find(uint32_t commandId, QueuedCommand* out) const {
    const uint8_t index = indexOf(commandId);
    if (index >= kCommandCapacity) {
        return false;
    }
    if (out != nullptr) {
        *out = slots_[index].command;
    }
    return true;
}

proto::Status CommandQueue::markDelivered(uint32_t commandId) {
    const uint8_t index = indexOf(commandId);
    if (index >= kCommandCapacity || slots_[index].command.state != CommandState::Pending) {
        return proto::Status::BadPayload;
    }
    const Slot previous = slots_[index];
    slots_[index].command.state = CommandState::Delivered;
    if (!saveSlot(index)) {
        slots_[index] = previous;
        return proto::Status::BadPayload;
    }
    return proto::Status::Ok;
}

proto::Status CommandQueue::markResult(uint32_t commandId, proto::CommandResultStatus result) {
    const uint8_t index = indexOf(commandId);
    if (index >= kCommandCapacity || slots_[index].command.state != CommandState::Delivered) {
        return proto::Status::BadPayload;
    }
    const Slot previous = slots_[index];
    slots_[index].command.state = succeeded(result) ? CommandState::Succeeded : CommandState::Failed;
    if (!saveSlot(index)) {
        slots_[index] = previous;
        return proto::Status::BadPayload;
    }
    return proto::Status::Ok;
}

uint8_t CommandQueue::size() const {
    uint8_t count = 0;
    for (const Slot& slot : slots_) {
        if (slot.used) {
            count = static_cast<uint8_t>(count + 1);
        }
    }
    return count;
}

proto::Status CommandQueue::insert(uint8_t nodeId,
                                   uint8_t commandType,
                                   const uint8_t* body,
                                   uint8_t bodyLen,
                                   bool assignId,
                                   uint32_t* commandId) {
    if (nodeId < proto::kMinNodeId || nodeId > proto::kMaxNodeId) {
        return proto::Status::BadDestination;
    }
    if (bodyLen > kCommandBodyBytes || (bodyLen > 0 && body == nullptr) || (!assignId && commandId == nullptr)) {
        return proto::Status::BadPayload;
    }

    uint8_t freeIndex = kCommandCapacity;
    for (uint8_t index = 0; index < kCommandCapacity; ++index) {
        if (!slots_[index].used) {
            freeIndex = index;
            break;
        }
    }
    if (freeIndex >= kCommandCapacity) {
        return proto::Status::Busy;
    }

    uint32_t id = assignId ? nextId_ : *commandId;
    if (!assignId && indexOf(id) < kCommandCapacity) {
        return proto::Status::Duplicate;
    }
    if (assignId && id == 0) {
        id = 1;
    }

    const uint32_t previousNext = nextId_;
    if (id >= nextId_) {
        nextId_ = id + 1;
        if (nextId_ == 0) {
            nextId_ = 1;
        }
    }
    if (!saveNextId()) {
        nextId_ = previousNext;
        return proto::Status::BadPayload;
    }

    Slot slot;
    slot.used = true;
    slot.command.commandId = id;
    slot.command.nodeId = nodeId;
    slot.command.commandType = commandType;
    slot.command.state = CommandState::Pending;
    slot.command.bodyLen = bodyLen;
    for (uint8_t i = 0; i < kCommandBodyBytes; ++i) {
        slot.command.body[i] = 0;
    }
    for (uint8_t i = 0; i < bodyLen; ++i) {
        slot.command.body[i] = body[i];
    }
    slots_[freeIndex] = slot;
    if (!saveSlot(freeIndex)) {
        slots_[freeIndex].used = false;
        return proto::Status::BadPayload;
    }
    if (commandId != nullptr) {
        *commandId = id;
    }
    return proto::Status::Ok;
}

bool CommandQueue::saveNextId() const {
    uint8_t raw[kIdBytes] = {};
    writeU32(raw, nextId_);
    return store_.commit(proto::kNextCommandIdKey, raw, kIdBytes);
}

bool CommandQueue::saveSlot(uint8_t index) const {
    uint8_t raw[kSlotBytes] = {};
    encodeSlot(slots_[index].command, raw);
    return store_.commit(proto::commandSlotKey(index), raw, kSlotBytes);
}

uint8_t CommandQueue::indexOf(uint32_t commandId) const {
    for (uint8_t index = 0; index < kCommandCapacity; ++index) {
        if (slots_[index].used && slots_[index].command.commandId == commandId) {
            return index;
        }
    }
    return kCommandCapacity;
}

}  // namespace gateway
