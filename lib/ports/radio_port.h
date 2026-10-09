/**
 * @file radio_port.h
 * @brief Half-duplex radio operations used by the check-in state machine.
 */

#pragma once

#include "../protocol/message_types.h"

#include <cstddef>
#include <cstdint>

/**
 * @brief Transmit and receive one packet at a time.
 *
 * Device code wraps RadioLib. Host tests use FakeRadio. This header does not include RadioLib.
 * Status::Busy from readPacket means the receiver is armed and no packet is waiting.
 */
class RadioPort {
public:
    virtual ~RadioPort() = default;

    /**
     * @brief Start sending @p data.
     * @param data Packet bytes. Required when @p len is not 0.
     * @param len Number of bytes to send.
     * @return Status::Ok when the send is accepted.
     */
    virtual proto::Status startTransmit(const uint8_t* data, size_t len) = 0;

    /**
     * @brief Arm the receiver after a transmit.
     * @return Status::Ok when the radio is listening.
     */
    virtual proto::Status startReceive() = 0;

    /**
     * @brief Copy one received packet, if one is waiting.
     * @param data Destination buffer.
     * @param cap Capacity of @p data.
     * @param outLen Receives the packet size on success.
     * @return Status::Ok when a packet was copied. Status::Busy when still listening with nothing queued.
     */
    virtual proto::Status readPacket(uint8_t* data, size_t cap, size_t* outLen) = 0;

    /** @brief RSSI of the last received packet, in dBm. */
    virtual float rssi() const = 0;

    /** @brief SNR of the last received packet, in dB. */
    virtual float snr() const = 0;
};
