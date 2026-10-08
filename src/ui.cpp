#include "ui.h"

#include <U8g2lib.h>
#include <Wire.h>

#include <stdio.h>
#include <string.h>

// T3 LoRa32 V1.6: SSD1306 on I2C, SDA=21 SCL=22, address 0x3C, no reset pin.
static constexpr uint8_t kOledSda = 21;
static constexpr uint8_t kOledScl = 22;
static constexpr uint8_t kOledAddr7 = 0x3C;

static U8G2_SSD1306_128X64_NONAME_F_HW_I2C display(
    U8G2_R0, U8X8_PIN_NONE, kOledScl, kOledSda);

Ui ui;

void Ui::copyText(char *dst, size_t len, const char *src)
{
    if (src == nullptr) {
        dst[0] = '\0';
        return;
    }
    strncpy(dst, src, len - 1);
    dst[len - 1] = '\0';
}

bool Ui::begin()
{
    display.setI2CAddress(kOledAddr7 << 1);
    display.begin();
    display.setFont(u8g2_font_6x10_tf);
    display.setFontMode(0);

    Wire.beginTransmission(kOledAddr7);
    ready_ = (Wire.endTransmission() == 0);
    if (!ready_) {
        return false;
    }

    copyText(status_, sizeof(status_), "Ready");
    paint();
    return true;
}

void Ui::setRole(const char *role)
{
    if (!ready_) {
        return;
    }
    copyText(role_, sizeof(role_), role);
    paint();
}

void Ui::setStatus(const char *status)
{
    if (!ready_) {
        return;
    }
    copyText(status_, sizeof(status_), status);
    paint();
}

void Ui::onTransmit(const char *payload)
{
    if (!ready_) {
        return;
    }
    txCount_++;
    copyText(lastTx_, sizeof(lastTx_), payload);
    copyText(status_, sizeof(status_), "Sending");
    paint();
}

void Ui::onTransmitDone(int state)
{
    if (!ready_) {
        return;
    }
    if (state == 0) {
        copyText(status_, sizeof(status_), "Sent");
    } else {
        snprintf(status_, sizeof(status_), "TX err %d", state);
    }
    paint();
}

void Ui::onReceive(const char *payload, float rssi, float snr)
{
    if (!ready_) {
        return;
    }
    rxCount_++;
    hasRx_ = true;
    rssi_ = (int)rssi;
    snrTenths_ = (int)(snr * 10.0f);
    copyText(lastRx_, sizeof(lastRx_), payload);
    copyText(status_, sizeof(status_), "Received");
    paint();
}

void Ui::onReceiveError(int state)
{
    if (!ready_) {
        return;
    }
    snprintf(status_, sizeof(status_), "RX err %d", state);
    paint();
}

void Ui::paint()
{
    char line[24];
    char snrText[8];

    display.clearBuffer();

    display.setDrawColor(1);
    display.drawBox(0, 0, 128, 12);
    display.setDrawColor(0);
    display.drawStr(2, 10, "PING PONG");
    int roleWidth = display.getStrWidth(role_);
    display.drawStr(126 - roleWidth, 10, role_);
    display.setDrawColor(1);

    snprintf(line, sizeof(line), "TX %u", txCount_);
    display.drawStr(0, 24, line);
    display.drawStr(48, 24, lastTx_);

    snprintf(line, sizeof(line), "RX %u", rxCount_);
    display.drawStr(0, 36, line);
    display.drawStr(48, 36, lastRx_);

    if (hasRx_) {
        int frac = abs(snrTenths_ % 10);
        int whole = snrTenths_ / 10;
        if (snrTenths_ < 0 && whole == 0) {
            snprintf(snrText, sizeof(snrText), "-0.%d", frac);
        } else {
            snprintf(snrText, sizeof(snrText), "%d.%d", whole, frac);
        }
        snprintf(line, sizeof(line), "%d dBm  %s dB", rssi_, snrText);
        display.drawStr(0, 50, line);
    } else {
        display.drawStr(0, 50, "waiting for reply");
    }

    display.drawStr(0, 62, status_);
    display.sendBuffer();
}
