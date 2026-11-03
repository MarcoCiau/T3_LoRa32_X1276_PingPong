/**
 * @file button_gesture.cpp
 * @brief Duration buckets for the shared user button.
 *
 * A release in a gap returns None. Reaching a hold threshold consumes the press,
 * so the later release is not a click and, on the gateway, is not Wi-Fi setup.
 */

#include "button_gesture.h"

ButtonGesture::ButtonGesture(ButtonRole role) : role_(role) {}

Gesture ButtonGesture::poll(bool pressed, uint32_t now) {
    if (pressed != raw_) {
        raw_ = pressed;
        debounceStart_ = now;
        debouncing_ = true;
    }

    if (debouncing_) {
        if (static_cast<uint32_t>(now - debounceStart_) < kDebounceMs) {
            return Gesture::None;
        }
        debouncing_ = false;
        if (raw_ == stable_) {
            return Gesture::None;
        }
        stable_ = raw_;
        if (stable_) {
            pressedAt_ = now;
            committed_ = false;
            return Gesture::None;
        }
        if (committed_) {
            return Gesture::None;
        }
        const uint32_t held = static_cast<uint32_t>(now - pressedAt_);
        if (held < kClickMaxMs) {
            return Gesture::Click;
        }
        if (role_ == ButtonRole::Gateway && held >= kGatewayWifiMs && held < kGatewayFactoryMs) {
            return Gesture::WifiSetup;
        }
        return Gesture::None;
    }

    if (stable_ && !committed_) {
        const uint32_t held = static_cast<uint32_t>(now - pressedAt_);
        if (role_ == ButtonRole::Node && held >= kNodeUnpairMs) {
            committed_ = true;
            return Gesture::Unpair;
        }
        if (role_ == ButtonRole::Gateway && held >= kGatewayFactoryMs) {
            committed_ = true;
            return Gesture::FactoryReset;
        }
    }
    return Gesture::None;
}

HoldHint ButtonGesture::hint(uint32_t now) const {
    if (!stable_) {
        return HoldHint::None;
    }
    const uint32_t held = static_cast<uint32_t>(now - pressedAt_);
    if (role_ == ButtonRole::Node) {
        return held >= kNodeUnpairMs ? HoldHint::Unpair : HoldHint::None;
    }
    if (held >= kGatewayFactoryMs) {
        return HoldHint::Reset;
    }
    if (held >= kGatewayWifiMs) {
        return HoldHint::Wifi;
    }
    return HoldHint::None;
}
