/**
 * @file board_profile.h
 * @brief LilyGO T3 LoRa32 V1.6 pins and the lab radio profile shared by node and gateway.
 *
 * The pin numbers are this board's, not the Heltec defaults selected by the
 * PlatformIO board id. kRadioFrequencyMhz is applied with setFrequency().
 * The old sketch declared 868 MHz and never programmed it; the radio that
 * has been on the air uses 434.0 MHz. Change this header for a field band,
 * and flash both ends from the same values.
 */

#pragma once

#include <cstdint>

namespace board {

/** @brief OLED I2C data. The radio adapter does not use this pin. */
constexpr uint8_t kOledSdaPin = 21;
/** @brief OLED I2C clock. The radio adapter does not use this pin. */
constexpr uint8_t kOledSclPin = 22;

/** @brief SX1276 SPI clock. */
constexpr uint8_t kRadioSclkPin = 5;
/** @brief SX1276 SPI MISO. */
constexpr uint8_t kRadioMisoPin = 19;
/** @brief SX1276 SPI MOSI. */
constexpr uint8_t kRadioMosiPin = 27;
/** @brief SX1276 chip select. */
constexpr uint8_t kRadioCsPin = 18;
/** @brief SX1276 reset. */
constexpr uint8_t kRadioRstPin = 23;
/** @brief SX1276 DIO0. Packet sent and packet received IRQ. */
constexpr uint8_t kRadioDio0Pin = 26;
/** @brief SX1276 DIO1. Passed to RadioLib as the second IRQ pin. */
constexpr uint8_t kRadioDio1Pin = 33;
/** @brief SX1276 DIO2. Present on the board and not passed to Module. */
constexpr uint8_t kRadioDio2Pin = 32;

/** @brief On-board LED, active high. */
constexpr uint8_t kBoardLedPin = 25;

/** @brief Lab center frequency in MHz. setFrequency() programs this value. */
constexpr float kRadioFrequencyMhz = 434.0f;
/** @brief LoRa bandwidth in kHz. */
constexpr float kRadioBandwidthKhz = 125.0f;
/** @brief LoRa spreading factor. */
constexpr uint8_t kRadioSpreadingFactor = 9;
/** @brief Coding-rate denominator. 7 means 4/7. */
constexpr uint8_t kRadioCodingRate = 7;
/** @brief LoRa sync word. 0x34 is reserved for LoRaWAN. */
constexpr uint8_t kRadioSyncWord = 0x12;
/** @brief Transmit power in dBm. */
constexpr int8_t kRadioOutputPowerDbm = 17;
/** @brief Preamble length in symbols, before RadioLib's 4.25-symbol overhead. */
constexpr uint16_t kRadioPreambleLength = 8;

}  // namespace board
