/**
 * @file esp_wifi_station.cpp
 * @brief ESP32 Arduino adapter for WifiStation. Host tests do not compile this file.
 */

#include "wifi_manager.h"

#include <WiFi.h>

namespace gateway {

bool EspWifiStation::isConnected() const {
    if (WiFi.status() != WL_CONNECTED) {
        return false;
    }
    const IPAddress ip = WiFi.localIP();
    if (ip == IPAddress(0, 0, 0, 0)) {
        return false;
    }
    const wifi_mode_t mode = WiFi.getMode();
    if ((mode == WIFI_AP || mode == WIFI_AP_STA) && ip == WiFi.softAPIP()) {
        return false;
    }
    return true;
}

void EspWifiStation::beginConnect(const char* ssid, const char* password) {
    if (ssid == nullptr || ssid[0] == '\0') {
        return;
    }
    WiFi.persistent(false);
    WiFi.setAutoReconnect(true);
    if (WiFi.getMode() != WIFI_STA) {
        WiFi.mode(WIFI_STA);
    }
    WiFi.begin(ssid, password != nullptr ? password : "");
    Serial.print(F("[gateway] wifi join "));
    Serial.println(ssid);
}

}  // namespace gateway
