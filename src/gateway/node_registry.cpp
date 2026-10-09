/**
 * @file node_registry.cpp
 * @brief In-memory last-seen table for node ids 1..10.
 */

#include "node_registry.h"

namespace gateway {

proto::Status NodeRegistry::observe(uint8_t nodeId,
                                    uint32_t counter,
                                    uint16_t seq,
                                    float rssi,
                                    float snr,
                                    uint32_t seenMs,
                                    const proto::EnergyReading* energy) {
    if (nodeId < proto::kMinNodeId || nodeId > proto::kMaxNodeId) {
        return proto::Status::BadDestination;
    }
    NodeFacts& row = nodes_[nodeId];
    row.seen = true;
    row.counter = counter;
    row.seq = seq;
    row.rssi = rssi;
    row.snr = snr;
    row.lastSeenMs = seenMs;
    row.hasEnergy = energy != nullptr;
    row.energy = energy != nullptr ? *energy : proto::EnergyReading{};
    return proto::Status::Ok;
}

bool NodeRegistry::lookup(uint8_t nodeId, NodeFacts* out) const {
    if (nodeId < proto::kMinNodeId || nodeId > proto::kMaxNodeId || !nodes_[nodeId].seen) {
        return false;
    }
    if (out != nullptr) {
        *out = nodes_[nodeId];
    }
    return true;
}

}  // namespace gateway
