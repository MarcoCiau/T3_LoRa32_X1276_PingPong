/**
 * @file node_registry.h
 * @brief Last packet the gateway accepted from each node.
 */

#pragma once

#include "energy_payload.h"
#include "message_types.h"

#include <cstdint>

namespace gateway {

/**
 * @brief Facts recorded for one node after a packet is accepted.
 *
 * seen is false until the first observe(). The registry does not check the
 * replay counter. The caller records a counter only after that check has passed.
 */
struct NodeFacts {
    bool seen;            ///< True after at least one observe() for this id.
    uint32_t counter;     ///< Last accepted replay counter.
    uint16_t seq;         ///< Last accepted telemetry sequence number.
    float rssi;                  ///< RSSI of that packet, in dBm.
    float snr;                   ///< SNR of that packet, in dB.
    uint32_t lastSeenMs;         ///< Clock value supplied by the caller, in milliseconds.
    bool hasEnergy;              ///< True when the last telemetry decoded as energy version 1.
    proto::EnergyReading energy; ///< Valid when hasEnergy is true. Zero when the body was empty or unknown.
};

/**
 * @brief Fixed table of the ten nodes.
 *
 * Index 0 is unused. The table is not persisted. A reboot starts from an empty registry.
 * This class does not transmit and does not allocate.
 */
class NodeRegistry {
public:
    /**
     * @brief Record the latest accepted packet from @p nodeId.
     * @param nodeId Node id, proto::kMinNodeId through proto::kMaxNodeId.
     * @param counter Replay counter the gateway accepted.
     * @param seq Telemetry sequence number the gateway accepted.
     * @param rssi RSSI in dBm.
     * @param snr SNR in dB.
     * @param seenMs Caller clock in milliseconds.
     * @param energy Parsed energy body. Null marks the energy fields absent.
     * @return Status::Ok, or BadDestination when @p nodeId is outside 1..10.
     */
    proto::Status observe(uint8_t nodeId,
                          uint32_t counter,
                          uint16_t seq,
                          float rssi,
                          float snr,
                          uint32_t seenMs,
                          const proto::EnergyReading* energy = nullptr);

    /**
     * @brief Copy the facts for @p nodeId.
     * @param nodeId Node id, 1..10.
     * @param out Filled when this node has been observed. May be null to query presence only.
     * @return false when @p nodeId is outside 1..10 or the node has not been observed.
     */
    bool lookup(uint8_t nodeId, NodeFacts* out) const;

private:
    NodeFacts nodes_[proto::kMaxNodeId + 1] = {};
};

}  // namespace gateway
