/**
 * @file wifi_setup.h
 * @brief Gateway Wi-Fi credential setup started by the 5 s button release.
 *
 * The access point and the stored secrets are ports. Host tests supply doubles.
 * This class does not assign a node id and does not erase the pairing table.
 */

#pragma once

#include <cstddef>
#include <cstdint>

class ClockPort;

namespace gateway {

class WifiLink;

/**
 * @brief SoftAP that collects one SSID and password.
 */
class WifiPortal {
public:
    virtual ~WifiPortal() = default;

    /**
     * @brief Start the access point and the form.
     * @param apName Access point name. Not null.
     */
    virtual void start(const char* apName) = 0;

    /** @brief Stop the access point. Stored credentials are left untouched. */
    virtual void stop() = 0;

    /** @brief Service the form. A host double may do nothing. */
    virtual void poll() = 0;

    /**
     * @brief Copy a posted form once.
     * @param ssid Receives the SSID. Required.
     * @param ssidCap Capacity of @p ssid, including the NUL.
     * @param password Receives the password. Required.
     * @param passwordCap Capacity of @p password, including the NUL.
     * @return true when a form was waiting. The form is then forgotten.
     */
    virtual bool takePosted(char* ssid, size_t ssidCap, char* password, size_t passwordCap) = 0;
};

/**
 * @brief SSID and password kept outside the 16-byte record store.
 */
class WifiSecrets {
public:
    virtual ~WifiSecrets() = default;

    /**
     * @brief Copy the stored credentials.
     * @return false when none are stored or a pointer is null.
     */
    virtual bool load(char* ssid, size_t ssidCap, char* password, size_t passwordCap) = 0;

    /**
     * @brief Replace the stored credentials.
     * @param ssid Station name. Empty is rejected.
     * @param password Station secret. Empty is an open network.
     * @return false when @p ssid is null or empty.
     */
    virtual bool save(const char* ssid, const char* password) = 0;

    /** @brief Delete the stored credentials. */
    virtual bool clear() = 0;
};

/**
 * @brief Five-minute setup session.
 *
 * begin() starts the portal. A click should call cancel(). A posted form is saved
 * and then WifiLink::reconfigure() joins that network. Cancel keeps the previous secrets.
 */
class WifiSetup {
public:
    /** @brief Setup ends on its own after this long with no form, in milliseconds. */
    static constexpr uint32_t kTimeoutMs = 300000;
    /** @brief Longest SSID stored, excluding the NUL. */
    static constexpr size_t kSsidCap = 33;
    /** @brief Longest password stored, excluding the NUL. */
    static constexpr size_t kPasswordCap = 65;

    /**
     * @brief Bind the session.
     * @param clock Millisecond clock.
     * @param portal Access point. Must outlive this object.
     * @param secrets Credential store. Must outlive this object.
     * @param wifi Station link updated after a successful form.
     * @param apName Access point name. Must stay valid for the session.
     */
    WifiSetup(ClockPort& clock, WifiPortal& portal, WifiSecrets& secrets, WifiLink& wifi, const char* apName);

    /** @brief Start setup. A second call while active is ignored. */
    void begin();

    /** @brief Save a posted form, or stop after kTimeoutMs. */
    void poll();

    /** @brief Stop the portal and keep the previous credentials. */
    void cancel();

    /** @brief True between begin() and a form, cancel, or timeout. */
    bool active() const;

private:
    ClockPort& clock_;
    WifiPortal& portal_;
    WifiSecrets& secrets_;
    WifiLink& wifi_;
    const char* apName_;
    bool active_ = false;
    uint32_t startedAt_ = 0;
};

/**
 * @brief NVS copy of the station credentials. Defined in the device translation unit.
 */
WifiSecrets& deviceWifiSecrets();

/**
 * @brief SoftAP form. Defined in the device translation unit.
 */
WifiPortal& deviceWifiPortal();

}  // namespace gateway
