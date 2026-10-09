/**
 * @file main.cpp
 * @brief Node composition root. setup() builds the ports. loop() only polls the check-in.
 *
 * NVS is opened before NodeCheckIn is constructed, because the replay window and the
 * last command id are read in those constructors. The ping-pong sketch is a different
 * build and is not this file.
 */

#include <Arduino.h>

#include "board_profile.h"
#include "clock_port.h"
#include "node_check_in.h"
#include "nop_command_executor.h"
#include "packet_codec.h"
#include "radio_service.h"
#include "random_port.h"
#include "security.h"
#include "sequence_manager.h"

#include <esp_system.h>

#include <cstddef>
#include <new>

#include <preferences_record_store.h>

namespace {

/** @brief This node's id. The lab gateway watches node 1. */
constexpr uint8_t kNodeId = 1;

/**
 * @brief Time between reports, in milliseconds.
 *
 * This is the short end of the 1–5 minute range. A cycle that ends in an Ack,
 * and a cycle that spends the retry budget, both wait this long.
 */
constexpr uint32_t kReportIntervalMs = 60000;

/**
 * @brief ClockPort backed by the Arduino millisecond clock.
 */
class ArduinoClock final : public ClockPort {
public:
    /** @brief Milliseconds since boot. The value may wrap. */
    uint32_t millis() const override { return ::millis(); }
};

/**
 * @brief RandomPort backed by the ESP32 hardware generator.
 */
class ArduinoRandom final : public RandomPort {
public:
    /**
     * @brief Return one value in the closed range [@p lo, @p hi].
     * @param lo Inclusive lower bound.
     * @param hi Inclusive upper bound.
     */
    uint32_t uniform(uint32_t lo, uint32_t hi) override {
        if (hi <= lo) {
            return lo;
        }
        const uint32_t span = hi - lo + 1;
        return lo + (esp_random() % span);
    }
};

/**
 * @brief Objects one node check-in needs. Built after NVS is open.
 */
struct NodeApp {
    RadioLibRadio radio;
    ArduinoClock clock;
    ArduinoRandom rng;
    proto::PacketCodec codec;
    proto::SequenceManager seq;
    proto::ReplayWindow replay;
    proto::LabAuthOff auth;
    NopCommandExecutor commands;
    node::NodeCheckIn checkIn;

    /**
     * @brief Bind the check-in to @p store.
     * @param store Open NVS record store. Must outlive this object.
     */
    explicit NodeApp(PreferencesRecordStore& store)
        : seq(rng),
          replay(store),
          checkIn(radio, clock, rng, seq, codec, replay, auth, commands, store, kNodeId, kReportIntervalMs) {}
};

PreferencesRecordStore store;
alignas(NodeApp) unsigned char appStorage[sizeof(NodeApp)];
NodeApp* app = nullptr;

/** @brief Stop the board after printing @p message. */
void halt(const __FlashStringHelper* message) {
    Serial.println(message);
    while (true) {
        delay(1000);
    }
}

}  // namespace

void setup() {
    Serial.begin(115200);
    pinMode(board::kBoardLedPin, OUTPUT);
    digitalWrite(board::kBoardLedPin, LOW);

    Serial.println(F("[node] radio begin"));
    if (!store.open()) {
        halt(F("[node] nvs fail"));
    }
    app = new (appStorage) NodeApp(store);
    if (app->radio.begin() != proto::Status::Ok) {
        halt(F("[node] radio fail"));
    }

    Serial.print(F("[node] freq MHz "));
    Serial.println(board::kRadioFrequencyMhz);
    Serial.print(F("[node] bw kHz "));
    Serial.print(board::kRadioBandwidthKhz);
    Serial.print(F(" sf "));
    Serial.print(board::kRadioSpreadingFactor);
    Serial.print(F(" cr 4/"));
    Serial.print(board::kRadioCodingRate);
    Serial.print(F(" sync 0x"));
    Serial.print(board::kRadioSyncWord, HEX);
    Serial.print(F(" power dBm "));
    Serial.println(board::kRadioOutputPowerDbm);
    Serial.print(F("[node] check-in node "));
    Serial.print(kNodeId);
    Serial.print(F(" every ms "));
    Serial.println(kReportIntervalMs);
}

void loop() {
    app->checkIn.poll();
}
