/**
 * @file fake_outbound_buffer.h
 * @brief OutboundBuffer that keeps the uplinks a test wants to inspect.
 */

#pragma once

#include "outbound_buffer.h"

#include <cstdint>

/**
 * @brief Fixed list of uplinks. push() copies the record and does not publish it.
 */
class FakeOutboundBuffer final : public gateway::OutboundBuffer {
public:
    /** @brief How many records push() will store. */
    static constexpr uint8_t kCapacity = 8;

    uint8_t count = 0;
    gateway::UplinkRecord records[kCapacity] = {};

    /**
     * @brief Copy @p record into records.
     * @return Status::Ok, or Busy when count is already kCapacity.
     */
    proto::Status push(const gateway::UplinkRecord& record) override {
        if (count >= kCapacity) {
            return proto::Status::Busy;
        }
        records[count] = record;
        count = static_cast<uint8_t>(count + 1);
        return proto::Status::Ok;
    }

    /**
     * @brief Remove the oldest record.
     * @param out Receives that record. Required.
     * @return true when a record was removed.
     */
    bool pop(gateway::UplinkRecord* out) override {
        if (out == nullptr || count == 0) {
            return false;
        }
        *out = records[0];
        for (uint8_t index = 1; index < count; ++index) {
            records[index - 1] = records[index];
        }
        count = static_cast<uint8_t>(count - 1);
        return true;
    }
};
