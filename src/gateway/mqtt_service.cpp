/**
 * @file mqtt_service.cpp
 * @brief ThingsBoard JSON mapping and the one-record MQTT poll.
 *
 * formatUplinkJson() and parseRpcCommand() do not open a socket. poll() starts a
 * session and returns; it does not wait for a CONNACK and it does not call the radio.
 */

#include "mqtt_service.h"

#include <cstring>

namespace gateway {
namespace {

/** @brief Append @p text. @return false when @p cap cannot hold another byte plus a NUL. */
bool appendText(char* out, size_t cap, size_t* used, const char* text) {
    while (*text != '\0') {
        if (*used + 1 >= cap) {
            return false;
        }
        out[(*used)++] = *text++;
    }
    return true;
}

/** @brief Append the decimal form of @p value. */
bool appendU32(char* out, size_t cap, size_t* used, uint32_t value) {
    char digits[10];
    uint8_t count = 0;
    do {
        digits[count++] = static_cast<char>('0' + (value % 10));
        value /= 10;
    } while (value != 0);
    while (count > 0) {
        if (*used + 1 >= cap) {
            return false;
        }
        out[(*used)++] = digits[--count];
    }
    return true;
}

/**
 * @brief Append @p value with one digit after the decimal point.
 * @return false when @p value is outside the range this encoder writes.
 */
bool appendFloat1(char* out, size_t cap, size_t* used, float value) {
    if (value < 0.0f) {
        if (*used + 1 >= cap) {
            return false;
        }
        out[(*used)++] = '-';
        value = -value;
    }
    if (value > 100000.0f) {
        return false;
    }
    const uint32_t scaled = static_cast<uint32_t>(value * 10.0f + 0.5f);
    if (!appendU32(out, cap, used, scaled / 10)) {
        return false;
    }
    if (*used + 1 >= cap) {
        return false;
    }
    out[(*used)++] = '.';
    if (*used + 1 >= cap) {
        return false;
    }
    out[(*used)++] = static_cast<char>('0' + (scaled % 10));
    return true;
}

/** @brief Pointer to the first value byte after `"key"`, or null when the key is absent. */
const char* valueAfterKey(const char* json, size_t len, const char* key) {
    const size_t keyLen = std::strlen(key);
    if (keyLen == 0 || len < keyLen + 3) {
        return nullptr;
    }
    for (size_t i = 0; i + keyLen + 2 < len; ++i) {
        if (json[i] != '"') {
            continue;
        }
        if (std::memcmp(json + i + 1, key, keyLen) != 0) {
            continue;
        }
        if (json[i + 1 + keyLen] != '"') {
            continue;
        }
        size_t cursor = i + keyLen + 2;
        while (cursor < len && (json[cursor] == ' ' || json[cursor] == '\t' || json[cursor] == '\n' ||
                                json[cursor] == '\r')) {
            cursor += 1;
        }
        if (cursor < len && json[cursor] == ':') {
            return json + cursor + 1;
        }
    }
    return nullptr;
}

/** @brief Read an unsigned decimal at @p cursor. Advances @p cursor past the digits. */
bool parseU32(const char** cursor, const char* end, uint32_t* value) {
    const char* p = *cursor;
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) {
        p += 1;
    }
    if (p >= end || *p < '0' || *p > '9') {
        return false;
    }
    uint32_t parsed = 0;
    while (p < end && *p >= '0' && *p <= '9') {
        const uint32_t digit = static_cast<uint32_t>(*p - '0');
        if (parsed > (4294967295u - digit) / 10u) {
            return false;
        }
        parsed = parsed * 10u + digit;
        p += 1;
    }
    *cursor = p;
    *value = parsed;
    return true;
}

/** @brief Map one hex character to its nibble. @return false when @p c is not hex. */
bool hexNibble(char c, uint8_t* nibble) {
    if (c >= '0' && c <= '9') {
        *nibble = static_cast<uint8_t>(c - '0');
        return true;
    }
    if (c >= 'a' && c <= 'f') {
        *nibble = static_cast<uint8_t>(c - 'a' + 10);
        return true;
    }
    if (c >= 'A' && c <= 'F') {
        *nibble = static_cast<uint8_t>(c - 'A' + 10);
        return true;
    }
    return false;
}

/**
 * @brief Decode a JSON hex string into @p body.
 * @p cursor points at the first byte after the colon. An absent key is handled by the caller.
 */
bool parseHexBody(const char** cursor, const char* end, uint8_t* body, uint8_t* bodyLen) {
    const char* p = *cursor;
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) {
        p += 1;
    }
    if (p >= end || *p != '"') {
        return false;
    }
    p += 1;
    uint8_t count = 0;
    while (p < end && *p != '"') {
        if (count >= kCommandBodyBytes) {
            return false;
        }
        uint8_t high = 0;
        uint8_t low = 0;
        if (p + 1 >= end || !hexNibble(*p, &high) || !hexNibble(*(p + 1), &low)) {
            return false;
        }
        body[count++] = static_cast<uint8_t>((high << 4) | low);
        p += 2;
    }
    if (p >= end || *p != '"') {
        return false;
    }
    *cursor = p + 1;
    *bodyLen = count;
    return true;
}

}  // namespace

proto::Status formatUplinkJson(const UplinkRecord& record, char* out, size_t cap, size_t* written) {
    if (out == nullptr || written == nullptr || cap == 0) {
        return proto::Status::BadPayload;
    }
    size_t used = 0;
    bool ok = appendText(out, cap, &used, "{\"node\":");
    ok = ok && appendU32(out, cap, &used, record.nodeId);
    if (record.kind == UplinkKind::Telemetry) {
        ok = ok && appendText(out, cap, &used, ",\"kind\":\"telemetry\",\"seq\":");
        ok = ok && appendU32(out, cap, &used, record.seq);
        ok = ok && appendText(out, cap, &used, ",\"counter\":");
        ok = ok && appendU32(out, cap, &used, record.counter);
        ok = ok && appendText(out, cap, &used, ",\"rssi\":");
        ok = ok && appendFloat1(out, cap, &used, record.rssi);
        ok = ok && appendText(out, cap, &used, ",\"snr\":");
        ok = ok && appendFloat1(out, cap, &used, record.snr);
    } else if (record.kind == UplinkKind::CommandResult) {
        ok = ok && appendText(out, cap, &used, ",\"kind\":\"result\",\"commandId\":");
        ok = ok && appendU32(out, cap, &used, record.commandId);
        ok = ok && appendText(out, cap, &used, ",\"status\":");
        ok = ok && appendU32(out, cap, &used, record.resultStatus);
    } else {
        ok = false;
    }
    ok = ok && appendText(out, cap, &used, "}");
    if (!ok || used >= cap) {
        return proto::Status::BadPayload;
    }
    out[used] = '\0';
    *written = used;
    return proto::Status::Ok;
}

proto::Status parseRpcCommand(const char* json, size_t len, RpcCommand* out) {
    if (json == nullptr || out == nullptr) {
        return proto::Status::BadPayload;
    }
    const char* end = json + len;
    const char* idAt = valueAfterKey(json, len, "id");
    const char* nodeAt = valueAfterKey(json, len, "node");
    const char* typeAt = valueAfterKey(json, len, "type");
    uint32_t commandId = 0;
    uint32_t nodeId = 0;
    uint32_t commandType = 0;
    if (idAt == nullptr || nodeAt == nullptr || typeAt == nullptr || !parseU32(&idAt, end, &commandId) ||
        !parseU32(&nodeAt, end, &nodeId) || !parseU32(&typeAt, end, &commandType)) {
        return proto::Status::BadPayload;
    }
    if (nodeId < proto::kMinNodeId || nodeId > proto::kMaxNodeId || commandType > 255u) {
        return proto::Status::BadPayload;
    }
    RpcCommand parsed = {};
    parsed.commandId = commandId;
    parsed.nodeId = static_cast<uint8_t>(nodeId);
    parsed.commandType = static_cast<uint8_t>(commandType);
    const char* bodyAt = valueAfterKey(json, len, "body");
    if (bodyAt != nullptr) {
        if (!parseHexBody(&bodyAt, end, parsed.body, &parsed.bodyLen)) {
            return proto::Status::BadPayload;
        }
    }
    *out = parsed;
    return proto::Status::Ok;
}

MqttService::MqttService(WifiLink& wifi,
                         OutboundBuffer& uplink,
                         CommandQueue& commands,
                         MqttClient& client,
                         const MqttSettings& settings)
    : wifi_(wifi), uplink_(uplink), commands_(commands), client_(client), settings_(settings) {}

void MqttService::begin() {
    const char* host = settings_.host;
    const char* username = settings_.username;
    const char* clientId = settings_.clientId;
    const char* topic = settings_.telemetryTopic;
    enabled_ = host != nullptr && host[0] != '\0' && username != nullptr && username[0] != '\0' &&
               clientId != nullptr && clientId[0] != '\0' && topic != nullptr && topic[0] != '\0' &&
               settings_.port != 0;
    if (settings_.password == nullptr) {
        settings_.password = "";
    }
    sessionStarted_ = false;
}

void MqttService::poll() {
    client_.poll();
    if (!enabled_ || !wifi_.connected()) {
        return;
    }
    if (!client_.connected()) {
        if (!sessionStarted_) {
            sessionStarted_ = client_.beginSession(settings_);
        }
        return;
    }
    acceptOneDownlink();
    publishOneUplink();
}

void MqttService::acceptOneDownlink() {
    uint8_t payload[kMqttDownlinkBytes];
    uint16_t length = 0;
    if (!client_.takeDownlink(payload, sizeof payload, &length)) {
        return;
    }
    RpcCommand command = {};
    if (parseRpcCommand(reinterpret_cast<const char*>(payload), length, &command) != proto::Status::Ok) {
        return;
    }
    const uint8_t* body = command.bodyLen == 0 ? nullptr : command.body;
    commands_.enqueueWithId(command.nodeId, command.commandType, body, command.bodyLen, command.commandId);
}

void MqttService::publishOneUplink() {
    UplinkRecord record = {};
    if (!uplink_.pop(&record)) {
        return;
    }
    char json[kMqttJsonBytes];
    size_t written = 0;
    if (formatUplinkJson(record, json, sizeof json, &written) != proto::Status::Ok ||
        !client_.publish(settings_.telemetryTopic, json, static_cast<uint16_t>(written))) {
        uplink_.push(record);
    }
}

}  // namespace gateway
