/**
 * @file fake_radio.h
 * @brief Scripted RadioPort that records the last transmit and holds one receive packet.
 */

#pragma once

#include <radio_port.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

/**
 * @brief Half-duplex radio double.
 *
 * startTransmit stores the bytes and leaves receive disarmed.
 * queueRx holds one packet. A second queueRx replaces it. readPacket copies that
 * packet once while the receiver is armed, then clears it.
 */
class FakeRadio final : public RadioPort {
public:
    static constexpr size_t kMaxBytes = 64;

    proto::Status startTransmit(const uint8_t* data, size_t len) override {
        if ((len > 0 && data == nullptr) || len > kMaxBytes) {
            return proto::Status::BadPayload;
        }
        if (len > 0) {
            std::memcpy(lastTx_, data, len);
        }
        lastTxLen_ = len;
        txCount_++;
        listening_ = false;
        return proto::Status::Ok;
    }

    proto::Status startReceive() override {
        listening_ = true;
        return proto::Status::Ok;
    }

    proto::Status readPacket(uint8_t* data, size_t cap, size_t* outLen) override {
        if (!listening_ || data == nullptr || outLen == nullptr) {
            return proto::Status::Busy;
        }
        if (!hasRx_) {
            return proto::Status::Busy;
        }
        if (cap < rxLen_) {
            return proto::Status::Truncated;
        }
        if (rxLen_ > 0) {
            std::memcpy(data, rx_, rxLen_);
        }
        *outLen = rxLen_;
        hasRx_ = false;
        return proto::Status::Ok;
    }

    float rssi() const override { return rssi_; }
    float snr() const override { return snr_; }

    /**
     * @brief Queue one packet for the next armed readPacket.
     * @param data Packet bytes. May be null when @p len is 0.
     * @param len Size of @p data. Longer than kMaxBytes is ignored.
     */
    void queueRx(const uint8_t* data, size_t len) {
        if (len > kMaxBytes || (len > 0 && data == nullptr)) {
            return;
        }
        if (len > 0) {
            std::memcpy(rx_, data, len);
        }
        rxLen_ = len;
        hasRx_ = true;
    }

    /** @brief Bytes from the latest startTransmit. */
    const uint8_t* txData() const { return lastTx_; }
    /** @brief Size of txData(). */
    size_t txLen() const { return lastTxLen_; }
    /** @brief How many transmits were accepted. */
    uint16_t txCount() const { return txCount_; }

private:
    uint8_t lastTx_[kMaxBytes] = {};
    size_t lastTxLen_ = 0;
    uint16_t txCount_ = 0;
    bool listening_ = false;
    bool hasRx_ = false;
    uint8_t rx_[kMaxBytes] = {};
    size_t rxLen_ = 0;
    float rssi_ = -40.0f;
    float snr_ = 8.0f;
};
