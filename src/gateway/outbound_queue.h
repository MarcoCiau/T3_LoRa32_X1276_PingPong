/**
 * @file outbound_queue.h
 * @brief Bounded uplink queue. Telemetry yields space. A command result does not.
 */

#pragma once

#include "outbound_buffer.h"

#include <cstdint>

namespace gateway {

/**
 * @brief Sixteen uplink records in arrival order.
 *
 * push() appends while a slot is free. When the queue is full, the oldest telemetry
 * record is removed so the new record can fit, and droppedTelemetry() increases.
 * A telemetry push is rejected when every slot is a command result. A command result
 * is never removed to store telemetry. This class does not allocate and does not
 * open a socket.
 */
class OutboundQueue final : public OutboundBuffer {
public:
    /** @brief Maximum records stored at once. */
    static constexpr uint8_t kCapacity = 16;

    /**
     * @brief Append @p record, dropping the oldest telemetry when the queue is full.
     * @param record Telemetry or command result.
     * @return Status::Ok when @p record is stored. Status::Busy when it is not stored.
     */
    proto::Status push(const UplinkRecord& record) override;

    /**
     * @brief Remove the oldest remaining record.
     * @param out Receives that record. Required.
     * @return true when a record was removed.
     */
    bool pop(UplinkRecord* out) override;

    /** @brief Records currently stored. */
    uint8_t size() const;

    /** @brief Telemetry records discarded because the queue could not keep them. */
    uint32_t droppedTelemetry() const;

    /** @brief Drop every stored record. Does not change droppedTelemetry(). */
    void clear();

private:
    /** @brief Record at logical position @p index, where 0 is the next pop. */
    UplinkRecord& at(uint8_t index);
    /** @brief Record at logical position @p index, where 0 is the next pop. */
    const UplinkRecord& at(uint8_t index) const;

    /**
     * @brief Logical index of the oldest telemetry, or kCapacity when there is none.
     */
    uint8_t oldestTelemetry() const;

    /** @brief Close the gap at logical @p index. @p index must be inside the queue. */
    void removeAt(uint8_t index);

    UplinkRecord records_[kCapacity] = {};
    uint8_t head_ = 0;
    uint8_t size_ = 0;
    uint32_t droppedTelemetry_ = 0;
};

}  // namespace gateway
