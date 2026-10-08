#pragma once

#include <Arduino.h>

// Onboard 0.96" SSD1306 (128x64) for the T3 LoRa32 ping-pong test.
class Ui {
public:
    bool begin();

    void setRole(const char *role);
    void setStatus(const char *status);
    void onTransmit(const char *payload);
    void onTransmitDone(int state);
    void onReceive(const char *payload, float rssi, float snr);
    void onReceiveError(int state);

private:
    void paint();
    void copyText(char *dst, size_t len, const char *src);

    bool ready_ = false;
    char role_[8] = "--";
    char status_[16] = "Boot";
    char lastTx_[18] = "-";
    char lastRx_[18] = "-";
    uint16_t txCount_ = 0;
    uint16_t rxCount_ = 0;
    int rssi_ = 0;
    int snrTenths_ = 0;
    bool hasRx_ = false;
};

extern Ui ui;
