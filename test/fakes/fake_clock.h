/**
 * @file fake_clock.h
 * @brief ClockPort that stays still until the test advances it.
 */

#pragma once

#include <clock_port.h>

#include <cstdint>

/**
 * @brief Manual millisecond clock.
 *
 * millis() returns now. advance() adds to it. Time does not move during poll().
 */
class FakeClock final : public ClockPort {
public:
    uint32_t now = 0;

    uint32_t millis() const override { return now; }

    /** @brief Move the clock forward by @p ms. */
    void advance(uint32_t ms) { now += ms; }
};
