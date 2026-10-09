/**
 * @file energy_payload.cpp
 * @brief Little-endian layout for the 15-byte energy telemetry body.
 *
 * A short buffer and a version other than kEnergyPayloadVersion are BadPayload.
 * This file does not include Arduino, Wi-Fi, or MQTT.
 */

#include "energy_payload.h"

namespace proto {
namespace {

constexpr size_t kVersionOffset = 0;
constexpr size_t kVoltageOffset = 1;
constexpr size_t kCurrentOffset = 3;
constexpr size_t kPowerOffset = 5;
constexpr size_t kEnergyOffset = 7;
constexpr size_t kPowerFactorOffset = 11;
constexpr size_t kFrequencyOffset = 13;

/** @brief Store @p value little-endian at @p p. @p p must hold 2 bytes. */
void writeU16(uint8_t* p, uint16_t value) {
    p[0] = static_cast<uint8_t>(value);
    p[1] = static_cast<uint8_t>(value >> 8);
}

/** @brief Store @p value little-endian at @p p. @p p must hold 4 bytes. */
void writeU32(uint8_t* p, uint32_t value) {
    p[0] = static_cast<uint8_t>(value);
    p[1] = static_cast<uint8_t>(value >> 8);
    p[2] = static_cast<uint8_t>(value >> 16);
    p[3] = static_cast<uint8_t>(value >> 24);
}

/** @brief Read a little-endian uint16 from @p p. */
uint16_t readU16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (static_cast<uint16_t>(p[1]) << 8));
}

/** @brief Read a little-endian uint32 from @p p. */
uint32_t readU32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

}  // namespace

Status encodeEnergy(const EnergyReading& reading, uint8_t* out, size_t cap) {
    if (out == nullptr || cap < kEnergyPayloadBytes) {
        return Status::BadPayload;
    }
    out[kVersionOffset] = kEnergyPayloadVersion;
    writeU16(out + kVoltageOffset, reading.voltageCentivolts);
    writeU16(out + kCurrentOffset, reading.currentMilliamps);
    writeU16(out + kPowerOffset, reading.powerWatts);
    writeU32(out + kEnergyOffset, reading.energyWattHours);
    writeU16(out + kPowerFactorOffset, reading.powerFactorMilli);
    writeU16(out + kFrequencyOffset, reading.frequencyCentiHertz);
    return Status::Ok;
}

Status decodeEnergy(const uint8_t* data, size_t len, EnergyReading* out) {
    if (data == nullptr || out == nullptr || len != kEnergyPayloadBytes) {
        return Status::BadPayload;
    }
    if (data[kVersionOffset] != kEnergyPayloadVersion) {
        return Status::BadPayload;
    }
    out->voltageCentivolts = readU16(data + kVoltageOffset);
    out->currentMilliamps = readU16(data + kCurrentOffset);
    out->powerWatts = readU16(data + kPowerOffset);
    out->energyWattHours = readU32(data + kEnergyOffset);
    out->powerFactorMilli = readU16(data + kPowerFactorOffset);
    out->frequencyCentiHertz = readU16(data + kFrequencyOffset);
    return Status::Ok;
}

}  // namespace proto
