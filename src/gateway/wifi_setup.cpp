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
    if (portal_.takePosted(ssid, sizeof(ssid), password, sizeof(password))) {
        if (ssid[0] != '\0' && secrets_.save(ssid, password)) {
            portal_.stop();
            active_ = false;
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

}  // namespace gateway
