/**
 * @file radio_service.cpp
 * @brief SX1276 transmit and receive driven by one IRQ flag.
 *
 * The ISR only sets the flag. poll() is the first place that flag is read.
 */

#include "radio_service.h"

#include "board_profile.h"

#include <SPI.h>

#include <cstring>

namespace {

/** @brief Set from the radio ISR. poll() is the reader. */
volatile bool radioIrqSeen = false;

/** @brief RadioLib trampoline. This is the one interrupt entry for the node radio. */
void IRAM_ATTR onRadioIrq() {
    radioIrqSeen = true;
}

}  // namespace

RadioLibRadio::RadioLibRadio()
    : module_(board::kRadioCsPin, board::kRadioDio0Pin, board::kRadioRstPin, board::kRadioDio1Pin),
      radio_(&module_) {}

proto::Status RadioLibRadio::fromRadio(int16_t state) {
    if (state == RADIOLIB_ERR_NONE) {
        return proto::Status::Ok;
    }
    return proto::Status::BadPayload;
}

proto::Status RadioLibRadio::begin() {
    SPI.begin(board::kRadioSclkPin, board::kRadioMisoPin, board::kRadioMosiPin);

    int16_t state = radio_.begin(board::kRadioFrequencyMhz,
                                 board::kRadioBandwidthKhz,
                                 board::kRadioSpreadingFactor,
                                 board::kRadioCodingRate,
                                 board::kRadioSyncWord,
                                 board::kRadioOutputPowerDbm,
                                 board::kRadioPreambleLength);
    if (state != RADIOLIB_ERR_NONE) {
        return fromRadio(state);
    }

    state = radio_.setBandwidth(board::kRadioBandwidthKhz);
    if (state != RADIOLIB_ERR_NONE) {
        return fromRadio(state);
    }
    state = radio_.setSpreadingFactor(board::kRadioSpreadingFactor);
    if (state != RADIOLIB_ERR_NONE) {
        return fromRadio(state);
    }
    state = radio_.setCodingRate(board::kRadioCodingRate);
    if (state != RADIOLIB_ERR_NONE) {
        return fromRadio(state);
    }
    state = radio_.setSyncWord(board::kRadioSyncWord);
    if (state != RADIOLIB_ERR_NONE) {
        return fromRadio(state);
    }
    state = radio_.setOutputPower(board::kRadioOutputPowerDbm);
    if (state != RADIOLIB_ERR_NONE) {
        return fromRadio(state);
    }
    state = radio_.setPreambleLength(board::kRadioPreambleLength);
    if (state != RADIOLIB_ERR_NONE) {
        return fromRadio(state);
    }
    state = radio_.setFrequency(board::kRadioFrequencyMhz);
    if (state != RADIOLIB_ERR_NONE) {
        return fromRadio(state);
    }

    radio_.setPacketSentAction(onRadioIrq);
    radio_.setPacketReceivedAction(onRadioIrq);
    radioIrqSeen = false;
    phase_ = Phase::Idle;
    txDone_ = false;
    return proto::Status::Ok;
}

void RadioLibRadio::poll() {
    if (!radioIrqSeen) {
        return;
    }
    radioIrqSeen = false;

    if (phase_ == Phase::Transmitting) {
        (void)radio_.finishTransmit();
        phase_ = Phase::Idle;
        txDone_ = true;
        return;
    }
    if (phase_ == Phase::Listening) {
        phase_ = Phase::PacketReady;
    }
}

bool RadioLibRadio::takeTransmitDone() {
    if (!txDone_) {
        return false;
    }
    txDone_ = false;
    return true;
}

proto::Status RadioLibRadio::startTransmit(const uint8_t* data, size_t len) {
    if (phase_ == Phase::Transmitting) {
        return proto::Status::Busy;
    }
    if (data == nullptr || len == 0 || len > sizeof(txBuf_)) {
        return proto::Status::BadPayload;
    }
    std::memcpy(txBuf_, data, len);
    radioIrqSeen = false;
    txDone_ = false;
    const int16_t state = radio_.startTransmit(txBuf_, len);
    if (state != RADIOLIB_ERR_NONE) {
        phase_ = Phase::Idle;
        return fromRadio(state);
    }
    phase_ = Phase::Transmitting;
    return proto::Status::Ok;
}

proto::Status RadioLibRadio::startReceive() {
    if (phase_ == Phase::Transmitting) {
        return proto::Status::Busy;
    }
    if (phase_ == Phase::Listening || phase_ == Phase::PacketReady) {
        return proto::Status::Ok;
    }
    radioIrqSeen = false;
    const int16_t state = radio_.startReceive();
    if (state != RADIOLIB_ERR_NONE) {
        phase_ = Phase::Idle;
        return fromRadio(state);
    }
    phase_ = Phase::Listening;
    return proto::Status::Ok;
}

proto::Status RadioLibRadio::readPacket(uint8_t* data, size_t cap, size_t* outLen) {
    poll();
    if (data == nullptr || outLen == nullptr || phase_ != Phase::PacketReady) {
        return proto::Status::Busy;
    }

    const size_t length = radio_.getPacketLength();
    if (length > cap) {
        return proto::Status::Truncated;
    }
    const int16_t state = radio_.readData(data, length);
    if (state != RADIOLIB_ERR_NONE) {
        return fromRadio(state);
    }
    *outLen = length;
    rssi_ = radio_.getRSSI();
    snr_ = radio_.getSNR();

    const int16_t listening = radio_.startReceive();
    if (listening != RADIOLIB_ERR_NONE) {
        phase_ = Phase::Idle;
        return fromRadio(listening);
    }
    phase_ = Phase::Listening;
    return proto::Status::Ok;
}

float RadioLibRadio::rssi() const {
    return rssi_;
}

float RadioLibRadio::snr() const {
    return snr_;
}
