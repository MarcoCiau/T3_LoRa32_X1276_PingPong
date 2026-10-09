/**
 * @file main.cpp
 * @brief Gateway composition root. setup() builds the ports. loop() polls in order.
 *
 * Each loop calls the radio coordinator, then one WifiLink::poll, then one
 * MqttService::poll, then prints any new telemetry for nodes 1 through 10.
 * Empty Wi-Fi and MQTT placeholders leave both down. The ping-pong sketch is a
 * different build and is not this file.
 */

#include <Arduino.h>

#include "board_profile.h"
#include "clock_port.h"
#include "command_queue.h"
#include "mqtt_service.h"
#include "node_registry.h"
#include "outbound_queue.h"
#include "packet_codec.h"
#include "radio_service.h"
#include "random_port.h"
#include "security.h"
#include "sequence_manager.h"
#include "wifi_manager.h"

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
 * @brief Print one accepted telemetry line. Energy fields appear only when the body was version 1.
 * @param nodeId Sender, 1..10.
 * @param facts Last packet stored for that node.
 */
void printTelemetry(uint8_t nodeId, const gateway::NodeFacts& facts) {
    Serial.print(F("[gateway] telemetry node "));
    Serial.print(nodeId);
    Serial.print(F(" seq "));
    Serial.print(facts.seq);
    Serial.print(F(" counter "));
    Serial.print(facts.counter);
    Serial.print(F(" rssi "));
    Serial.print(facts.rssi);
    Serial.print(F(" snr "));
    Serial.print(facts.snr);
    if (facts.hasEnergy) {
        Serial.print(F(" voltage "));
        Serial.print(facts.energy.voltageCentivolts / 100.0f, 2);
        Serial.print(F(" current "));
        Serial.print(facts.energy.currentMilliamps / 1000.0f, 3);
        Serial.print(F(" power "));
        Serial.print(facts.energy.powerWatts);
        Serial.print(F(" watt-hours "));
        Serial.print(facts.energy.energyWattHours);
        Serial.print(F(" power-factor "));
        Serial.print(facts.energy.powerFactorMilli / 1000.0f, 3);
        Serial.print(F(" frequency "));
        Serial.print(facts.energy.frequencyCentiHertz / 100.0f, 2);
    }
    Serial.println();
}

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
#if defined(LP_AUTH_REQUIRED)
    proto::AesCcmAuth auth;
#else
    proto::LabAuthOff auth;
#endif
    gateway::NodeRegistry registry;
    gateway::CommandQueue commands;
    gateway::OutboundQueue uplink;
    gateway::RadioService coordinator;
    gateway::EspWifiStation station;
    gateway::WifiLink wifi;
    gateway::EspMqttClient mqttClient;
    gateway::MqttService mqtt;

    /**
     * @brief Bind the coordinator to @p store.
     *
     * Wi-Fi is constructed with the lab credentials. An empty SSID leaves the station down.
     * MQTT uses the ThingsBoard CE placeholders. An empty host or token leaves it idle.
     * @param store Open NVS record store. Must outlive this object.
     */
    explicit GatewayApp(PreferencesRecordStore& store)
        : seq(rng),
          replay(store),
#if defined(LP_AUTH_REQUIRED)
          auth(proto::kDeploymentKey),
#endif
          commands(store),
          coordinator(radio, clock, codec, seq, replay, auth, registry, commands, uplink),
          wifi(clock, station, gateway::kWifiSsid, gateway::kWifiPassword),
          mqtt(wifi, uplink, commands, mqttClient, gateway::kMqttSettings) {}
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
    app->wifi.begin();
    app->mqtt.begin();
    Serial.println(F("[gateway] listening"));
}

void loop() {
    const proto::Status stepped = app->coordinator.poll();
    app->wifi.poll();
    app->mqtt.poll();
    if (app->radio.takeTransmitDone()) {
        digitalWrite(board::kBoardLedPin, HIGH);
        Serial.println(F("[gateway] ack sent"));
    }
    if (stepped != proto::Status::Ok && stepped != proto::Status::Busy) {
        Serial.println(F("[gateway] poll fail"));
        return;
    }

    static bool heard[proto::kMaxNodeId + 1] = {};
    static uint16_t lastSeq[proto::kMaxNodeId + 1] = {};
    static uint32_t lastCounter[proto::kMaxNodeId + 1] = {};
    for (uint8_t nodeId = proto::kMinNodeId; nodeId <= proto::kMaxNodeId; ++nodeId) {
        gateway::NodeFacts facts = {};
        if (!app->registry.lookup(nodeId, &facts)) {
            continue;
        }
        if (heard[nodeId] && facts.seq == lastSeq[nodeId] && facts.counter == lastCounter[nodeId]) {
            continue;
        }
        heard[nodeId] = true;
        lastSeq[nodeId] = facts.seq;
        lastCounter[nodeId] = facts.counter;
        printTelemetry(nodeId, facts);
    }
}
