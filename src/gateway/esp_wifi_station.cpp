/**
 * @file esp_wifi_station.cpp
 * @brief ESP32 Arduino adapter for WifiStation. Host tests do not compile this file.
 */

#include "wifi_manager.h"

#include <WiFi.h>

namespace gateway {

bool EspWifiStation::isConnected() const {
    return WiFi.status() == WL_CONNECTED;
}

void EspWifiStation::beginConnect(const char* ssid, const char* password) {
    if (WiFi.getMode() != WIFI_STA) {
        WiFi.mode(WIFI_STA);
    }
    WiFi.begin(ssid, password);
}

}  // namespace gateway
