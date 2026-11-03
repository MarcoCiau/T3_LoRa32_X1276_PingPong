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
#include "button_gesture.h"
#include "clock_port.h"
#include "node_binding.h"
#include "node_check_in.h"
#include "node_pairing.h"
#include "nop_command_executor.h"
#include "packet_codec.h"
#include "radio_service.h"
#include "random_port.h"
#include "security.h"
#include "sequence_manager.h"

#include <esp_mac.h>
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
#if defined(LP_AUTH_REQUIRED)
    proto::AesCcmAuth auth;
#else
    proto::LabAuthOff auth;
#endif
    NopCommandExecutor commands;
    node::NodeCheckIn checkIn;
    ButtonGesture button;
    node::NodePairing pairing;

    /**
     * @brief Bind the check-in and the pairing session.
     * @param store Open NVS record store. Must outlive this object.
     * @param mac Station MAC, proto::kMacBytes.
     * @param binding Stored pairing. An unpaired binding suspends telemetry.
     */
    NodeApp(PreferencesRecordStore& store, const uint8_t* mac, const node::Binding& binding)
        : seq(rng),
          replay(store),
#if defined(LP_AUTH_REQUIRED)
          auth(proto::kDeploymentKey),
#endif
          checkIn(radio,
                  clock,
                  rng,
                  seq,
                  codec,
                  replay,
                  auth,
                  commands,
                  store,
                  binding.paired ? binding.nodeId : kNodeId,
                  kReportIntervalMs),
          button(ButtonRole::Node),
          pairing(radio, clock, rng, codec, auth, store, checkIn, mac) {
        if (!binding.paired) {
            checkIn.suspend(true);
        }
    }
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
    pinMode(board::kUserButtonPin, INPUT_PULLUP);

    Serial.println(F("[node] radio begin"));
    if (!store.open()) {
        halt(F("[node] nvs fail"));
    }
    uint8_t mac[proto::kMacBytes] = {};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    node::Binding binding = {};
    (void)node::loadBinding(store, &binding);
    app = new (appStorage) NodeApp(store, mac, binding);
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
    if (binding.paired) {
        Serial.print(F("[node] check-in node "));
        Serial.print(binding.nodeId);
        Serial.print(F(" every ms "));
        Serial.println(kReportIntervalMs);
    } else {
        Serial.println(F("[node] unpaired"));
    }
}

void loop() {
    const uint32_t now = millis();
    const bool pressed = digitalRead(board::kUserButtonPin) == LOW;
    const Gesture gesture = app->button.poll(pressed, now);
    if (gesture == Gesture::Unpair) {
        app->pairing.beginUnpair();
    } else if (gesture == Gesture::Click && !app->pairing.active()) {
        if (app->pairing.paired()) {
            app->pairing.beginPing();
        } else {
            app->pairing.beginPair();
        }
    }

    if (app->pairing.active()) {
        app->pairing.poll();
    } else {
        app->checkIn.poll();
    }

    switch (app->pairing.takeEvent()) {
    case node::PairEvent::Paired:
        Serial.print(F("[node] paired "));
        Serial.println(app->pairing.nodeId());
        break;
    case node::PairEvent::PairFailed:
        Serial.println(F("[node] pair failed"));
        break;
    case node::PairEvent::PingOk:
        Serial.println(F("[node] ping ok"));
        break;
    case node::PairEvent::PingFail:
        Serial.println(F("[node] ping fail"));
        break;
    case node::PairEvent::Unpaired:
        Serial.println(F("[node] unpaired"));
        break;
    case node::PairEvent::None:
        break;
    }

    const bool led = app->button.hint(now) == HoldHint::Unpair || app->pairing.ledOn(now);
    digitalWrite(board::kBoardLedPin, led ? HIGH : LOW);
}
