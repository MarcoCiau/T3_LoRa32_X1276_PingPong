/**
 * @file esp_wifi_portal.cpp
 * @brief SoftAP form and NVS copies of the station credentials.
 *
 * Host tests do not compile this file. The check-in path does not call it.
 */

#include "wifi_setup.h"

#include "portal_pages.h"

#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <Update.h>

#include <esp_task_wdt.h>

#include <cstdio>
#include <cstring>

namespace gateway {
namespace {

constexpr char kNamespace[] = "lpwifi";

/** @brief Copy @p src into @p dst, keeping room for a NUL. */
void copyText(char* dst, size_t cap, const char* src) {
    if (dst == nullptr || cap == 0) {
        return;
    }
    if (src == nullptr) {
        dst[0] = '\0';
        return;
    }
    size_t i = 0;
    while (i + 1 < cap && src[i] != '\0') {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

/** @brief Append @p src. @return false when @p dst would overflow. */
bool appendText(char* dst, size_t cap, size_t* used, const char* src) {
    if (dst == nullptr || used == nullptr || cap == 0) {
        return false;
    }
    size_t n = *used;
    if (src == nullptr) {
        src = "";
    }
    for (size_t i = 0; src[i] != '\0'; ++i) {
        if (n + 1 >= cap) {
            dst[cap - 1] = '\0';
            return false;
        }
        dst[n++] = src[i];
    }
    dst[n] = '\0';
    *used = n;
    return true;
}

/** @brief Append @p src as a JSON string, escaping quotes and backslashes. */
bool appendJson(char* dst, size_t cap, size_t* used, const char* src) {
    if (!appendText(dst, cap, used, "\"")) {
        return false;
    }
    if (src == nullptr) {
        src = "";
    }
    for (size_t i = 0; src[i] != '\0'; ++i) {
        const char ch = src[i];
        if (ch == '"' || ch == '\\') {
            if (!appendText(dst, cap, used, "\\")) {
                return false;
            }
        }
        char one[2] = {ch, '\0'};
        if (!appendText(dst, cap, used, one)) {
            return false;
        }
    }
    return appendText(dst, cap, used, "\"");
}

/** @brief Write a dotted address into @p dst. */
void formatIp(char* dst, size_t cap, const IPAddress& ip) {
    if (dst == nullptr || cap == 0) {
        return;
    }
    std::snprintf(dst, cap, "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
}

}  // namespace

/**
 * @brief Preferences namespace lpwifi. Empty SSID means no stored network.
 */
class EspWifiSecrets final : public WifiSecrets {
public:
    /** @brief Open the namespace. A later call reuses it. */
    bool open() {
        if (open_) {
            return true;
        }
        open_ = prefs_.begin(kNamespace, false);
        return open_;
    }

    bool load(char* ssid,
              size_t ssidCap,
              char* password,
              size_t passwordCap,
              char* token,
              size_t tokenCap) override {
        if (!open() || ssid == nullptr || password == nullptr || token == nullptr || ssidCap == 0 ||
            passwordCap == 0 || tokenCap == 0) {
            return false;
        }
        if (!prefs_.isKey("ssid")) {
            return false;
        }
        copyText(ssid, ssidCap, prefs_.getString("ssid", "").c_str());
        copyText(password, passwordCap, prefs_.getString("pass", "").c_str());
        copyText(token, tokenCap, prefs_.getString("tb", "").c_str());
        return ssid[0] != '\0';
    }

    bool save(const char* ssid, const char* password, const char* token) override {
        if (!open() || ssid == nullptr || ssid[0] == '\0') {
            return false;
        }
        if (password == nullptr) {
            password = "";
        }
        if (token == nullptr) {
            token = "";
        }
        if (prefs_.putString("ssid", ssid) == 0) {
            return false;
        }
        prefs_.putString("pass", password);
        if (token[0] != '\0') {
            prefs_.putString("tb", token);
        }
        return true;
    }

    bool clear() override {
        if (!open()) {
            return false;
        }
        prefs_.remove("ssid");
        prefs_.remove("pass");
        prefs_.remove("tb");
        return true;
    }

private:
    Preferences prefs_;
    bool open_ = false;
};

/**
 * @brief SoftAP pages: home, device config, and firmware update.
 */
class EspWifiPortal final : public WifiPortal {
public:
    EspWifiPortal() : server_(80) {}

    void start(const char* apName) override {
        posted_ = false;
        ssid_[0] = '\0';
        password_[0] = '\0';
        token_[0] = '\0';
        copyText(apName_, sizeof(apName_), apName != nullptr ? apName : "lp-gw");
        WiFi.persistent(false);
        WiFi.mode(WIFI_AP_STA);
        WiFi.softAP(apName_);
        if (!routes_) {
            server_.on("/", HTTP_GET, [this]() { sendPage(device_portal::kHome); });
            server_.on("/config", HTTP_GET, [this]() { sendPage(device_portal::kConfig); });
            server_.on("/ota", HTTP_GET, [this]() { sendPage(device_portal::kOta); });
            server_.on("/style.css", HTTP_GET, [this]() { sendPage(device_portal::kStyle, PSTR("text/css")); });
            server_.on("/status", HTTP_GET, [this]() { sendStatus(); });
            server_.on("/save", HTTP_POST, [this]() { handleSave(); });
            server_.on("/update", HTTP_POST, [this]() { finishUpdate(); }, [this]() { writeUpdate(); });
            server_.onNotFound([this]() {
                if (server_.method() == HTTP_GET) {
                    sendPage(device_portal::kHome);
                    return;
                }
                server_.send(404, "text/plain", "Not found");
            });
            routes_ = true;
        }
        server_.begin();
        running_ = true;
    }

    void stop() override {
        if (!running_) {
            return;
        }
        server_.stop();
        WiFi.softAPdisconnect(false);
        running_ = false;
    }

    void poll() override {
        if (running_) {
            server_.handleClient();
        }
    }

    bool takePosted(char* ssid,
                    size_t ssidCap,
                    char* password,
                    size_t passwordCap,
                    char* token,
                    size_t tokenCap) override {
        if (!posted_ || ssid == nullptr || password == nullptr || token == nullptr) {
            return false;
        }
        copyText(ssid, ssidCap, ssid_);
        copyText(password, passwordCap, password_);
        copyText(token, tokenCap, token_);
        posted_ = false;
        return ssid[0] != '\0';
    }

private:
    /** @brief Send one flash page. */
    void sendPage(const char* page, const char* type = PSTR("text/html")) {
        server_.send_P(200, type, page);
    }

    /** @brief Accept the device-config form. An empty ThingsBoard id keeps the stored one. */
    void handleSave() {
        copyText(ssid_, sizeof(ssid_), server_.arg("s").c_str());
        copyText(password_, sizeof(password_), server_.arg("p").c_str());
        copyText(token_, sizeof(token_), server_.arg("t").c_str());
        posted_ = ssid_[0] != '\0';
        sendPage(posted_ ? device_portal::kSaved : device_portal::kSsidRequired);
    }

    /** @brief Device info for the home page. The access token is not included. */
    void sendStatus() {
        char ssid[WifiSetup::kSsidCap] = {};
        bool tokenSet = false;
        Preferences prefs;
        if (prefs.begin(kNamespace, true)) {
            if (prefs.isKey("ssid")) {
                copyText(ssid, sizeof(ssid), prefs.getString("ssid", "").c_str());
            }
            tokenSet = prefs.isKey("tb") && prefs.getString("tb", "").length() > 0;
            prefs.end();
        }

        uint8_t mac[6] = {};
        WiFi.softAPmacAddress(mac);
        char macText[18] = {};
        std::snprintf(macText, sizeof(macText), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3],
                      mac[4], mac[5]);

        char apIp[16] = {};
        char staIp[16] = {};
        formatIp(apIp, sizeof(apIp), WiFi.softAPIP());
        const bool up = WiFi.status() == WL_CONNECTED;
        formatIp(staIp, sizeof(staIp), up ? WiFi.localIP() : IPAddress(0, 0, 0, 0));
        if (up) {
            copyText(ssid, sizeof(ssid), WiFi.SSID().c_str());
        }

        char json[512] = {};
        size_t used = 0;
        const bool ok = appendText(json, sizeof(json), &used, "{\"name\":") &&
                        appendJson(json, sizeof(json), &used, apName_) &&
                        appendText(json, sizeof(json), &used, ",\"mac\":") &&
                        appendJson(json, sizeof(json), &used, macText) &&
                        appendText(json, sizeof(json), &used, ",\"ap\":") &&
                        appendJson(json, sizeof(json), &used, apIp) &&
                        appendText(json, sizeof(json), &used, ",\"sta\":") &&
                        appendJson(json, sizeof(json), &used, up ? "up" : "down") &&
                        appendText(json, sizeof(json), &used, ",\"ip\":") &&
                        appendJson(json, sizeof(json), &used, staIp) &&
                        appendText(json, sizeof(json), &used, ",\"ssid\":") &&
                        appendJson(json, sizeof(json), &used, ssid) &&
                        appendText(json, sizeof(json), &used, ",\"tb\":") &&
                        appendJson(json, sizeof(json), &used, tokenSet ? "set" : "empty") &&
                        appendText(json, sizeof(json), &used, "}");
        server_.send(ok ? 200 : 500, "application/json", ok ? json : "{}");
    }

    /** @brief Write one chunk of a firmware image. */
    void writeUpdate() {
        HTTPUpload& upload = server_.upload();
        if (upload.status == UPLOAD_FILE_START) {
            Serial.println(F("[gateway] ota start"));
            if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
                Update.printError(Serial);
            }
        } else if (upload.status == UPLOAD_FILE_WRITE) {
            esp_task_wdt_reset();
            if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
                Update.printError(Serial);
            }
        } else if (upload.status == UPLOAD_FILE_END) {
            esp_task_wdt_reset();
            if (Update.end(true)) {
                Serial.println(F("[gateway] ota ok"));
            } else {
                Update.printError(Serial);
                Serial.println(F("[gateway] ota fail"));
            }
        } else if (upload.status == UPLOAD_FILE_ABORTED) {
            Update.abort();
            Serial.println(F("[gateway] ota fail"));
        }
    }

    /** @brief Answer the upload, then reboot after a successful image. */
    void finishUpdate() {
        server_.sendHeader("Connection", "close");
        const bool ok = !Update.hasError();
        server_.send(200, "text/plain", ok ? "OK" : "FAIL");
        if (ok) {
            delay(300);
            ESP.restart();
        }
    }

    WebServer server_;
    bool running_ = false;
    bool posted_ = false;
    bool routes_ = false;
    char apName_[33] = {};
    char ssid_[WifiSetup::kSsidCap] = {};
    char password_[WifiSetup::kPasswordCap] = {};
    char token_[WifiSetup::kTokenCap] = {};
};

WifiSecrets& deviceWifiSecrets() {
    static EspWifiSecrets secrets;
    return secrets;
}

WifiPortal& deviceWifiPortal() {
    static EspWifiPortal portal;
    return portal;
}

}  // namespace gateway
