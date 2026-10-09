/**
 * @file energy_payload.h
 * @brief Fixed 15-byte telemetry body for mock energy readings.
 *
 * This is the application payload inside a Telemetry frame. It does not change
 * the radio header version and it does not read a meter.
 */

#pragma once

#include "message_types.h"

#include <cstddef>
#include <cstdint>

namespace proto {

/** @brief Version byte at offset 0 of the energy body. A different value is rejected. */
constexpr uint8_t kEnergyPayloadVersion = 1;

/** @brief Size of one energy body, version byte included. */
constexpr uint8_t kEnergyPayloadBytes = 15;

static_assert(1 + 2 + 2 + 2 + 4 + 2 + 2 == kEnergyPayloadBytes,
              "energy body is version, voltage, current, power, energy, power factor, frequency");

/**
 * @brief One energy reading carried in a telemetry payload.
 *
 * Multi-byte fields are little-endian on the wire. The version byte is not stored
 * here; encode writes kEnergyPayloadVersion and decode requires that value.
 */
struct EnergyReading {
    uint16_t voltageCentivolts;    ///< Centivolts. 23000 is 230.00 V.
    uint16_t currentMilliamps;     ///< Milliamps.
    uint16_t powerWatts;           ///< Watts.
    uint32_t energyWattHours;      ///< Watt-hours.
    uint16_t powerFactorMilli;     ///< Thousandths. 950 is 0.950.
    uint16_t frequencyCentiHertz;  ///< Centihertz. 5000 is 50.00 Hz.
};

/**
 * @brief Write @p reading as a 15-byte energy body.
 * @param reading Values to store. The version byte is kEnergyPayloadVersion.
 * @param out Destination. Required.
 * @param cap Bytes available at @p out. Less than kEnergyPayloadBytes is rejected.
 * @return Status::Ok, or BadPayload when @p out is null or @p cap is short.
 */
Status encodeEnergy(const EnergyReading& reading, uint8_t* out, size_t cap);

/**
 * @brief Read one energy body.
 * @param data Body bytes. Required.
 * @param len Size of @p data. Anything other than kEnergyPayloadBytes is rejected.
 * @param out Filled on success. Required.
 * @return Status::Ok, or BadPayload when the buffer is the wrong size, the version is not 1, or @p out is null.
 */
Status decodeEnergy(const uint8_t* data, size_t len, EnergyReading* out);

}  // namespace proto
