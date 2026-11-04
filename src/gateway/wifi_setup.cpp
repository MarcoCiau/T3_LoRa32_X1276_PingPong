/**
 * @file wifi_setup.cpp
 * @brief Setup session. The portal and the secret store are injected.
 */

#include "wifi_setup.h"

#include "clock_port.h"
#include "wifi_manager.h"

namespace gateway {

WifiSetup::WifiSetup(ClockPort& clock,
                     WifiPortal& portal,
                     WifiSecrets& secrets,
                     WifiLink& wifi,
                     const char* apName)
    : clock_(clock), portal_(portal), secrets_(secrets), wifi_(wifi), apName_(apName) {}

void WifiSetup::begin() {
    if (active_) {
        return;
    }
    active_ = true;
    startedAt_ = clock_.millis();
    portal_.start(apName_ != nullptr ? apName_ : "lp-gw");
}

void WifiSetup::poll() {
    if (!active_) {
        return;
    }
    portal_.poll();
    char ssid[kSsidCap] = {};
    char password[kPasswordCap] = {};
    char token[kTokenCap] = {};
    if (portal_.takePosted(ssid, sizeof(ssid), password, sizeof(password), token, sizeof(token))) {
        if (ssid[0] != '\0' && secrets_.save(ssid, password, token)) {
            portal_.stop();
            active_ = false;
            saved_ = true;
            wifi_.reconfigure(ssid, password);
            return;
        }
    }
    if (static_cast<uint32_t>(clock_.millis() - startedAt_) >= kTimeoutMs) {
        cancel();
    }
}

void WifiSetup::cancel() {
    if (!active_) {
        return;
    }
    portal_.stop();
    active_ = false;
}

bool WifiSetup::active() const {
    return active_;
}

bool WifiSetup::takeSaved() {
    const bool saved = saved_;
    saved_ = false;
    return saved;
}

}  // namespace gateway
