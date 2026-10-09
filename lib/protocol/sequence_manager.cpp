/**
 * @file sequence_manager.cpp
 * @brief Per-peer seq allocation, retry budget, and duplicate detection.
 *
 * Does not persist a replay counter and does not block on a timer.
 */

#include "sequence_manager.h"

#include "random_port.h"

namespace proto {

SequenceManager::SequenceManager(RandomPort& rng) : rng_(rng) {}

SequenceManager::Tx* SequenceManager::txSlot(uint8_t id) {
    if (id >= kSlots) {
        return nullptr;
    }
    return &tx_[id];
}

SequenceManager::Rx* SequenceManager::rxSlot(uint8_t id) {
    if (id >= kSlots) {
        return nullptr;
    }
    return &rx_[id];
}

Status SequenceManager::beginTransmit(uint8_t dstId, uint16_t* seq) {
    if (seq == nullptr) {
        return Status::BadPayload;
    }
    Tx* tx = txSlot(dstId);
    if (tx == nullptr) {
        return Status::BadDestination;
    }
    tx->seq = static_cast<uint16_t>(tx->seq + 1);
    tx->retriesLeft = kRetryBudget;
    tx->active = true;
    *seq = tx->seq;
    return Status::Ok;
}

Status SequenceManager::onTimeout(uint8_t dstId, RetryResult* result, uint16_t* seq, uint32_t* delayMs) {
    if (result == nullptr || seq == nullptr || delayMs == nullptr) {
        return Status::BadPayload;
    }
    Tx* tx = txSlot(dstId);
    if (tx == nullptr) {
        return Status::BadDestination;
    }
    if (!tx->active) {
        return Status::BadPayload;
    }
    if (tx->retriesLeft == 0) {
        tx->active = false;
        *result = RetryResult::WaitForInterval;
        *seq = tx->seq;
        *delayMs = 0;
        return Status::Ok;
    }

    *delayMs = rng_.uniform(kRetryDelayMinMs, kRetryDelayMaxMs);
    tx->retriesLeft = static_cast<uint8_t>(tx->retriesLeft - 1);
    *seq = tx->seq;
    *result = RetryResult::Again;
    return Status::Ok;
}

Status SequenceManager::accept(uint8_t srcId, uint16_t seq) {
    Rx* rx = rxSlot(srcId);
    if (rx == nullptr) {
        return Status::BadDestination;
    }
    if (rx->seen && rx->seq == seq) {
        return Status::Duplicate;
    }
    rx->seen = true;
    rx->seq = seq;
    return Status::Ok;
}

}  // namespace proto
