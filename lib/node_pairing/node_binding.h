/**
 * @file node_binding.h
 * @brief The node's paired flag, assigned id, and last completed command id.
 */

#pragma once

#include "message_types.h"

#include <cstdint>

class RecordStorePort;

namespace node {

/**
 * @brief What the node keeps across reboot.
 *
 * paired is false until a PairAccept has been stored. The MAC is not stored.
 */
struct Binding {
    bool paired = false;          ///< True after a stored PairAccept.
    uint8_t nodeId = 0;           ///< Assigned id. Meaningful when paired is true.
    uint32_t lastCommandId = 0;   ///< Last completed command. Zero when none.
    bool hasLastCommand = false;  ///< True when lastCommandId was stored.
};

/**
 * @brief Read the binding. A missing record is unpaired.
 * @param store Record store.
 * @param out Filled on success. Required.
 * @return false when @p out is null.
 */
bool loadBinding(RecordStorePort& store, Binding* out);

/**
 * @brief Store @p binding. The last command key is written only when hasLastCommand is set.
 * @param store Record store.
 * @param binding Record to write.
 * @return false when the store rejects the write.
 */
bool saveBinding(RecordStorePort& store, const Binding& binding);

}  // namespace node
