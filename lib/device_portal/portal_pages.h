/**
 * @file portal_pages.h
 * @brief Setup pages for the gateway access point.
 *
 * Home shows device info. Device config and firmware update are the two actions.
 * The strings live in flash. This library does not open the radio or the network.
 */

#pragma once

#include <Arduino.h>

namespace device_portal {

extern const char kStyle[] PROGMEM;
extern const char kHome[] PROGMEM;
extern const char kConfig[] PROGMEM;
extern const char kOta[] PROGMEM;
extern const char kSaved[] PROGMEM;
extern const char kSsidRequired[] PROGMEM;

}  // namespace device_portal
