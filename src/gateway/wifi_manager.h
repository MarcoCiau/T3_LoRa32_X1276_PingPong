/**
 * @file wifi_manager.h
 * @brief Gateway Wi-Fi link. Reconnect is scheduled here and does not run on the radio.
 */

#pragma once

#include "clock_port.h"

#include <cstdint>

namespace gateway {

/** @brief First reconnect wait after an attempt, in milliseconds. */
constexpr uint32_t kWifiBackoffMinMs = 1000;

/** @brief Longest reconnect wait, in milliseconds. */
constexpr uint32_t kWifiBackoffMaxMs = 60000;

/**
 * @brief Station SSID. Empty leaves Wi-Fi down so the radio still runs in the lab.
 *
 * Set this to the access point name before a gateway that should join a network.
 */
constexpr char kWifiSsid[] = "";

/** @brief Station password. May be empty for an open access point. */
constexpr char kWifiPassword[] = "";

/**
 * @brief Association attempts for WifiLink.
 *
 * beginConnect() must return without waiting for an address. Device code calls
 * WiFi.begin(). Host tests use a double that records the call.
 */
class WifiStation {
public:
    virtual ~WifiStation() = default;

    /** @brief True when the station has an association. */
    virtual bool isConnected() const = 0;

    /**
     * @brief Start joining @p ssid. Return before the join finishes.
     * @param ssid Access point name. Not null.
     * @param password Access point secret. Not null. May be empty.
     */
    virtual void beginConnect(const char* ssid, const char* password) = 0;
};

/**
 * @brief STA link with a doubling reconnect gap from 1 s to 60 s.
 *
 * begin() and poll() do not delay. A disconnected poll either starts one attempt
 * or returns because the current gap has not elapsed. This class does not publish
 * MQTT and does not include the radio.
 */
class WifiLink {
public:
    /**
     * @brief Bind the clock, the station, and the credentials.
     *
     * @p clock, @p station, @p ssid, and @p password must outlive this object.
     * An empty or null @p ssid disables the link.
     */
    WifiLink(ClockPort& clock, WifiStation& station, const char* ssid, const char* password);

    /** @brief Start the first join when a station SSID is configured. */
    void begin();

    /**
     * @brief Continue the link.
     *
     * A connected station resets the gap to kWifiBackoffMinMs. A disconnected
     * station gets a new attempt only after the current gap. The call returns
     * in either case.
     */
    void poll();

    /** @brief True when the link is enabled and the station reports an association. */
    bool connected() const;

private:
    /** @brief Start one join and arm the next gap. */
    void startAttempt(uint32_t now);

    ClockPort& clock_;
    WifiStation& station_;
    const char* ssid_;
    const char* password_;
    bool enabled_ = false;
    bool attemptPending_ = false;
    uint32_t gapMs_ = kWifiBackoffMinMs;
    uint32_t waitMs_ = kWifiBackoffMinMs;
    uint32_t lastAttemptMs_ = 0;
};

/**
 * @brief WifiStation that calls the ESP32 Arduino Wi-Fi stack.
 *
 * The methods live in a device translation unit. Host tests do not construct this type.
 */
class EspWifiStation final : public WifiStation {
public:
    /** @brief True when WiFi.status() is WL_CONNECTED. */
    bool isConnected() const override;

    /** @brief Switch to STA mode if needed and call WiFi.begin(). */
    void beginConnect(const char* ssid, const char* password) override;
};

}  // namespace gateway
