/**
 * @file mqtt_service.h
 * @brief Gateway MQTT service: one uplink JSON publish per poll, RPC into the command queue.
 *
 * The broker, token, and topics below are the ThingsBoard CE device API. Leave the host
 * and the access token empty until a live broker test. This header does not include
 * the ESP-MQTT client.
 */

#pragma once

#include "command_queue.h"
#include "outbound_buffer.h"
#include "wifi_manager.h"

#include <atomic>
#include <cstddef>
#include <cstdint>

/**
 * @brief Broker host. Empty keeps MQTT down so the radio lab image does not open a socket.
 *
 * ThingsBoard CE: the broker hostname, for example the address of a local CE install.
 */
#define LP_MQTT_HOST ""

/** @brief Broker TCP port. ThingsBoard CE MQTT is 1883. */
#define LP_MQTT_PORT 1883

/** @brief MQTT client id sent in CONNECT. ThingsBoard accepts any non-empty id. */
#define LP_MQTT_CLIENT_ID "lora-gateway"

/**
 * @brief ThingsBoard device access token, sent as the MQTT username.
 *
 * Empty keeps MQTT down. The password macro may stay empty; CE authenticates the token.
 */
#define LP_MQTT_ACCESS_TOKEN ""

/** @brief MQTT password. ThingsBoard CE device API uses an empty password. */
#define LP_MQTT_PASSWORD ""

/** @brief ThingsBoard CE telemetry topic. Uplink JSON is published here. */
#define LP_MQTT_TELEMETRY_TOPIC "v1/devices/me/telemetry"

/**
 * @brief ThingsBoard CE RPC subscribe filter.
 *
 * A command arrives on `v1/devices/me/rpc/request/<requestId>`.
 */
#define LP_MQTT_RPC_REQUEST_TOPIC "v1/devices/me/rpc/request/+"

namespace gateway {

/** @brief Bytes reserved for one uplink JSON document, including the terminating NUL. */
constexpr size_t kMqttJsonBytes = 256;

/** @brief Bytes copied from one downlink RPC payload. */
constexpr size_t kMqttDownlinkBytes = 192;

/**
 * @brief Broker and topic strings for MqttService.
 *
 * The pointers must outlive the service. kMqttSettings is filled from the LP_MQTT_* macros.
 */
struct MqttSettings {
    const char* host;             ///< Broker hostname. Empty disables the service.
    uint16_t port;                ///< Broker TCP port.
    const char* clientId;         ///< MQTT client id. Empty is rejected by begin().
    const char* username;         ///< ThingsBoard access token. Empty disables the service.
    const char* password;         ///< MQTT password. May be empty.
    const char* telemetryTopic;   ///< Publish topic for uplink JSON.
};

/** @brief Placeholders from the LP_MQTT_* macros. Edit the macros, not this object. */
constexpr MqttSettings kMqttSettings = {
    LP_MQTT_HOST,
    LP_MQTT_PORT,
    LP_MQTT_CLIENT_ID,
    LP_MQTT_ACCESS_TOKEN,
    LP_MQTT_PASSWORD,
    LP_MQTT_TELEMETRY_TOPIC,
};

/**
 * @brief One ThingsBoard RPC command parsed from a downlink payload.
 *
 * body holds at most kCommandBodyBytes. Bytes past bodyLen are zero.
 */
struct RpcCommand {
    uint32_t commandId;               ///< params.id. Stored with enqueueWithId.
    uint8_t nodeId;                   ///< params.node. Must be 1..proto::kMaxNodeId.
    uint8_t commandType;              ///< params.type.
    uint8_t bodyLen;                  ///< Bytes used in body.
    uint8_t body[kCommandBodyBytes];  ///< Decoded params.body hex.
};

/**
 * @brief MQTT session used by MqttService.
 *
 * beginSession() returns before the broker answers. publish() queues one message and
 * returns. Host tests use a double. The device type is EspMqttClient.
 */
class MqttClient {
public:
    virtual ~MqttClient() = default;

    /** @brief True after the broker has accepted the session. */
    virtual bool connected() const = 0;

    /**
     * @brief Start one session. Return before the broker answers.
     * @param settings Host, token, and client id. The pointers must stay valid.
     * @return false when the session was not started.
     */
    virtual bool beginSession(const MqttSettings& settings) = 0;

    /**
     * @brief Stop dialing the broker.
     *
     * Safe when no session exists. The next beginSession() may start again.
     */
    virtual void endSession() = 0;

    /** @brief Let the client move a session forward. Must not block the caller. */
    virtual void poll() = 0;

    /**
     * @brief Queue one payload on @p topic.
     * @param topic Publish topic. Not null.
     * @param payload Bytes to send. Not null when @p len is greater than 0.
     * @param len Payload length. No terminating NUL is required.
     * @return false when the payload was not queued.
     */
    virtual bool publish(const char* topic, const char* payload, uint16_t len) = 0;

    /**
     * @brief Copy one waiting downlink payload and remove it.
     * @param out Receives the payload. Required. Not necessarily NUL-terminated.
     * @param cap Bytes available at @p out.
     * @param outLen Receives the copied length. Required.
     * @return false when no payload is waiting or it does not fit in @p cap.
     */
    virtual bool takeDownlink(uint8_t* out, size_t cap, uint16_t* outLen) = 0;
};

/**
 * @brief Build the JSON document published to LP_MQTT_TELEMETRY_TOPIC.
 *
 * This is the only place that chooses the ThingsBoard keys. Change this function
 * to change the payload. Telemetry keeps node, kind, seq, counter, rssi, and snr.
 * When @p record has an energy body, the document also carries voltage (volts),
 * current (amps), powerW (watts), energyWh (kilowatt-hours), powerFactor (0–1),
 * and frequencyHz (hertz). A record without that body omits those keys. A command
 * result stays `{"node":N,"kind":"result","commandId":I,"status":S}`.
 *
 * @param record Accepted uplink.
 * @param out Receives the JSON and a terminating NUL. Required.
 * @param cap Bytes available at @p out, including the NUL.
 * @param written Receives the JSON length without the NUL. Required.
 * @return Status::Ok, or BadPayload when @p out is missing or @p cap is too small.
 */
proto::Status buildThingsBoardPayload(const UplinkRecord& record, char* out, size_t cap, size_t* written);

/**
 * @brief Same document as buildThingsBoardPayload().
 *
 * publish and the host tests call that function. This name remains for callers
 * that already format an uplink.
 */
proto::Status formatUplinkJson(const UplinkRecord& record, char* out, size_t cap, size_t* written);

/**
 * @brief Parse a ThingsBoard CE RPC request into a command.
 *
 * The payload is `{"method":"command","params":{"id":1,"node":1,"type":0,"body":"0A0B"}}`.
 * `body` is optional hex, two digits per byte, at most kCommandBodyBytes. Whitespace
 * around `:` is ignored. A missing id, node, or type is BadPayload.
 *
 * @param json RPC payload. Not necessarily NUL-terminated. Required.
 * @param len Bytes at @p json.
 * @param out Filled on success. Required.
 * @return Status::Ok, or BadPayload when a field is missing or out of range.
 */
proto::Status parseRpcCommand(const char* json, size_t len, RpcCommand* out);

/**
 * @brief Publish at most one uplink per poll and accept at most one RPC.
 *
 * poll() returns without waiting for the broker. It does not publish while Wi-Fi or
 * the client is down, and it does not call the radio. MQTT is not used from an IRQ.
 */
class MqttService {
public:
    /**
     * @brief Bind the link, the queues, and the client.
     *
     * @p wifi, @p uplink, @p commands, @p client, and every pointer inside @p settings
     * must outlive this object.
     */
    MqttService(WifiLink& wifi,
                OutboundBuffer& uplink,
                CommandQueue& commands,
                MqttClient& client,
                const MqttSettings& settings);

    /**
     * @brief Arm the service.
     *
     * An empty host, an empty username, or a zero port leaves the service idle.
     */
    void begin();

    /**
     * @brief Use @p username as the ThingsBoard device id and arm the service again.
     *
     * The broker host is unchanged. An empty @p username leaves the previous username.
     * A session that has not started yet uses the new id. A session already accepted
     * by the broker keeps that connection until reboot.
     */
    void setUsername(const char* username);

    /**
     * @brief Advance the session, accept one RPC, and publish one uplink JSON document.
     *
     * A session starts only after the station has an IP. Losing that address stops
     * the client so it does not dial early. While the address is held, a down
     * client is started again. A failed publish puts the uplink record back.
     */
    void poll();

private:
    /** @brief Parse one downlink payload and store it when the JSON is a command. */
    void acceptOneDownlink();

    /** @brief Pop one uplink and publish its JSON. A failed publish is pushed back. */
    void publishOneUplink();

    WifiLink& wifi_;
    OutboundBuffer& uplink_;
    CommandQueue& commands_;
    MqttClient& client_;
    MqttSettings settings_;
    bool enabled_ = false;
    bool sessionStarted_ = false;
};

/**
 * @brief MqttClient that uses the ESP-MQTT task.
 *
 * The methods live in a device translation unit. Host tests do not construct this type.
 * The event callback receives this object as user context. There is no process-wide
 * client pointer.
 */
class EspMqttClient final : public MqttClient {
public:
    /** @brief True after MQTT_EVENT_CONNECTED until MQTT_EVENT_DISCONNECTED. */
    bool connected() const override;

    /** @brief Start the ESP-MQTT client. A later call starts it again after endSession(). */
    bool beginSession(const MqttSettings& settings) override;

    /** @brief Stop the ESP-MQTT task so it cannot dial without a station address. */
    void endSession() override;

    /** @brief The ESP-MQTT task services the socket. This call does not wait. */
    void poll() override;

    /** @brief Enqueue a QoS 0 payload and return. */
    bool publish(const char* topic, const char* payload, uint16_t len) override;

    /** @brief Copy the one stored RPC payload, if the MQTT task has delivered one. */
    bool takeDownlink(uint8_t* out, size_t cap, uint16_t* outLen) override;

    /**
     * @brief Remember that the broker accepted the session.
     *
     * Called from the MQTT event handler. Subscribes to LP_MQTT_RPC_REQUEST_TOPIC.
     */
    void onConnected();

    /** @brief Remember that the session dropped. Called from the MQTT event handler. */
    void onDisconnected();

    /**
     * @brief Store one RPC payload for takeDownlink().
     *
     * A payload that arrives while one is still waiting is dropped.
     * @param data RPC JSON. Required.
     * @param len Length of @p data. Longer than kMqttDownlinkBytes is dropped.
     */
    void storeDownlink(const uint8_t* data, uint16_t len);

private:
    /** @brief esp_mqtt_client_handle_t. Null until beginSession() creates the client. */
    void* handle_ = nullptr;
    /** @brief True while the ESP-MQTT task is allowed to dial. */
    bool running_ = false;
    /** @brief millis() of the last start failure. Zero means none. */
    uint32_t lastFailMs_ = 0;
    /** @brief Set from the MQTT task. poll() only reads it. */
    std::atomic<bool> connected_{false};
    /** @brief True while downlink_ holds an RPC the loop has not copied yet. */
    std::atomic<bool> downlinkPending_{false};
    uint16_t downlinkLen_ = 0;
    uint8_t downlink_[kMqttDownlinkBytes] = {};
};

}  // namespace gateway
