/**
 * @file wifi_manager.cpp
 * @brief Reconnect schedule for the gateway station.
 *
 * The next attempt is recorded and poll() returns. Nothing in this file waits
 * for a DHCP lease or includes the radio.
 */

#include "wifi_manager.h"

namespace gateway {

WifiLink::WifiLink(ClockPort& clock, WifiStation& station, const char* ssid, const char* password)
    : clock_(clock), station_(station), ssid_(ssid), password_(password) {}

void WifiLink::begin() {
    enabled_ = ssid_ != nullptr && ssid_[0] != '\0';
    if (password_ == nullptr) {
        password_ = "";
    }
    gapMs_ = kWifiBackoffMinMs;
    attemptPending_ = false;
    if (!enabled_) {
        return;
    }
    startAttempt(clock_.millis());
}

void WifiLink::poll() {
    if (!enabled_) {
        return;
    }
    if (station_.isConnected()) {
        gapMs_ = kWifiBackoffMinMs;
        attemptPending_ = false;
        return;
    }
    const uint32_t now = clock_.millis();
    if (attemptPending_ && static_cast<uint32_t>(now - lastAttemptMs_) < waitMs_) {
        return;
    }
    startAttempt(now);
}

bool WifiLink::connected() const {
    return enabled_ && station_.isConnected();
}

void WifiLink::startAttempt(uint32_t now) {
    station_.beginConnect(ssid_, password_);
    lastAttemptMs_ = now;
    attemptPending_ = true;
    waitMs_ = gapMs_;
    if (gapMs_ < kWifiBackoffMaxMs) {
        const uint32_t doubled = gapMs_ * 2;
        gapMs_ = doubled > kWifiBackoffMaxMs ? kWifiBackoffMaxMs : doubled;
    }
}

}  // namespace gateway
