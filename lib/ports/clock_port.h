/**
 * @file clock_port.h
 * @brief Millisecond clock for report intervals and the ACK window.
 */

#pragma once

#include <cstdint>

/**
 * @brief Monotonic millisecond time.
 *
 * Callers compare values with unsigned subtraction. Host tests advance the clock explicitly.
 */
class ClockPort {
public:
    virtual ~ClockPort() = default;

    /** @brief Milliseconds since an arbitrary start. The value may wrap. */
    virtual uint32_t millis() const = 0;
};
