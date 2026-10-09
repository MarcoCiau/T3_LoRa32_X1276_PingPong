/**
 * @file telemetry_service.cpp
 * @brief Deterministic energy body. Voltage, power, and energy follow the node id.
 * Current and energy also follow the telemetry sequence. Nothing here reads the ADC.
 */

#include "telemetry_service.h"

namespace node {

proto::Status fillMockEnergy(uint8_t nodeId, uint16_t seq, uint8_t* out, size_t cap, uint8_t* outLen) {
    if (out == nullptr || outLen == nullptr) {
        return proto::Status::BadPayload;
    }
    const uint32_t current = static_cast<uint32_t>(kMockCurrentBaseMilliamps) + seq;
    proto::EnergyReading reading = {};
    reading.voltageCentivolts =
        static_cast<uint16_t>(kMockVoltageBaseCentivolts + static_cast<uint16_t>(nodeId) * kMockVoltageStepCentivolts);
    reading.currentMilliamps = current > 0xFFFFu ? static_cast<uint16_t>(0xFFFFu) : static_cast<uint16_t>(current);
    reading.powerWatts = static_cast<uint16_t>(kMockPowerBaseWatts + nodeId);
    reading.energyWattHours = static_cast<uint32_t>(nodeId) * kMockEnergyPerNode + seq;
    reading.powerFactorMilli = kMockPowerFactorMilli;
    reading.frequencyCentiHertz = kMockFrequencyCentiHertz;

    const proto::Status wrote = proto::encodeEnergy(reading, out, cap);
    if (wrote != proto::Status::Ok) {
        return wrote;
    }
    *outLen = proto::kEnergyPayloadBytes;
    return proto::Status::Ok;
}

}  // namespace node
