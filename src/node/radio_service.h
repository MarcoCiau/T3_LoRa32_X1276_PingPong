/**
 * @file radio_service.h
 * @brief RadioLib SX1276 adapter for the node. OLED is not part of this type.
 */

#pragma once

#include "radio_port.h"
#include <RadioLib.h>
#include <cstddef>
#include <cstdint>

/**
 * @brief Half-duplex SX1276 that implements RadioPort.
 *
 * begin() programs the lab profile from board_profile.h, including an explicit
 * setFrequency() call. An interrupt sets a flag. poll() is what reads that flag.
 * readPacket() also checks it, so a later check-in loop can call the port alone.
 * This class does not format JSON and does not use the OLED.
 */
class RadioLibRadio final : public RadioPort {
public:
    /** @brief Build the module from the board pin map. Does not touch the SPI bus. */
    RadioLibRadio();

    /**
     * @brief Start SPI and program the lab radio profile.
     * @return Status::Ok, or BadPayload when RadioLib rejects the profile.
     */
    proto::Status begin();

    /**
     * @brief Read the interrupt flag set by the radio ISR.
     *
     * A finished transmit is reported by takeTransmitDone(). A finished receive
     * becomes available to readPacket().
     */
    void poll();

    /**
     * @brief Take the "transmit finished" event.
     * @return true once after the transmit IRQ has been seen by poll().
     */
    bool takeTransmitDone();

    /**
     * @brief Start sending @p data. The bytes are copied and remain valid until the IRQ.
     * @return Status::Ok, Busy when a transmit is already in progress, or BadPayload.
     */
    proto::Status startTransmit(const uint8_t* data, size_t len) override;

    /**
     * @brief Arm the receiver.
     *
     * A second call while already listening leaves a waiting packet in place.
     * @return Status::Ok, Busy while a transmit is on the air, or BadPayload when RadioLib rejects it.
     */
    proto::Status startReceive() override;

    /**
     * @brief Copy one received packet after the receive IRQ.
     * @return Status::Ok, Busy while still listening, Truncated when @p cap is short, or BadPayload.
     */
    proto::Status readPacket(uint8_t* data, size_t cap, size_t* outLen) override;

    /** @brief RSSI of the last copied packet, in dBm. */
    float rssi() const override;

    /** @brief SNR of the last copied packet, in dB. */
    float snr() const override;

private:
    /** @brief What the radio is doing between interrupts. */
    enum class Phase : uint8_t {
        Idle,         ///< Standby. No packet is waiting.
        Transmitting, ///< startTransmit has been accepted.
        Listening,    ///< Receiver is armed.
        PacketReady,  ///< Receive IRQ seen. The packet is still in the radio.
    };

    /** @brief Map a RadioLib status to Status::Ok or BadPayload. */
    static proto::Status fromRadio(int16_t state);

    Module module_;
    SX1276 radio_;
    Phase phase_ = Phase::Idle;
    bool txDone_ = false;
    float rssi_ = 0.0f;
    float snr_ = 0.0f;
    uint8_t txBuf_[proto::kMaxPhyBytes] = {};
};
