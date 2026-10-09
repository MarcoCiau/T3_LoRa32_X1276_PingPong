/**
 * @file test_energy_payload.cpp
 * @brief The 15-byte energy body round-trips, and a short or bad-version body is rejected.
 */

#include <unity.h>

#include <energy_payload.h>

namespace {

void setStatus(proto::Status actual, proto::Status expected) {
    TEST_ASSERT_EQUAL_INT8(static_cast<int8_t>(expected), static_cast<int8_t>(actual));
}

proto::EnergyReading sample() {
    proto::EnergyReading reading = {};
    reading.voltageCentivolts = 23010;
    reading.currentMilliamps = 1001;
    reading.powerWatts = 101;
    reading.energyWattHours = 1001;
    reading.powerFactorMilli = 950;
    reading.frequencyCentiHertz = 5000;
    return reading;
}

}  // namespace

void setUp(void) {}

void tearDown(void) {}

void test_energy_body_round_trips(void) {
    const proto::EnergyReading reading = sample();
    uint8_t body[proto::kEnergyPayloadBytes] = {};
    setStatus(proto::encodeEnergy(reading, body, sizeof(body)), proto::Status::Ok);
    TEST_ASSERT_EQUAL_UINT8(proto::kEnergyPayloadVersion, body[0]);
    TEST_ASSERT_EQUAL_UINT8(0xE2, body[1]);
    TEST_ASSERT_EQUAL_UINT8(0x59, body[2]);

    proto::EnergyReading decoded = {};
    setStatus(proto::decodeEnergy(body, sizeof(body), &decoded), proto::Status::Ok);
    TEST_ASSERT_EQUAL_UINT16(reading.voltageCentivolts, decoded.voltageCentivolts);
    TEST_ASSERT_EQUAL_UINT16(reading.currentMilliamps, decoded.currentMilliamps);
    TEST_ASSERT_EQUAL_UINT16(reading.powerWatts, decoded.powerWatts);
    TEST_ASSERT_EQUAL_UINT32(reading.energyWattHours, decoded.energyWattHours);
    TEST_ASSERT_EQUAL_UINT16(reading.powerFactorMilli, decoded.powerFactorMilli);
    TEST_ASSERT_EQUAL_UINT16(reading.frequencyCentiHertz, decoded.frequencyCentiHertz);
}

void test_short_buffer_and_bad_version_are_rejected(void) {
    const proto::EnergyReading reading = sample();
    uint8_t shortBody[proto::kEnergyPayloadBytes - 1] = {};
    setStatus(proto::encodeEnergy(reading, shortBody, sizeof(shortBody)), proto::Status::BadPayload);
    setStatus(proto::decodeEnergy(shortBody, sizeof(shortBody), nullptr), proto::Status::BadPayload);

    uint8_t body[proto::kEnergyPayloadBytes] = {};
    setStatus(proto::encodeEnergy(reading, body, sizeof(body)), proto::Status::Ok);
    proto::EnergyReading decoded = {};
    setStatus(proto::decodeEnergy(body, sizeof(body) - 1, &decoded), proto::Status::BadPayload);

    body[0] = 2;
    setStatus(proto::decodeEnergy(body, sizeof(body), &decoded), proto::Status::BadPayload);
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_energy_body_round_trips);
    RUN_TEST(test_short_buffer_and_bad_version_are_rejected);
    return UNITY_END();
}
