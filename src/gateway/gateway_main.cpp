/**
 * @file gateway_main.cpp
 * @brief Gateway bring-up: listen, and answer a node check-in with an Ack.
 *
 * The receive path does not start Wi-Fi or MQTT. The Ack transmit is the radio
 * operation that follows a valid packet.
 */

#include <Arduino.h>

#include "board_profile.h"
#include "clock_port.h"
#include "command_queue.h"
#include "node_registry.h"
#include "outbound_queue.h"
#include "packet_codec.h"
#include "radio_service.h"
#include "random_port.h"
#include "security.h"
#include "sequence_manager.h"

#include "../node/radio_service.h"

#include <esp_system.h>

#include <cstddef>
#include <new>

#include <preferences_record_store.h>

namespace {

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
 * @brief Objects the check-in coordinator needs. Built after NVS is open.
 */
struct GatewayApp {
    RadioLibRadio radio;
    ArduinoClock clock;
    ArduinoRandom rng;
    proto::PacketCodec codec;
    proto::SequenceManager seq;
    proto::ReplayWindow replay;
    proto::LabAuthOff auth;
    gateway::NodeRegistry registry;
    gateway::CommandQueue commands;
    gateway::OutboundQueue uplink;
    gateway::RadioService coordinator;

    /**
     * @brief Bind the coordinator to @p store.
     * @param store Open NVS record store. Must outlive this object.
     */
    explicit GatewayApp(PreferencesRecordStore& store)
        : seq(rng),
          replay(store),
          commands(store),
          coordinator(radio, clock, codec, seq, replay, auth, registry, commands, uplink) {}
};

PreferencesRecordStore store;
alignas(GatewayApp) unsigned char appStorage[sizeof(GatewayApp)];
GatewayApp* app = nullptr;

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

    Serial.println(F("[gateway] radio begin"));
    if (!store.open()) {
        halt(F("[gateway] nvs fail"));
    }
    app = new (appStorage) GatewayApp(store);
    if (app->radio.begin() != proto::Status::Ok) {
        halt(F("[gateway] radio fail"));
    }

    Serial.print(F("[gateway] freq MHz "));
    Serial.println(board::kRadioFrequencyMhz);
    Serial.print(F("[gateway] bw kHz "));
    Serial.print(board::kRadioBandwidthKhz);
    Serial.print(F(" sf "));
    Serial.print(board::kRadioSpreadingFactor);
    Serial.print(F(" cr 4/"));
    Serial.print(board::kRadioCodingRate);
    Serial.print(F(" sync 0x"));
    Serial.print(board::kRadioSyncWord, HEX);
    Serial.print(F(" power dBm "));
    Serial.println(board::kRadioOutputPowerDbm);

    if (app->coordinator.poll() != proto::Status::Ok) {
        halt(F("[gateway] listen fail"));
    }
    Serial.println(F("[gateway] listening"));
}

void loop() {
    app->radio.poll();
    if (app->radio.takeTransmitDone()) {
        digitalWrite(board::kBoardLedPin, HIGH);
        Serial.println(F("[gateway] ack sent"));
    }

    const proto::Status stepped = app->coordinator.poll();
    if (stepped == proto::Status::Busy || stepped == proto::Status::Ok) {
        gateway::NodeFacts facts = {};
        static bool heard = false;
        static uint16_t lastSeq = 0;
        static uint32_t lastCounter = 0;
        if (app->registry.lookup(1, &facts) && (!heard || facts.seq != lastSeq || facts.counter != lastCounter)) {
            heard = true;
            lastSeq = facts.seq;
            lastCounter = facts.counter;
            Serial.print(F("[gateway] telemetry node 1 seq "));
            Serial.print(facts.seq);
            Serial.print(F(" counter "));
            Serial.print(facts.counter);
            Serial.print(F(" rssi "));
            Serial.print(facts.rssi);
            Serial.print(F(" snr "));
            Serial.println(facts.snr);
        }
        return;
    }
    Serial.println(F("[gateway] poll fail"));
}
