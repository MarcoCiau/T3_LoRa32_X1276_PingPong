/**
 * @file pairing_table.cpp
 * @brief Load and update the ten MAC-to-id slots.
 *
 * admit() writes the slot before it returns. A failed write leaves the previous slot.
 */

#include "pairing_table.h"

#include "record_store_port.h"
#include "security.h"
#include "store_keys.h"

#include <cstring>

namespace gateway {
namespace {

/** @brief Read a little-endian uint32 from @p p. */
uint32_t readU32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

/** @brief True when @p a and @p b are the same MAC. Both are proto::kMacBytes. */
bool sameMac(const uint8_t* a, const uint8_t* b) {
    return std::memcmp(a, b, proto::kMacBytes) == 0;
}

}  // namespace

PairingTable::PairingTable(RecordStorePort& store) : store_(store) {
    for (uint8_t id = proto::kMinNodeId; id <= proto::kMaxNodeId; ++id) {
        uint8_t raw[kSlotBytes] = {};
        if (!store_.read(proto::pairSlotKey(id), raw, kSlotBytes) || raw[0] != 1) {
            continue;
        }
        slots_[id].assigned = true;
        std::memcpy(slots_[id].mac, raw + 1, proto::kMacBytes);
    }
}

void PairingTable::openWindow(uint32_t now) {
    window_ = true;
    windowAt_ = now;
}

void PairingTable::closeWindow() {
    window_ = false;
}

bool PairingTable::windowOpen(uint32_t now) const {
    return window_ && static_cast<uint32_t>(now - windowAt_) < kWindowMs;
}

bool PairingTable::paired(uint8_t nodeId) const {
    if (nodeId < proto::kMinNodeId || nodeId > proto::kMaxNodeId) {
        return false;
    }
    return slots_[nodeId].assigned;
}

bool PairingTable::macFor(uint8_t nodeId, uint8_t* mac) const {
    if (mac == nullptr || !paired(nodeId)) {
        return false;
    }
    std::memcpy(mac, slots_[nodeId].mac, proto::kMacBytes);
    return true;
}

proto::Status PairingTable::admit(const uint8_t* mac, uint8_t* nodeId, uint32_t* lastCommandId) {
    if (mac == nullptr || nodeId == nullptr) {
        return proto::Status::BadPayload;
    }
    for (uint8_t id = proto::kMinNodeId; id <= proto::kMaxNodeId; ++id) {
        if (slots_[id].assigned && sameMac(slots_[id].mac, mac)) {
            *nodeId = id;
            if (lastCommandId != nullptr) {
                *lastCommandId = storedCommand(id);
            }
            admitted_ = id;
            return proto::Status::Ok;
        }
    }

    uint8_t freeId = 0;
    for (uint8_t id = proto::kMinNodeId; id <= proto::kMaxNodeId; ++id) {
        if (!slots_[id].assigned) {
            freeId = id;
            break;
        }
    }
    if (freeId == 0) {
        return proto::Status::Busy;
    }

    Slot previous = slots_[freeId];
    slots_[freeId].assigned = true;
    std::memcpy(slots_[freeId].mac, mac, proto::kMacBytes);
    if (!save(freeId)) {
        slots_[freeId] = previous;
        return proto::Status::BadPayload;
    }
    *nodeId = freeId;
    if (lastCommandId != nullptr) {
        *lastCommandId = storedCommand(freeId);
    }
    admitted_ = freeId;
    return proto::Status::Ok;
}

proto::Status PairingTable::release(uint8_t nodeId, proto::ReplayWindow& replay, CommandQueue& commands) {
    if (nodeId < proto::kMinNodeId || nodeId > proto::kMaxNodeId) {
        return proto::Status::BadDestination;
    }
    Slot previous = slots_[nodeId];
    slots_[nodeId].assigned = false;
    for (uint8_t i = 0; i < proto::kMacBytes; ++i) {
        slots_[nodeId].mac[i] = 0;
    }
    if (!save(nodeId)) {
        slots_[nodeId] = previous;
        return proto::Status::BadPayload;
    }
    (void)store_.erase(proto::lastCommandIdKey(nodeId));
    (void)replay.forget(nodeId);
    commands.dropFor(nodeId);
    return proto::Status::Ok;
}

void PairingTable::wipe(proto::ReplayWindow& replay, CommandQueue& commands) {
    closeWindow();
    admitted_ = 0;
    for (uint8_t id = proto::kMinNodeId; id <= proto::kMaxNodeId; ++id) {
        slots_[id].assigned = false;
        for (uint8_t i = 0; i < proto::kMacBytes; ++i) {
            slots_[id].mac[i] = 0;
        }
        (void)save(id);
        (void)store_.erase(proto::lastCommandIdKey(id));
        (void)replay.forget(id);
    }
    (void)replay.forget(proto::kGatewayId);
    commands.clear();
}

uint8_t PairingTable::takeAdmitted() {
    const uint8_t id = admitted_;
    admitted_ = 0;
    return id;
}

bool PairingTable::save(uint8_t nodeId) {
    uint8_t raw[kSlotBytes] = {};
    raw[0] = slots_[nodeId].assigned ? 1 : 0;
    std::memcpy(raw + 1, slots_[nodeId].mac, proto::kMacBytes);
    return store_.commit(proto::pairSlotKey(nodeId), raw, kSlotBytes);
}

uint32_t PairingTable::storedCommand(uint8_t nodeId) const {
    uint8_t raw[4] = {};
    if (!store_.read(proto::lastCommandIdKey(nodeId), raw, sizeof(raw))) {
        return 0;
    }
    return readU32(raw);
}

}  // namespace gateway
