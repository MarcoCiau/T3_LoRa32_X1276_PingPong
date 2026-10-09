/**
 * @file test_mqtt_service.cpp
 * @brief ThingsBoard JSON mapping and a one-record MQTT poll, without the MQTT library.
 */

#include <unity.h>

#include "mqtt_service.h"
#include "outbound_queue.h"

#include "../fakes/fake_clock.h"
#include "../fakes/memory_record_store.h"

#include <cstring>

namespace {

class FakeStation final : public gateway::WifiStation {
public:
    bool up = false;

    bool isConnected() const override { return up; }
    void beginConnect(const char* /*ssid*/, const char* /*password*/) override {}
};

class FakeMqtt final : public gateway::MqttClient {
public:
    bool up = false;
    bool acceptPublish = true;
    uint32_t sessions = 0;
    uint32_t publishes = 0;
    char topic[64] = {};
    char payload[gateway::kMqttJsonBytes] = {};
    uint16_t payloadLen = 0;
    uint8_t downlink[gateway::kMqttDownlinkBytes] = {};
    uint16_t downlinkLen = 0;
    bool hasDownlink = false;

    bool connected() const override { return up; }

    bool beginSession(const gateway::MqttSettings& /*settings*/) override {
        sessions += 1;
        return true;
    }

    void poll() override {}

    bool publish(const char* topicIn, const char* payloadIn, uint16_t len) override {
        if (!acceptPublish || topicIn == nullptr || payloadIn == nullptr || len >= sizeof payload) {
            return false;
        }
        std::strncpy(topic, topicIn, sizeof topic - 1);
        std::memcpy(payload, payloadIn, len);
        payload[len] = '\0';
        payloadLen = len;
        publishes += 1;
        return true;
    }

    bool takeDownlink(uint8_t* out, size_t cap, uint16_t* outLen) override {
        if (!hasDownlink || out == nullptr || outLen == nullptr || downlinkLen > cap) {
            return false;
        }
        std::memcpy(out, downlink, downlinkLen);
        *outLen = downlinkLen;
        hasDownlink = false;
        return true;
    }
};

gateway::MqttSettings enabledSettings() {
    return {"broker.local", 1883, "lora-gateway", "token", "", "v1/devices/me/telemetry"};
}

void setStatus(proto::Status actual, proto::Status expected) {
    TEST_ASSERT_EQUAL_INT8(static_cast<int8_t>(expected), static_cast<int8_t>(actual));
}

gateway::UplinkRecord telemetryRecord() {
    gateway::UplinkRecord record = {};
    record.kind = gateway::UplinkKind::Telemetry;
    record.nodeId = 1;
    record.seq = 2;
    record.counter = 3;
    record.rssi = -40.5f;
    record.snr = 8.0f;
    return record;
}

}  // namespace

void setUp(void) {}

void tearDown(void) {}

void test_telemetry_json_matches_the_thingsboard_object(void) {
    char json[gateway::kMqttJsonBytes];
    size_t written = 0;
    setStatus(gateway::formatUplinkJson(telemetryRecord(), json, sizeof json, &written), proto::Status::Ok);
    TEST_ASSERT_EQUAL_STRING("{\"node\":1,\"kind\":\"telemetry\",\"seq\":2,\"counter\":3,\"rssi\":-40.5,\"snr\":8.0}",
                             json);
    TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(std::strlen(json)), static_cast<uint32_t>(written));
}

void test_energy_json_matches_the_thingsboard_object(void) {
    gateway::UplinkRecord record = telemetryRecord();
    record.seq = 1;
    record.counter = 1;
    record.rssi = -40.0f;
    record.snr = 8.0f;
    record.hasEnergy = true;
    record.energy.voltageCentivolts = 23010;
    record.energy.currentMilliamps = 1001;
    record.energy.powerWatts = 101;
    record.energy.energyWattHours = 1001;
    record.energy.powerFactorMilli = 950;
    record.energy.frequencyCentiHertz = 5000;
    char json[gateway::kMqttJsonBytes];
    size_t written = 0;
    setStatus(gateway::buildThingsBoardPayload(record, json, sizeof json, &written), proto::Status::Ok);
    const char* expected =
        "{\"node\":1,\"kind\":\"telemetry\",\"seq\":1,\"counter\":1,\"rssi\":-40.0,\"snr\":8.0,"
        "\"voltage\":230.10,\"current\":1.001,\"powerW\":101.0,\"energyWh\":1.001,"
        "\"powerFactor\":0.950,\"frequencyHz\":50.00}";
    TEST_ASSERT_EQUAL_STRING(expected, json);
    TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(std::strlen(json)), static_cast<uint32_t>(written));
}

void test_result_json_matches_the_thingsboard_object(void) {
    gateway::UplinkRecord record = {};
    record.kind = gateway::UplinkKind::CommandResult;
    record.nodeId = 1;
    record.commandId = 9;
    record.resultStatus = static_cast<uint8_t>(proto::CommandResultStatus::Ok);
    char json[gateway::kMqttJsonBytes];
    size_t written = 0;
    setStatus(gateway::formatUplinkJson(record, json, sizeof json, &written), proto::Status::Ok);
    TEST_ASSERT_EQUAL_STRING("{\"node\":1,\"kind\":\"result\",\"commandId\":9,\"status\":0}", json);
}

void test_short_buffer_is_rejected(void) {
    char json[8];
    size_t written = 0;
    setStatus(gateway::formatUplinkJson(telemetryRecord(), json, sizeof json, &written), proto::Status::BadPayload);
}

void test_rpc_params_become_a_command(void) {
    const char* json = "{\"method\":\"command\",\"params\":{\"id\":9,\"node\":1,\"type\":0,\"body\":\"0A0B\"}}";
    gateway::RpcCommand command = {};
    setStatus(gateway::parseRpcCommand(json, std::strlen(json), &command), proto::Status::Ok);
    TEST_ASSERT_EQUAL_UINT32(9, command.commandId);
    TEST_ASSERT_EQUAL_UINT8(1, command.nodeId);
    TEST_ASSERT_EQUAL_UINT8(0, command.commandType);
    TEST_ASSERT_EQUAL_UINT8(2, command.bodyLen);
    TEST_ASSERT_EQUAL_UINT8(0x0A, command.body[0]);
    TEST_ASSERT_EQUAL_UINT8(0x0B, command.body[1]);
}

void test_rpc_without_body_is_empty(void) {
    const char* json = "{ \"id\" : 4, \"node\" : 2, \"type\" : 3 }";
    gateway::RpcCommand command = {};
    setStatus(gateway::parseRpcCommand(json, std::strlen(json), &command), proto::Status::Ok);
    TEST_ASSERT_EQUAL_UINT32(4, command.commandId);
    TEST_ASSERT_EQUAL_UINT8(2, command.nodeId);
    TEST_ASSERT_EQUAL_UINT8(3, command.commandType);
    TEST_ASSERT_EQUAL_UINT8(0, command.bodyLen);
}

void test_rpc_rejects_a_bad_body_and_a_bad_node(void) {
    gateway::RpcCommand command = {};
    const char* odd = "{\"id\":1,\"node\":1,\"type\":0,\"body\":\"0A0\"}";
    setStatus(gateway::parseRpcCommand(odd, std::strlen(odd), &command), proto::Status::BadPayload);
    const char* node = "{\"id\":1,\"node\":0,\"type\":0}";
    setStatus(gateway::parseRpcCommand(node, std::strlen(node), &command), proto::Status::BadPayload);
    const char* missing = "{\"id\":1,\"type\":0}";
    setStatus(gateway::parseRpcCommand(missing, std::strlen(missing), &command), proto::Status::BadPayload);
}

void test_empty_token_does_not_publish(void) {
    FakeClock clock;
    FakeStation station;
    station.up = true;
    gateway::WifiLink wifi(clock, station, "lab", "");
    wifi.begin();
    wifi.poll();
    gateway::OutboundQueue uplink;
    MemoryRecordStore store;
    gateway::CommandQueue commands(store);
    FakeMqtt client;
    client.up = true;
    gateway::MqttSettings settings = {"", 1883, "lora-gateway", "", "", "v1/devices/me/telemetry"};
    gateway::MqttService service(wifi, uplink, commands, client, settings);
    service.begin();
    setStatus(uplink.push(telemetryRecord()), proto::Status::Ok);
    service.poll();
    TEST_ASSERT_EQUAL_UINT32(0, client.sessions);
    TEST_ASSERT_EQUAL_UINT32(0, client.publishes);
    TEST_ASSERT_EQUAL_UINT8(1, uplink.size());
}

void test_poll_publishes_one_record_and_leaves_the_rest(void) {
    FakeClock clock;
    FakeStation station;
    station.up = true;
    gateway::WifiLink wifi(clock, station, "lab", "");
    wifi.begin();
    wifi.poll();
    gateway::OutboundQueue uplink;
    MemoryRecordStore store;
    gateway::CommandQueue commands(store);
    FakeMqtt client;
    gateway::MqttService service(wifi, uplink, commands, client, enabledSettings());
    service.begin();

    setStatus(uplink.push(telemetryRecord()), proto::Status::Ok);
    gateway::UplinkRecord second = telemetryRecord();
    second.seq = 5;
    setStatus(uplink.push(second), proto::Status::Ok);

    service.poll();
    TEST_ASSERT_EQUAL_UINT32(1, client.sessions);
    TEST_ASSERT_EQUAL_UINT32(0, client.publishes);
    TEST_ASSERT_EQUAL_UINT8(2, uplink.size());

    client.up = true;
    service.poll();
    TEST_ASSERT_EQUAL_UINT32(1, client.sessions);
    TEST_ASSERT_EQUAL_UINT32(1, client.publishes);
    TEST_ASSERT_EQUAL_STRING("v1/devices/me/telemetry", client.topic);
    TEST_ASSERT_EQUAL_STRING("{\"node\":1,\"kind\":\"telemetry\",\"seq\":2,\"counter\":3,\"rssi\":-40.5,\"snr\":8.0}",
                             client.payload);
    TEST_ASSERT_EQUAL_UINT8(1, uplink.size());

    service.poll();
    TEST_ASSERT_EQUAL_UINT32(2, client.publishes);
    TEST_ASSERT_EQUAL_UINT8(0, uplink.size());
}

void test_failed_publish_puts_the_record_back(void) {
    FakeClock clock;
    FakeStation station;
    station.up = true;
    gateway::WifiLink wifi(clock, station, "lab", "");
    wifi.begin();
    wifi.poll();
    gateway::OutboundQueue uplink;
    MemoryRecordStore store;
    gateway::CommandQueue commands(store);
    FakeMqtt client;
    client.up = true;
    client.acceptPublish = false;
    gateway::MqttService service(wifi, uplink, commands, client, enabledSettings());
    service.begin();
    setStatus(uplink.push(telemetryRecord()), proto::Status::Ok);
    service.poll();
    TEST_ASSERT_EQUAL_UINT8(1, uplink.size());
}

void test_rpc_downlink_is_stored_on_the_command_queue(void) {
    FakeClock clock;
    FakeStation station;
    station.up = true;
    gateway::WifiLink wifi(clock, station, "lab", "");
    wifi.begin();
    wifi.poll();
    gateway::OutboundQueue uplink;
    MemoryRecordStore store;
    gateway::CommandQueue commands(store);
    FakeMqtt client;
    client.up = true;
    const char* json = "{\"method\":\"command\",\"params\":{\"id\":9,\"node\":1,\"type\":4,\"body\":\"AA\"}}";
    client.downlinkLen = static_cast<uint16_t>(std::strlen(json));
    std::memcpy(client.downlink, json, client.downlinkLen);
    client.hasDownlink = true;
    gateway::MqttService service(wifi, uplink, commands, client, enabledSettings());
    service.begin();
    service.poll();

    gateway::QueuedCommand stored = {};
    TEST_ASSERT_TRUE(commands.find(9, &stored));
    TEST_ASSERT_EQUAL_UINT8(1, stored.nodeId);
    TEST_ASSERT_EQUAL_UINT8(4, stored.commandType);
    TEST_ASSERT_EQUAL_UINT8(1, stored.bodyLen);
    TEST_ASSERT_EQUAL_UINT8(0xAA, stored.body[0]);
    TEST_ASSERT_EQUAL_UINT32(0, client.publishes);
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_telemetry_json_matches_the_thingsboard_object);
    RUN_TEST(test_energy_json_matches_the_thingsboard_object);
    RUN_TEST(test_result_json_matches_the_thingsboard_object);
    RUN_TEST(test_short_buffer_is_rejected);
    RUN_TEST(test_rpc_params_become_a_command);
    RUN_TEST(test_rpc_without_body_is_empty);
    RUN_TEST(test_rpc_rejects_a_bad_body_and_a_bad_node);
    RUN_TEST(test_empty_token_does_not_publish);
    RUN_TEST(test_poll_publishes_one_record_and_leaves_the_rest);
    RUN_TEST(test_failed_publish_puts_the_record_back);
    RUN_TEST(test_rpc_downlink_is_stored_on_the_command_queue);
    return UNITY_END();
}
