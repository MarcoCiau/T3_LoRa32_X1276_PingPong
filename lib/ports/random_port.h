/**
 * @file random_port.h
 * @brief Jitter source used by the protocol retry delay.
 */

#pragma once

#include <cstdint>

/**
 * @brief Inclusive uniform random source.
 *
 * Device code will wrap the ESP32 RNG. Host tests supply a scripted double.
 * This header does not include Arduino.h.
 */
class RandomPort {
public:
    virtual ~RandomPort() = default;

    /**
     * @brief Return one value in the closed range [@p lo, @p hi].
     * @param lo Inclusive lower bound.
     * @param hi Inclusive upper bound. Callers pass hi >= lo.
     * @return A value from @p lo through @p hi.
     */
    virtual uint32_t uniform(uint32_t lo, uint32_t hi) = 0;
};
