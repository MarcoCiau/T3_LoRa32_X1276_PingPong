/**
 * @file security.cpp
 * @brief LabAuthOff and the persistent replay-counter check.
 *
 * The counter is committed before it counts as accepted. A failed commit
 * leaves the previous value, so a replay is not treated as new.
 */

#include "security.h"

#include "record_store_port.h"

namespace proto {
namespace {

constexpr size_t kCounterBytes = 4;

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

uint8_t LabAuthOff::tagLen() const {
    return 0;
}

Status LabAuthOff::sign(const uint8_t* /*frame*/, size_t /*frameLen*/, uint8_t* /*tag*/) const {
    return Status::Ok;
}

Status LabAuthOff::verify(const uint8_t* /*frame*/, size_t /*frameLen*/, const uint8_t* /*tag*/) const {
    return Status::Ok;
}

ReplayWindow::ReplayWindow(RecordStorePort& store) : store_(store) {}

uint16_t ReplayWindow::replayKey(uint8_t srcId) {
    return static_cast<uint16_t>(kReplayKeyBase + srcId);
}

Status ReplayWindow::accept(uint8_t srcId, uint32_t counter) {
    if (srcId > kMaxNodeId) {
        return Status::BadDestination;
    }

    uint8_t stored[kCounterBytes] = {};
    const uint16_t key = replayKey(srcId);
    if (store_.read(key, stored, kCounterBytes) && counter <= readU32(stored)) {
        return Status::Replay;
    }

    uint8_t bytes[kCounterBytes] = {};
    writeU32(bytes, counter);
    if (!store_.commit(key, bytes, kCounterBytes)) {
        return Status::BadPayload;
    }
    return Status::Ok;
}

}  // namespace proto
