/**
 * @file fake_random.h
 * @brief Scripted RandomPort for host tests.
 */

#pragma once

#include <random_port.h>

#include <cstdint>

/**
 * @brief Returns a chosen value and records the requested bounds.
 *
 * next is returned as-is so a test can see the delay the caller received.
 * lastLo and lastHi show the range SequenceManager requested.
 */
class FakeRandom final : public RandomPort {
public:
    uint32_t next = 0;    ///< Value returned by the next uniform call.
    uint32_t lastLo = 0;  ///< lo from the latest uniform call.
    uint32_t lastHi = 0;  ///< hi from the latest uniform call.
    uint32_t calls = 0;   ///< Number of uniform calls.

    /**
     * @brief Record [@p lo, @p hi] and return next.
     * @param lo Inclusive lower bound requested by the caller.
     * @param hi Inclusive upper bound requested by the caller.
     * @return next, including a value outside [@p lo, @p hi] when the test sets one.
     */
    uint32_t uniform(uint32_t lo, uint32_t hi) override {
        lastLo = lo;
        lastHi = hi;
        calls++;
        return next;
    }
};
