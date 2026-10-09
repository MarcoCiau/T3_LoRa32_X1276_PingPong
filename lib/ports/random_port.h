#pragma once

#include <cstdint>

// Inclusive bounds.
class RandomPort {
public:
    virtual ~RandomPort() = default;
    virtual uint32_t uniform(uint32_t lo, uint32_t hi) = 0;
};
