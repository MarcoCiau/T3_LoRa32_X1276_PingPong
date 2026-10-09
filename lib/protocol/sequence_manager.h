#pragma once

#include "message_types.h"

#include <cstdint>

class RandomPort;

namespace proto {

enum class RetryResult : uint8_t {
    Again = 0,
    WaitForInterval = 1,
};

class SequenceManager {
public:
    static constexpr uint8_t kRetryBudget = 3;
    static constexpr uint32_t kRetryDelayMinMs = 200;
    static constexpr uint32_t kRetryDelayMaxMs = 800;

    explicit SequenceManager(RandomPort& rng);

    Status beginTransmit(uint8_t dstId, uint16_t* seq);
    Status onTimeout(uint8_t dstId, RetryResult* result, uint16_t* seq, uint32_t* delayMs);
    Status accept(uint8_t srcId, uint16_t seq);

private:
    struct Tx {
        uint16_t seq;
        uint8_t retriesLeft;
        bool active;
    };
    struct Rx {
        uint16_t seq;
        bool seen;
    };

    static constexpr uint8_t kSlots = kMaxNodeId + 1;

    Tx* txSlot(uint8_t id);
    Rx* rxSlot(uint8_t id);

    RandomPort& rng_;
    Tx tx_[kSlots] = {};
    Rx rx_[kSlots] = {};
};

}  // namespace proto
