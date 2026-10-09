#pragma once

#include <random_port.h>

#include <cstdint>

class FakeRandom final : public RandomPort {
public:
    uint32_t next = 0;
    uint32_t lastLo = 0;
    uint32_t lastHi = 0;
    uint32_t calls = 0;

    uint32_t uniform(uint32_t lo, uint32_t hi) override {
        lastLo = lo;
        lastHi = hi;
        calls++;
        return next;
    }
};
