/**
 * @file node_binding.cpp
 * @brief Six-byte node pairing record.
 *
 * Byte 0 is the paired flag, byte 1 is the id, and the next four bytes are the
 * little-endian last command id. hasLastCommand is true when that id is not zero.
 */

#include "node_binding.h"

#include "record_store_port.h"
#include "store_keys.h"

namespace node {
namespace {

constexpr size_t kBytes = 6;

/** @brief Store @p value little-endian at @p p. @p p must hold 4 bytes. */
void writeU32(uint8_t* p, uint32_t value) {
    p[0] = static_cast<uint8_t>(value);
    p[1] = static_cast<uint8_t>(value >> 8);
    p[2] = static_cast<uint8_t>(value >> 16);
    p[3] = static_cast<uint8_t>(value >> 24);
}

/** @brief Read a little-endian uint32 from @p p. */
uint32_t readU32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

}  // namespace

bool loadBinding(RecordStorePort& store, Binding* out) {
    if (out == nullptr) {
        return false;
    }
    *out = {};
    uint8_t raw[kBytes] = {};
    if (!store.read(proto::kNodeBindingKey, raw, kBytes) || raw[0] != 1) {
        return true;
    }
    if (raw[1] < proto::kMinNodeId || raw[1] > proto::kMaxNodeId) {
        return true;
    }
    out->paired = true;
    out->nodeId = raw[1];
    out->lastCommandId = readU32(raw + 2);
    out->hasLastCommand = out->lastCommandId != 0;
    return true;
}

bool saveBinding(RecordStorePort& store, const Binding& binding) {
    uint8_t raw[kBytes] = {};
    raw[0] = binding.paired ? 1 : 0;
    raw[1] = binding.paired ? binding.nodeId : 0;
    writeU32(raw + 2, binding.hasLastCommand ? binding.lastCommandId : 0);
    if (!store.commit(proto::kNodeBindingKey, raw, kBytes)) {
        return false;
    }
    if (binding.nodeId < proto::kMinNodeId || binding.nodeId > proto::kMaxNodeId) {
        return true;
    }
    if (binding.paired && binding.hasLastCommand) {
        return store.commit(proto::lastCommandIdKey(binding.nodeId), raw + 2, 4);
    }
    return store.erase(proto::lastCommandIdKey(binding.nodeId));
}

}  // namespace node
