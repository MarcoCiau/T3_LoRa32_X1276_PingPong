/**
 * @file button_gesture.h
 * @brief One-button press classifier for the node and the gateway.
 *
 * The press is timed from the debounced falling edge. Nothing runs on that edge.
 * A hold that reaches its threshold is not also a click.
 */

#pragma once

#include <cstdint>

/**
 * @brief Which thresholds this button uses.
 */
enum class ButtonRole : uint8_t {
    Node,     ///< Click, or unpair at 3 s.
    Gateway,  ///< Click, Wi-Fi setup on a 5 s release, factory reset at 10 s.
};

/**
 * @brief The one outcome of a press.
 */
enum class Gesture : uint8_t {
    None,          ///< No commit on this sample.
    Click,         ///< Released before 1 s.
    Unpair,        ///< Node hold reached 3 s.
    WifiSetup,     ///< Gateway released between 5 s and 10 s.
    FactoryReset,  ///< Gateway hold reached 10 s.
};

/**
 * @brief What the held button is about to commit.
 */
enum class HoldHint : uint8_t {
    None,   ///< Below the next threshold.
    Unpair, ///< Node, 3 s or more.
    Wifi,   ///< Gateway, 5 s up to 10 s.
    Reset,  ///< Gateway, 10 s or more.
};

/**
 * @brief Classify one active-low button from sampled levels.
 *
 * poll() takes the raw level, true when the button is down. Debounce is 30 ms.
 * This class does not read a pin and does not transmit.
 */
class ButtonGesture {
public:
    /** @brief Samples that must agree before a level is accepted, in milliseconds. */
    static constexpr uint32_t kDebounceMs = 30;
    /** @brief A release before this is a click, in milliseconds. */
    static constexpr uint32_t kClickMaxMs = 1000;
    /** @brief Node unpair commits at this hold, in milliseconds. */
    static constexpr uint32_t kNodeUnpairMs = 3000;
    /** @brief Gateway Wi-Fi setup is a release at or after this, in milliseconds. */
    static constexpr uint32_t kGatewayWifiMs = 5000;
    /** @brief Gateway factory reset commits at this hold, in milliseconds. */
    static constexpr uint32_t kGatewayFactoryMs = 10000;

    /**
     * @brief Choose the node or gateway thresholds.
     * @param role Which device owns the button.
     */
    explicit ButtonGesture(ButtonRole role);

    /**
     * @brief Advance the classifier.
     * @param pressed True when the raw pin is down.
     * @param now Millisecond clock. Unsigned subtraction handles wrap.
     * @return The gesture that committed on this sample, or None.
     */
    Gesture poll(bool pressed, uint32_t now);

    /**
     * @brief Hint for the LED while the button is down.
     * @param now Millisecond clock.
     * @return The outcome a continued hold would commit.
     */
    HoldHint hint(uint32_t now) const;

private:
    ButtonRole role_;
    bool raw_ = false;
    bool stable_ = false;
    bool debouncing_ = false;
    bool committed_ = false;
    uint32_t debounceStart_ = 0;
    uint32_t pressedAt_ = 0;
};
