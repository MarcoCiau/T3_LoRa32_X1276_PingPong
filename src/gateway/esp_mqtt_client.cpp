/**
 * @file esp_mqtt_client.cpp
 * @brief ESP-MQTT adapter for MqttClient. Host tests do not compile this file.
 *
 * The event function uses the client user-context pointer. It does not keep a
 * process-wide MqttClient*. publish() enqueues and returns so the radio loop
 * does not wait on the socket.
 */

#include "mqtt_service.h"

#include <mqtt_client.h>

#include <cstring>

namespace gateway {
namespace {

/**
 * @brief Forward one ESP-MQTT event to the client stored in user_context.
 * @return ESP_OK. The MQTT task keeps running.
 */
esp_err_t onMqttEvent(esp_mqtt_event_handle_t event) {
    if (event == nullptr || event->user_context == nullptr) {
        return ESP_OK;
    }
    auto* self = static_cast<EspMqttClient*>(event->user_context);
    if (event->event_id == MQTT_EVENT_CONNECTED) {
        self->onConnected();
    } else if (event->event_id == MQTT_EVENT_DISCONNECTED) {
        self->onDisconnected();
    } else if (event->event_id == MQTT_EVENT_DATA) {
        const int prefixLen = static_cast<int>(sizeof(LP_MQTT_RPC_REQUEST_TOPIC) - 2);
        const bool fullPayload = event->current_data_offset == 0 && event->data_len == event->total_data_len &&
                                 event->data != nullptr && event->data_len > 0;
        const bool rpcTopic = event->topic != nullptr && event->topic_len >= prefixLen &&
                              std::memcmp(event->topic, LP_MQTT_RPC_REQUEST_TOPIC, static_cast<size_t>(prefixLen)) == 0;
        if (fullPayload && rpcTopic && event->data_len <= static_cast<int>(kMqttDownlinkBytes)) {
            self->storeDownlink(reinterpret_cast<const uint8_t*>(event->data),
                                static_cast<uint16_t>(event->data_len));
        }
    }
    return ESP_OK;
}

/** @brief The handle stored as void* in the header, so this file is the only MQTT include. */
esp_mqtt_client_handle_t clientHandle(void* handle) {
    return static_cast<esp_mqtt_client_handle_t>(handle);
}

}  // namespace

bool EspMqttClient::connected() const {
    return connected_.load(std::memory_order_acquire);
}

bool EspMqttClient::beginSession(const MqttSettings& settings) {
    if (handle_ != nullptr) {
        return true;
    }
    if (settings.host == nullptr || settings.host[0] == '\0') {
        return false;
    }
    esp_mqtt_client_config_t config = {};
    config.event_handle = onMqttEvent;
    config.host = settings.host;
    config.port = settings.port;
    config.client_id = settings.clientId;
    config.username = settings.username;
    config.password = settings.password == nullptr ? "" : settings.password;
    config.user_context = this;
    config.keepalive = 60;
    config.disable_auto_reconnect = false;
    config.buffer_size = 512;
    esp_mqtt_client_handle_t created = esp_mqtt_client_init(&config);
    if (created == nullptr || esp_mqtt_client_start(created) != ESP_OK) {
        if (created != nullptr) {
            esp_mqtt_client_destroy(created);
        }
        return false;
    }
    handle_ = created;
    return true;
}

void EspMqttClient::poll() {}

bool EspMqttClient::publish(const char* topic, const char* payload, uint16_t len) {
    if (handle_ == nullptr || topic == nullptr || (payload == nullptr && len != 0)) {
        return false;
    }
    const int queued = esp_mqtt_client_enqueue(clientHandle(handle_), topic, payload, len, 0, 0, true);
    return queued >= 0;
}

bool EspMqttClient::takeDownlink(uint8_t* out, size_t cap, uint16_t* outLen) {
    if (out == nullptr || outLen == nullptr) {
        return false;
    }
    if (!downlinkPending_.load(std::memory_order_acquire)) {
        return false;
    }
    if (downlinkLen_ > cap) {
        return false;
    }
    std::memcpy(out, downlink_, downlinkLen_);
    *outLen = downlinkLen_;
    downlinkPending_.store(false, std::memory_order_release);
    return true;
}

void EspMqttClient::onConnected() {
    connected_.store(true, std::memory_order_release);
    if (handle_ != nullptr) {
        esp_mqtt_client_subscribe(clientHandle(handle_), LP_MQTT_RPC_REQUEST_TOPIC, 0);
    }
}

void EspMqttClient::onDisconnected() {
    connected_.store(false, std::memory_order_release);
}

void EspMqttClient::storeDownlink(const uint8_t* data, uint16_t len) {
    if (data == nullptr || len == 0 || len > kMqttDownlinkBytes) {
        return;
    }
    if (downlinkPending_.load(std::memory_order_acquire)) {
        return;
    }
    std::memcpy(downlink_, data, len);
    downlinkLen_ = len;
    downlinkPending_.store(true, std::memory_order_release);
}

}  // namespace gateway
