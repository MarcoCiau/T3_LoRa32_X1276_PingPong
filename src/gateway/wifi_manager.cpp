/**
 * @file wifi_manager.cpp
 * @brief Reconnect schedule for the gateway station.
 *
 * The next attempt is recorded and poll() returns. Nothing in this file waits
 * for a DHCP lease or includes the radio.
 */

#include "wifi_manager.h"

#include <cstddef>

namespace gateway {
namespace {

/** @brief Copy @p src into @p dst, truncating so the result stays NUL-terminated. */
void copyText(char* dst, size_t cap, const char* src) {
    if (cap == 0) {
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

WifiLink::WifiLink(ClockPort& clock, WifiStation& station, const char* ssid, const char* password)
    : clock_(clock), station_(station) {
    copyText(ssid_, sizeof(ssid_), ssid);
    copyText(password_, sizeof(password_), password);
}

void WifiLink::begin() {
    enabled_ = ssid_[0] != '\0';
    gapMs_ = kWifiBackoffMinMs;
    attemptPending_ = false;
    if (!enabled_) {
        return;
    }
    startAttempt(clock_.millis());
}

void WifiLink::poll() {
    if (!enabled_ || paused_) {
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

void WifiLink::reconfigure(const char* ssid, const char* password) {
    copyText(ssid_, sizeof(ssid_), ssid);
    copyText(password_, sizeof(password_), password);
    enabled_ = ssid_[0] != '\0';
    gapMs_ = kWifiBackoffMinMs;
    attemptPending_ = false;
    if (!enabled_) {
        return;
    }
    startAttempt(clock_.millis());
}

void WifiLink::setPaused(bool paused) {
    paused_ = paused;
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
