/**
 * @file node_main.cpp
 * @brief Node bring-up: one binary transmit, then listen. The ping-pong sketch is unchanged.
 */

#include <Arduino.h>

#include "board_profile.h"
#include "packet_codec.h"
#include "radio_service.h"
#include "security.h"

namespace {

RadioLibRadio radio;
proto::PacketCodec codec;
proto::LabAuthOff auth;

/** @brief Print @p data as lowercase hex. */
void printHex(const uint8_t* data, size_t len) {
    for (size_t index = 0; index < len; ++index) {
        if (data[index] < 0x10) {
            Serial.print('0');
        }
        Serial.print(data[index], HEX);
    }
    Serial.println();
}

}  // namespace

void setup() {
    Serial.begin(115200);
    pinMode(board::kBoardLedPin, OUTPUT);
    digitalWrite(board::kBoardLedPin, LOW);

    Serial.println(F("[node] radio begin"));
    if (radio.begin() != proto::Status::Ok) {
        Serial.println(F("[node] radio fail"));
        while (true) {
            delay(1000);
        }
    }

    Serial.print(F("[node] freq MHz "));
    Serial.println(board::kRadioFrequencyMhz);
    Serial.print(F("[node] bw kHz "));
    Serial.print(board::kRadioBandwidthKhz);
    Serial.print(F(" sf "));
    Serial.print(board::kRadioSpreadingFactor);
    Serial.print(F(" cr 4/"));
    Serial.print(board::kRadioCodingRate);
    Serial.print(F(" sync 0x"));
    Serial.print(board::kRadioSyncWord, HEX);
    Serial.print(F(" power dBm "));
    Serial.println(board::kRadioOutputPowerDbm);

    proto::PacketHeader header = {};
    header.type = proto::MessageType::Telemetry;
    header.srcId = 1;
    header.dstId = proto::kGatewayId;
    header.seq = 1;
    header.counter = 1;

    uint8_t packet[proto::kMaxPhyBytes] = {};
    size_t packetLen = 0;
    if (codec.encode(header, nullptr, 0, auth.tagLen() != 0, nullptr, packet, sizeof(packet), &packetLen) !=
        proto::Status::Ok) {
        Serial.println(F("[node] encode fail"));
        while (true) {
            delay(1000);
        }
    }

    Serial.print(F("[node] tx bytes "));
    Serial.println(packetLen);
    printHex(packet, packetLen);
    if (radio.startTransmit(packet, packetLen) != proto::Status::Ok) {
        Serial.println(F("[node] transmit fail"));
        while (true) {
            delay(1000);
        }
    }
}

void loop() {
    radio.poll();
    if (radio.takeTransmitDone()) {
        digitalWrite(board::kBoardLedPin, HIGH);
        Serial.println(F("[node] transmission finished"));
        if (radio.startReceive() == proto::Status::Ok) {
            Serial.println(F("[node] listening"));
        } else {
            Serial.println(F("[node] listen fail"));
        }
    }

    uint8_t buffer[proto::kMaxPhyBytes] = {};
    size_t length = 0;
    if (radio.readPacket(buffer, sizeof(buffer), &length) != proto::Status::Ok) {
        return;
    }
    Serial.print(F("[node] rx bytes "));
    Serial.print(length);
    Serial.print(F(" rssi "));
    Serial.print(radio.rssi());
    Serial.print(F(" snr "));
    Serial.println(radio.snr());
    printHex(buffer, length);
}
