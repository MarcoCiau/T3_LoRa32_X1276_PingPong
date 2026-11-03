/**
 * @file esp_wifi_portal.cpp
 * @brief SoftAP form and NVS copies of the station credentials.
 *
 * Host tests do not compile this file. The check-in path does not call it.
 */

#include "wifi_setup.h"

#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>

#include <cstring>

namespace gateway {
namespace {

constexpr char kNamespace[] = "lpwifi";
constexpr char kPage[] =
    "<!doctype html><html><body><form method=POST action=/save>"
    "SSID <input name=s maxlength=32><br>"
    "Password <input name=p maxlength=64><br>"
    "<button type=submit>Save</button></form></body></html>";

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

    bool load(char* ssid, size_t ssidCap, char* password, size_t passwordCap) override {
        if (!open() || ssid == nullptr || password == nullptr || ssidCap == 0 || passwordCap == 0) {
            return false;
        }
        if (!prefs_.isKey("ssid")) {
            return false;
        }
        copyText(ssid, ssidCap, prefs_.getString("ssid", "").c_str());
        copyText(password, passwordCap, prefs_.getString("pass", "").c_str());
        return ssid[0] != '\0';
    }

    bool save(const char* ssid, const char* password) override {
        if (!open() || ssid == nullptr || ssid[0] == '\0') {
            return false;
        }
        if (password == nullptr) {
            password = "";
        }
        if (prefs_.putString("ssid", ssid) == 0) {
            return false;
        }
        prefs_.putString("pass", password);
        return true;
    }

    bool clear() override {
        if (!open()) {
            return false;
        }
        prefs_.remove("ssid");
        prefs_.remove("pass");
        return true;
    }

private:
    Preferences prefs_;
    bool open_ = false;
};

/**
 * @brief One SoftAP and a two-field form on port 80.
 */
class EspWifiPortal final : public WifiPortal {
public:
    EspWifiPortal() : server_(80) {}

    void start(const char* apName) override {
        posted_ = false;
        ssid_[0] = '\0';
        password_[0] = '\0';
        WiFi.mode(WIFI_AP_STA);
        WiFi.softAP(apName != nullptr ? apName : "lp-gw");
        server_.on("/", HTTP_GET, [this]() { server_.send(200, "text/html", kPage); });
        server_.on("/save", HTTP_POST, [this]() {
            copyText(ssid_, sizeof(ssid_), server_.arg("s").c_str());
            copyText(password_, sizeof(password_), server_.arg("p").c_str());
            posted_ = ssid_[0] != '\0';
            server_.send(200, "text/html", posted_ ? "Saved" : "SSID required");
        });
        server_.begin();
        running_ = true;
    }

    void stop() override {
        if (!running_) {
            return;
        }
        server_.stop();
        WiFi.softAPdisconnect(true);
        running_ = false;
    }

    void poll() override {
        if (running_) {
            server_.handleClient();
        }
    }

    bool takePosted(char* ssid, size_t ssidCap, char* password, size_t passwordCap) override {
        if (!posted_ || ssid == nullptr || password == nullptr) {
            return false;
        }
        copyText(ssid, ssidCap, ssid_);
        copyText(password, passwordCap, password_);
        posted_ = false;
        return ssid[0] != '\0';
    }

private:
    WebServer server_;
    bool running_ = false;
    bool posted_ = false;
    char ssid_[WifiSetup::kSsidCap] = {};
    char password_[WifiSetup::kPasswordCap] = {};
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
