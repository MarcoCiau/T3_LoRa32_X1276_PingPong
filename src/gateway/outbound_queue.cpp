/**
 * @file outbound_queue.cpp
 * @brief Fixed uplink queue that drops the oldest telemetry to admit a newer record.
 */

#include "outbound_queue.h"

namespace gateway {

proto::Status OutboundQueue::push(const UplinkRecord& record) {
    if (size_ < kCapacity) {
        at(size_) = record;
        size_ = static_cast<uint8_t>(size_ + 1);
        return proto::Status::Ok;
    }

    const uint8_t drop = oldestTelemetry();
    if (drop >= kCapacity) {
        if (record.kind == UplinkKind::Telemetry) {
            droppedTelemetry_ += 1;
        }
        return proto::Status::Busy;
    }

    removeAt(drop);
    droppedTelemetry_ += 1;
    at(size_) = record;
    size_ = static_cast<uint8_t>(size_ + 1);
    return proto::Status::Ok;
}

bool OutboundQueue::pop(UplinkRecord* out) {
    if (out == nullptr || size_ == 0) {
        return false;
    }
    *out = at(0);
    head_ = static_cast<uint8_t>((head_ + 1) % kCapacity);
    size_ = static_cast<uint8_t>(size_ - 1);
    return true;
}

uint8_t OutboundQueue::size() const {
    return size_;
}

uint32_t OutboundQueue::droppedTelemetry() const {
    return droppedTelemetry_;
}

UplinkRecord& OutboundQueue::at(uint8_t index) {
    return records_[(static_cast<uint8_t>(head_ + index)) % kCapacity];
}

const UplinkRecord& OutboundQueue::at(uint8_t index) const {
    return records_[(static_cast<uint8_t>(head_ + index)) % kCapacity];
}

uint8_t OutboundQueue::oldestTelemetry() const {
    for (uint8_t index = 0; index < size_; ++index) {
        if (at(index).kind == UplinkKind::Telemetry) {
            return index;
        }
    }
    return kCapacity;
}

void OutboundQueue::removeAt(uint8_t index) {
    for (uint8_t cursor = index; static_cast<uint8_t>(cursor + 1) < size_; ++cursor) {
        at(cursor) = at(static_cast<uint8_t>(cursor + 1));
    }
    size_ = static_cast<uint8_t>(size_ - 1);
}

}  // namespace gateway
