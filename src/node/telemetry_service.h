/**
 * @file telemetry_service.h
 * @brief Mock energy body for a node check-in. No meter is read.
 */

#pragma once

#include "energy_payload.h"

#include <cstddef>
#include <cstdint>

namespace node {

/** @brief Centivolts added once for every node. 23000 is 230.00 V before the id step. */
constexpr uint16_t kMockVoltageBaseCentivolts = 23000;

/** @brief Extra centivolts per node id. */
constexpr uint16_t kMockVoltageStepCentivolts = 10;

/** @brief Milliamps before the sequence is added. */
constexpr uint16_t kMockCurrentBaseMilliamps = 1000;

/** @brief Watts before the node id is added. */
constexpr uint16_t kMockPowerBaseWatts = 100;

/** @brief Watt-hours contributed by the node id. The sequence is added after this scale. */
constexpr uint32_t kMockEnergyPerNode = 1000;

/** @brief Power factor in thousandths. 950 is 0.950. */
constexpr uint16_t kMockPowerFactorMilli = 950;

/** @brief Frequency in centihertz. 5000 is 50.00 Hz. */
constexpr uint16_t kMockFrequencyCentiHertz = 5000;

/**
 * @brief Write the mock energy body for @p nodeId and @p seq.
 *
 * The same pair always produces the same 15 bytes, so a retry of one report
 * matches the first transmission. Current saturates at the uint16 maximum.
 *
 * @param nodeId Sender id. The formula does not reject an id outside 1..10.
 * @param seq Telemetry sequence used for current and energy.
 * @param out Destination. Required.
 * @param cap Bytes available at @p out.
 * @param outLen Receives kEnergyPayloadBytes on success. Required.
 * @return Status::Ok, or BadPayload when a pointer is missing or @p cap is short.
 */
proto::Status fillMockEnergy(uint8_t nodeId, uint16_t seq, uint8_t* out, size_t cap, uint8_t* outLen);

}  // namespace node
